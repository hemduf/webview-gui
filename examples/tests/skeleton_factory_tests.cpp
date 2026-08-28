#include "skeleton_plugin.h"

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

} // namespace

int main() {
    const auto *factory = webview_gui::examples::skeletonFactory();
    if (!factory || factory->get_plugin_count(factory) != 1)
        return 1;

    const auto *descriptor = factory->get_plugin_descriptor(factory, 0);
    if (!descriptor || !descriptor->id ||
        std::strcmp(descriptor->id, webview_gui::examples::kSkeletonPluginId) != 0)
        return 2;

    if (factory->get_plugin_descriptor(factory, 1) != nullptr)
        return 3;

    const auto *plugin = factory->create_plugin(
        factory, &kHost, webview_gui::examples::kSkeletonPluginId);
    if (!plugin)
        return 4;

    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 5;
    }

    if (!plugin->activate(plugin, 48000.0, 1, 64)) {
        plugin->destroy(plugin);
        return 6;
    }

    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 7;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
