#pragma once

#include "gain_event_processor.h"

#include <clap/clap.h>

namespace webview_gui::examples::gain {

inline constexpr const char *kGainPluginId = "com.webview-gui.example.gain";
inline constexpr clap_id kGainInputPortId = 0x2000u;
inline constexpr clap_id kGainOutputPortId = 0x2001u;

const clap_plugin_factory_t *gainFactory() noexcept;

} // namespace webview_gui::examples::gain
