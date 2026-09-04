#pragma once

#include "../common/example_plugin_ids.h"
#include "../common/preset_catalog_factory.h"

#include <clap/clap.h>

#include <cstddef>

namespace webview_gui::examples::polysynth {

inline constexpr const char *kPolySynthPluginId = plugin_ids::kPolySynthPluginId;
inline constexpr clap_id kPolySynthAudioOutputPortId = 0x3000u;
inline constexpr clap_id kPolySynthNoteInputPortId = 0x3100u;
inline constexpr std::size_t kPolySynthDefaultVoiceCount = 16u;

const clap_plugin_t *createPolySynthPlugin(
    const clap_host_t *host,
    presets::PresetCatalogFactory presetCatalogFactory) noexcept;
const clap_plugin_factory_t *polysynthFactory() noexcept;

} // namespace webview_gui::examples::polysynth
