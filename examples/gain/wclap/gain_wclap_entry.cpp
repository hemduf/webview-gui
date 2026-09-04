#include "../gain_plugin.h"
#include "../gain_preset_discovery.h"
#include "webview-gui/wclap-legacy-webview-proxy.h"

#include <clap/clap.h>
#include <clap/factory/preset-discovery.h>

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

    const auto *wrapped = ::webview_gui::wrapLegacyWclapWebviewPlugin(inner);
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

bool CLAP_ABI gainWclapEntryInit(const char *) { return true; }
void CLAP_ABI gainWclapEntryDeinit() {}

const void *CLAP_ABI gainWclapEntryGetFactory(const char *factoryId) {
    if (!factoryId)
        return nullptr;
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kWclapFactory;
    if (std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0 ||
        std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
        return gainPresetDiscoveryFactory();
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
