#pragma once

#include "preset_factory_catalog.h"

#include <array>
#include <string_view>

namespace webview_gui::examples::presets {

struct BundledFactoryPreset {
    std::string_view relativePath;
    std::string_view targetPluginId;
    std::string_view loadKey;
};

// Checked-in .wvpreset files are generated mirrors of the #101 catalog. The
// portability contract regenerates canonical bytes from the catalog and requires
// exact equality, so every format packages the same representation.
inline constexpr std::array<BundledFactoryPreset, 9> kBundledFactoryPresets{{
    {"factory/gain-unity.wvpreset", kGainFactoryTargetPluginId, "gain:unity"},
    {"factory/gain-trim-minus-6db.wvpreset", kGainFactoryTargetPluginId, "gain:trim-minus-6db"},
    {"factory/gain-boost-plus-6db.wvpreset", kGainFactoryTargetPluginId, "gain:boost-plus-6db"},
    {"factory/polysynth-init.wvpreset", kPolySynthFactoryTargetPluginId, "polysynth:init"},
    {"factory/polysynth-bass.wvpreset", kPolySynthFactoryTargetPluginId, "polysynth:bass"},
    {"factory/polysynth-lead.wvpreset", kPolySynthFactoryTargetPluginId, "polysynth:lead"},
    {"factory/polysynth-pad.wvpreset", kPolySynthFactoryTargetPluginId, "polysynth:pad"},
    {"factory/polysynth-pluck.wvpreset", kPolySynthFactoryTargetPluginId, "polysynth:pluck"},
    {"factory/polysynth-poly-expression-demo.wvpreset", kPolySynthFactoryTargetPluginId, "polysynth:poly-expression-demo"},
}};

[[nodiscard]] inline const FactoryPresetCatalog &factoryCatalogForTarget(
    std::string_view targetPluginId) {
    return targetPluginId == kGainFactoryTargetPluginId
               ? gainFactoryPresetCatalog()
               : polySynthFactoryPresetCatalog();
}

} // namespace webview_gui::examples::presets
