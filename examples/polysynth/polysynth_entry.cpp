#include "polysynth_plugin.h"
#include "polysynth_preset_discovery.h"
#include "wclap/polysynth_preset_browser_proxy.h"

#include <cstring>

namespace webview_gui::examples::polysynth {
namespace {

const clap_plugin_factory_t *innerFactory() noexcept {
    return polysynthFactory();
}

uint32_t CLAP_ABI nativeFactoryGetPluginCount(const clap_plugin_factory_t *) {
    const auto *factory = innerFactory();
    return factory && factory->get_plugin_count
               ? factory->get_plugin_count(factory)
               : 0u;
}

const clap_plugin_descriptor_t *CLAP_ABI nativeFactoryGetPluginDescriptor(
    const clap_plugin_factory_t *, uint32_t index) {
    const auto *factory = innerFactory();
    return factory && factory->get_plugin_descriptor
               ? factory->get_plugin_descriptor(factory, index)
               : nullptr;
}

const clap_plugin_t *CLAP_ABI nativeFactoryCreatePlugin(
    const clap_plugin_factory_t *, const clap_host_t *host, const char *pluginId) {
    const auto *factory = innerFactory();
    if (!factory || !factory->create_plugin)
        return nullptr;

    const auto *inner = factory->create_plugin(factory, host, pluginId);
    if (!inner)
        return nullptr;

    // Native CLAP and WCLAP intentionally share the exact same WebView proxy,
    // parameter gesture bridge, resources, preset browser and RT-safe telemetry
    // handoff. ClapWebviewGui negotiates the native Cocoa/Win32/X11 backend when
    // the host does not own a WebView.
    const auto *wrapped = wclap::wrapPolySynthPresetBrowserPlugin(inner, host);
    if (wrapped)
        return wrapped;

    if (inner->destroy)
        inner->destroy(inner);
    return nullptr;
}

const clap_plugin_factory_t kNativeFactory{
    nativeFactoryGetPluginCount,
    nativeFactoryGetPluginDescriptor,
    nativeFactoryCreatePlugin,
};

bool CLAP_ABI polysynthEntryInit(const char *) { return true; }
void CLAP_ABI polysynthEntryDeinit() {}

const void *CLAP_ABI polysynthEntryGetFactory(const char *factoryId) {
    if (!factoryId)
        return nullptr;
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kNativeFactory;
    if (presets::classifyPresetClapId(factoryId) == presets::ClapPresetSurface::EntryFactory)
        return polysynthPresetDiscoveryFactory();
    return nullptr;
}

} // namespace
} // namespace webview_gui::examples::polysynth

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    webview_gui::examples::polysynth::polysynthEntryInit,
    webview_gui::examples::polysynth::polysynthEntryDeinit,
    webview_gui::examples::polysynth::polysynthEntryGetFactory,
};
