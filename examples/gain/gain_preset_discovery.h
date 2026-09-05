#pragma once

#include "gain_plugin.h"
#include "../common/preset_discovery_factory.h"
#include "../common/preset_runtime_catalog.h"
#include "../common/presets/preset_factory_catalog.h"

#include <memory>

namespace webview_gui::examples::gain {

struct GainPresetDiscoveryTag {
    inline static constexpr const char *providerId =
        "com.webview-gui.example.gain.presets";
    inline static constexpr const char *providerName =
        "webview-gui Gain presets";
    inline static constexpr const char *vendor = "webview-gui";
    inline static constexpr const char *targetPluginId = kGainPluginId;

    static std::unique_ptr<presets::PresetCatalog> createPresetCatalog() noexcept {
        return presets::makeDefaultProductionPresetCatalog(
            presets::gainFactoryPresetCatalog(), targetPluginId);
    }
};

inline const clap_preset_discovery_factory_t *gainPresetDiscoveryFactory() noexcept {
    return presets::presetDiscoveryFactory<GainPresetDiscoveryTag>();
}

} // namespace webview_gui::examples::gain
