#pragma once

#include "polysynth_plugin.h"
#include "../common/preset_discovery_factory.h"
#include "../common/preset_production_catalog.h"
#include "../common/presets/preset_factory_catalog.h"

#include <memory>

namespace webview_gui::examples::polysynth {

struct PolySynthPresetDiscoveryTag {
    inline static constexpr const char *providerId =
        "com.webview-gui.example.polysynth.presets";
    inline static constexpr const char *providerName =
        "webview-gui PolySynth presets";
    inline static constexpr const char *vendor = "webview-gui";
    inline static constexpr const char *targetPluginId = kPolySynthPluginId;

    static std::unique_ptr<presets::PresetCatalog> createPresetCatalog() noexcept {
        return presets::makeDefaultProductionPresetCatalog(
            presets::polySynthFactoryPresetCatalog(), targetPluginId);
    }
};

inline const clap_preset_discovery_factory_t *polysynthPresetDiscoveryFactory() noexcept {
    return presets::presetDiscoveryFactory<PolySynthPresetDiscoveryTag>();
}

} // namespace webview_gui::examples::polysynth
