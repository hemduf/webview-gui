#include "polysynth_plugin.h"

#include <clap/clap.h>
#include <clap/ext/tail.h>

#include <cstdint>
#include <limits>

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
    "webview-gui PolySynth tail tests",
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
    using namespace webview_gui::examples::polysynth;

    const auto *factory = polysynthFactory();
    if (!factory)
        return 1;

    const auto *plugin = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!plugin)
        return 2;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 3;
    }

    const auto *tail = static_cast<const clap_plugin_tail_t *>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    if (!tail || !tail->get) {
        plugin->destroy(plugin);
        return 4;
    }

    // The current reference patch configures a fixed 64-sample amplitude
    // release. Tail reporting must remain finite and match that actual release
    // contract rather than claiming zero or an unbounded/infinite tail.
    constexpr std::uint32_t expectedTailSamples = 64u;
    const auto beforeActivation = tail->get(plugin);
    if (beforeActivation != expectedTailSamples ||
        beforeActivation >= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        plugin->destroy(plugin);
        return 5;
    }

    if (!plugin->activate(plugin, 48000.0, 1, 64)) {
        plugin->destroy(plugin);
        return 6;
    }

    if (tail->get(plugin) != expectedTailSamples) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 7;
    }

    plugin->deactivate(plugin);

    // Tail is a patch property, not process-history state. It must remain stable
    // across activation/deactivation until a future host-facing release control
    // is deliberately wired to tail.changed().
    if (tail->get(plugin) != expectedTailSamples) {
        plugin->destroy(plugin);
        return 8;
    }

    plugin->destroy(plugin);
    return 0;
}
