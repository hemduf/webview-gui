#pragma once

#include "gain_plugin.h"
#include "../common/preset_discovery_factory.h"

namespace webview_gui::examples::gain {

struct GainPresetDiscoveryTag {
    inline static constexpr const char *providerId =
        "com.webview-gui.example.gain.presets";
    inline static constexpr const char *providerName =
        "webview-gui Gain presets";
    inline static constexpr const char *vendor = "webview-gui";
    inline static constexpr const char *targetPluginId = kGainPluginId;
};

inline const clap_preset_discovery_factory_t *gainPresetDiscoveryFactory() noexcept {
    return presets::presetDiscoveryFactory<GainPresetDiscoveryTag>();
}

} // namespace webview_gui::examples::gain
