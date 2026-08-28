#include "skeleton_plugin.h"
#include "test_support.h"

#include <clap/clap.h>

#include <cstring>

namespace {

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) {
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui example tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

bool testReusableEventAndAudioHelpers() {
    using namespace webview_gui::examples::test_support;

    StereoFloatBlock block{8};
    block.fillInput(0.25f, -0.5f);
    if (block.frames() != 8 || block.input()->channel_count != 2 ||
        block.output()->channel_count != 2)
        return false;
    if (block.inputChannel(0)[3] != 0.25f || block.inputChannel(1)[3] != -0.5f)
        return false;

    InputEvents events;
    if (!events.pushParamValue(3, 7, 0.5))
        return false;
    if (!events.pushParamMod(4, 7, 0.25, 42, 0, 1, 60))
        return false;
    if (!events.pushNote(CLAP_EVENT_NOTE_ON, 5, 42, 0, 1, 60, 0.8))
        return false;
    if (!events.pushNoteExpression(6, CLAP_NOTE_EXPRESSION_TUNING,
                                   42, 0, 1, 60, 0.125))
        return false;
    if (events.pushParamValue(2, 7, 1.0))
        return false; // timestamps must remain monotonic

    const auto *inputEvents = events.clapInputEvents();
    if (inputEvents->size(inputEvents) != 4)
        return false;
    if (inputEvents->get(inputEvents, 0)->time != 3 ||
        inputEvents->get(inputEvents, 1)->type != CLAP_EVENT_PARAM_MOD ||
        inputEvents->get(inputEvents, 2)->type != CLAP_EVENT_NOTE_ON ||
        inputEvents->get(inputEvents, 3)->type != CLAP_EVENT_NOTE_EXPRESSION)
        return false;

    CapturedOutputEvents captured;
    clap_event_note_t noteEnd{};
    noteEnd.header.size = sizeof(noteEnd);
    noteEnd.header.time = 7;
    noteEnd.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    noteEnd.header.type = CLAP_EVENT_NOTE_END;
    noteEnd.note_id = 42;
    noteEnd.port_index = 0;
    noteEnd.channel = 1;
    noteEnd.key = 60;

    auto *outputEvents = captured.clapOutputEvents();
    if (!outputEvents->try_push(outputEvents, &noteEnd.header))
        return false;
    if (captured.size() != 1 || captured.header(0)->type != CLAP_EVENT_NOTE_END ||
        captured.header(0)->time != 7)
        return false;

    return true;
}

} // namespace

int main() {
    if (!testReusableEventAndAudioHelpers())
        return 1;

    const auto *factory = webview_gui::examples::skeletonFactory();
    if (!factory || factory->get_plugin_count(factory) != 1)
        return 2;

    const auto *descriptor = factory->get_plugin_descriptor(factory, 0);
    if (!descriptor || !descriptor->id ||
        std::strcmp(descriptor->id, webview_gui::examples::kSkeletonPluginId) != 0)
        return 3;

    if (factory->get_plugin_descriptor(factory, 1) != nullptr)
        return 4;

    const auto *plugin = factory->create_plugin(
        factory, &kHost, webview_gui::examples::kSkeletonPluginId);
    if (!plugin)
        return 5;

    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 6;
    }

    if (!plugin->activate(plugin, 48000.0, 1, 64)) {
        plugin->destroy(plugin);
        return 7;
    }

    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 8;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
