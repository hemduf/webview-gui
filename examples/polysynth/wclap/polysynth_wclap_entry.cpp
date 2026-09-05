#include "../polysynth_plugin.h"
#include "../polysynth_preset_discovery.h"
#include "polysynth_preset_browser_proxy.h"

#include <clap/clap.h>

#include <cstring>

namespace webview_gui::examples::polysynth {
namespace {

const clap_plugin_factory_t *innerFactory() noexcept {
    return polysynthFactory();
}

uint32_t CLAP_ABI wclapFactoryGetPluginCount(const clap_plugin_factory_t *) {
    const auto *factory = innerFactory();
    return factory && factory->get_plugin_count
               ? factory->get_plugin_count(factory)
               : 0u;
}

const clap_plugin_descriptor_t *CLAP_ABI wclapFactoryGetPluginDescriptor(
    const clap_plugin_factory_t *, uint32_t index) {
    const auto *factory = innerFactory();
    return factory && factory->get_plugin_descriptor
               ? factory->get_plugin_descriptor(factory, index)
               : nullptr;
}

const clap_plugin_t *CLAP_ABI wclapFactoryCreatePlugin(
    const clap_plugin_factory_t *, const clap_host_t *host, const char *pluginId) {
    const auto *factory = innerFactory();
    if (!factory || !factory->create_plugin)
        return nullptr;

    const auto *inner = factory->create_plugin(factory, host, pluginId);
    if (!inner)
        return nullptr;

    // The WCLAP/native proxy keeps the already-qualified PolySynth DSP/CLAP
    // implementation unchanged while adding host-owned clap.gui + clap.webview,
    // the shared preset browser, bounded GUI parameter events and read-only RT
    // telemetry. It also contains the single historical pre-init clap.webview/3
    // compatibility exception required by the pinned bridge.
    const auto *wrapped = wclap::wrapPolySynthPresetBrowserPlugin(inner, host);
    if (wrapped)
        return wrapped;

    if (inner->destroy)
        inner->destroy(inner);
    return nullptr;
}

const clap_plugin_factory_t kWclapFactory{
    wclapFactoryGetPluginCount,
    wclapFactoryGetPluginDescriptor,
    wclapFactoryCreatePlugin,
};

bool CLAP_ABI polysynthWclapEntryInit(const char *) { return true; }
void CLAP_ABI polysynthWclapEntryDeinit() {}

const void *CLAP_ABI polysynthWclapEntryGetFactory(const char *factoryId) {
    if (!factoryId)
        return nullptr;
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kWclapFactory;
    if (presets::classifyPresetClapId(factoryId) == presets::ClapPresetSurface::EntryFactory)
        return polysynthPresetDiscoveryFactory();
    return nullptr;
}

} // namespace
} // namespace webview_gui::examples::polysynth

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    webview_gui::examples::polysynth::polysynthWclapEntryInit,
    webview_gui::examples::polysynth::polysynthWclapEntryDeinit,
    webview_gui::examples::polysynth::polysynthWclapEntryGetFactory,
};
