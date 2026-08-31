#include "../gain_plugin.h"
#include "webview-gui/wclap-legacy-webview-proxy.h"

#include <clap/clap.h>

#include <cstring>

namespace webview_gui::examples::gain {
namespace {

const clap_plugin_factory_t *innerFactory() noexcept {
    return gainFactory();
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

    // WebCLAP/wclap-bridge@cd11d22 embeds
    // geraintluff/webview-gui@172164b5, whose native helper queries the wrapper
    // plug-in's clap.webview/3 extension before it calls the module's
    // clap_plugin.init(). Keep the CLAP core strict and adapt only this WCLAP
    // factory boundary. The proxy returns a fail-closed WebView table pre-init and
    // resolves the real extension once the normal Gain init has completed.
    const auto *wrapped = ::webview_gui::wrapLegacyWclapWebviewPlugin(inner);
    if (wrapped)
        return wrapped;

    // Factory creation transfers ownership to the host. If the compatibility
    // allocation fails before an instance is returned, release the inner instance
    // through its mandatory destroy callback just as a failed init path would.
    if (inner->destroy)
        inner->destroy(inner);
    return nullptr;
}

const clap_plugin_factory_t kWclapFactory{
    wclapFactoryGetPluginCount,
    wclapFactoryGetPluginDescriptor,
    wclapFactoryCreatePlugin,
};

bool CLAP_ABI gainWclapEntryInit(const char *) { return true; }
void CLAP_ABI gainWclapEntryDeinit() {}

const void *CLAP_ABI gainWclapEntryGetFactory(const char *factoryId) {
    if (factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kWclapFactory;
    return nullptr;
}

} // namespace
} // namespace webview_gui::examples::gain

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    webview_gui::examples::gain::gainWclapEntryInit,
    webview_gui::examples::gain::gainWclapEntryDeinit,
    webview_gui::examples::gain::gainWclapEntryGetFactory,
};
