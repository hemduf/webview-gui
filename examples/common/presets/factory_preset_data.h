#pragma once

#include "../example_plugin_ids.h"
#include "preset_document.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace webview_gui::examples::presets {

inline constexpr std::string_view kPresetFileExtension = "wvpreset";
inline constexpr std::string_view kPresetFileSuffix = ".wvpreset";
inline constexpr std::string_view kGainFactoryTargetPluginId =
    plugin_ids::kGainPluginId;
inline constexpr std::string_view kPolySynthFactoryTargetPluginId =
    plugin_ids::kPolySynthPluginId;

template <typename Definitions>
[[nodiscard]] constexpr bool factoryDefinitionsHaveUniqueLoadKeys(
    const Definitions &definitions) noexcept {
    for (std::size_t i = 0u; i < definitions.size(); ++i) {
        if (definitions[i].loadKey.empty())
            return false;
        for (std::size_t j = 0u; j < i; ++j) {
            if (definitions[j].loadKey == definitions[i].loadKey)
                return false;
        }
    }
    return true;
}

namespace detail {

struct GainFactoryDefinition {
    std::string_view loadKey;
    std::string_view name;
    std::string_view description;
    double gainDb = 0.0;
};

struct PolyFactoryDefinition {
    std::string_view loadKey;
    std::string_view name;
    std::string_view description;
    std::string_view category;
    std::array<double, 13> values{};
};

inline constexpr std::array<GainFactoryDefinition, 3> kGainFactoryDefinitions{{
    {"gain:unity",
     "Unity",
     "Neutral unity-gain starting point.",
     0.0},
    {"gain:trim-minus-6db",
     "-6 dB Trim",
     "Clean six-decibel attenuation for gain staging.",
     -6.0},
    {"gain:boost-plus-6db",
     "+6 dB Boost",
     "Clean six-decibel boost for level matching.",
     6.0},
}};

inline constexpr std::array<PolyFactoryDefinition, 6> kPolyFactoryDefinitions{{
    {"polysynth:init",
     "Init",
     "Neutral PolySynth starting point with the published defaults.",
     "init",
     {{0.0, 0.0, 0.0, 0.0, 6000.0, 0.0, 0.01, 0.1, 0.8, 0.25, 0.0, 0.0, 1.0}}},
    {"polysynth:bass",
     "Bass",
     "Compact square-wave bass with a short filter contour.",
     "bass",
     {{-3.0, 2.0, -12.0, 0.0, 220.0, 0.25, 0.005, 0.15, 0.7, 0.2, 0.55, 0.0, 1.0}}},
    {"polysynth:lead",
     "Lead",
     "Focused saw lead with a responsive envelope.",
     "lead",
     {{-3.0, 1.0, 0.0, 0.0, 2500.0, 0.2, 0.01, 0.08, 0.75, 0.15, 0.3, 0.0, 1.0}}},
    {"polysynth:pad",
     "Pad",
     "Slow saw pad with a soft filter and long release.",
     "pad",
     {{-6.0, 1.0, 0.0, 0.0, 3500.0, 0.15, 1.5, 1.0, 0.8, 2.5, 0.2, 0.0, 0.85}}},
    {"polysynth:pluck",
     "Pluck",
     "Fast bright pluck demonstrating filter-envelope movement.",
     "pluck",
     {{-4.0, 1.0, 0.0, 0.0, 1800.0, 0.2, 0.001, 0.25, 0.05, 0.3, 0.7, 0.0, 1.0}}},
    {"polysynth:poly-expression-demo",
     "Poly Expression Demo",
     "Balanced base patch intended for per-note expressive control.",
     "expression",
     {{-6.0, 1.0, 0.0, 0.0, 5000.0, 0.2, 0.01, 0.15, 0.8, 0.5, 0.35, 0.0, 0.8}}},
}};

static_assert(factoryDefinitionsHaveUniqueLoadKeys(kGainFactoryDefinitions),
              "Gain factory definitions must have unique non-empty load keys");
static_assert(factoryDefinitionsHaveUniqueLoadKeys(kPolyFactoryDefinitions),
              "PolySynth factory definitions must have unique non-empty load keys");

inline PresetMetadata makeFactoryMetadata(std::string_view targetPluginId,
                                          std::string_view loadKey,
                                          std::string_view name,
                                          std::string_view description,
                                          std::string_view category) {
    PresetMetadata metadata;
    metadata.targetPluginId.assign(targetPluginId.data(), targetPluginId.size());
    metadata.name.assign(name.data(), name.size());
    metadata.creator = "webview-gui";
    metadata.description.assign(description.data(), description.size());
    metadata.tags = {"factory", std::string{category}};
    metadata.features = targetPluginId == kGainFactoryTargetPluginId
                            ? std::vector<std::string>{"audio-effect", "utility"}
                            : std::vector<std::string>{"instrument", "synthesizer"};
    metadata.factoryLoadKey = std::string{loadKey};
    return metadata;
}

inline PresetDocument makeGainFactoryDocument(const GainFactoryDefinition &definition) {
    PresetDocument document;
    document.metadata = makeFactoryMetadata(kGainFactoryTargetPluginId,
                                            definition.loadKey,
                                            definition.name,
                                            definition.description,
                                            "gain");
    document.parameters = {
        {0x1000u, definition.gainDb},
        {0x1001u, 0.0},
    };
    return document;
}

inline PresetDocument makePolyFactoryDocument(const PolyFactoryDefinition &definition) {
    PresetDocument document;
    document.metadata = makeFactoryMetadata(kPolySynthFactoryTargetPluginId,
                                            definition.loadKey,
                                            definition.name,
                                            definition.description,
                                            definition.category);
    document.parameters.reserve(definition.values.size());
    for (std::size_t i = 0u; i < definition.values.size(); ++i)
        document.parameters.push_back(
            {static_cast<StableParameterId>(1000u + i), definition.values[i]});
    return document;
}

} // namespace detail

} // namespace webview_gui::examples::presets
