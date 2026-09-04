#pragma once

#include "../example_plugin_ids.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace webview_gui::examples::presets {

inline constexpr std::string_view kPresetFileExtension = "wvpreset";
inline constexpr std::string_view kPresetFileSuffix = ".wvpreset";
inline constexpr std::string_view kGainFactoryTargetPluginId = plugin_ids::kGainPluginId;
inline constexpr std::string_view kPolySynthFactoryTargetPluginId = plugin_ids::kPolySynthPluginId;

enum class FactoryPresetFamily : std::uint8_t {
    Gain,
    PolySynth,
};

struct FactoryPresetDefinitionView {
    std::string_view loadKey;
    std::string_view name;
    std::string_view description;
    std::string_view category;
    std::string_view targetPluginId;
    const double *values = nullptr;
    std::size_t valueCount = 0u;
    std::uint32_t firstStableParameterId = 0u;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return !loadKey.empty() && !name.empty() && !targetPluginId.empty() &&
               values != nullptr && valueCount != 0u;
    }
};

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
    std::array<double, 2> values{};
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
     {{0.0, 0.0}}},
    {"gain:trim-minus-6db",
     "-6 dB Trim",
     "Clean six-decibel attenuation for gain staging.",
     {{-6.0, 0.0}}},
    {"gain:boost-plus-6db",
     "+6 dB Boost",
     "Clean six-decibel boost for level matching.",
     {{6.0, 0.0}}},
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

} // namespace detail

[[nodiscard]] constexpr std::size_t factoryPresetCount(FactoryPresetFamily family) noexcept {
    return family == FactoryPresetFamily::Gain ? detail::kGainFactoryDefinitions.size()
                                               : detail::kPolyFactoryDefinitions.size();
}

[[nodiscard]] constexpr std::string_view factoryPresetTargetPluginId(
    FactoryPresetFamily family) noexcept {
    return family == FactoryPresetFamily::Gain ? kGainFactoryTargetPluginId
                                               : kPolySynthFactoryTargetPluginId;
}

[[nodiscard]] constexpr FactoryPresetDefinitionView factoryPresetDefinitionAt(
    FactoryPresetFamily family,
    std::size_t index) noexcept {
    if (family == FactoryPresetFamily::Gain) {
        if (index >= detail::kGainFactoryDefinitions.size())
            return {};
        const auto &definition = detail::kGainFactoryDefinitions[index];
        return {definition.loadKey,
                definition.name,
                definition.description,
                "gain",
                kGainFactoryTargetPluginId,
                definition.values.data(),
                definition.values.size(),
                0x1000u};
    }

    if (index >= detail::kPolyFactoryDefinitions.size())
        return {};
    const auto &definition = detail::kPolyFactoryDefinitions[index];
    return {definition.loadKey,
            definition.name,
            definition.description,
            definition.category,
            kPolySynthFactoryTargetPluginId,
            definition.values.data(),
            definition.values.size(),
            1000u};
}

[[nodiscard]] constexpr FactoryPresetDefinitionView findFactoryPresetDefinition(
    FactoryPresetFamily family,
    std::string_view loadKey) noexcept {
    for (std::size_t i = 0u; i < factoryPresetCount(family); ++i) {
        const auto definition = factoryPresetDefinitionAt(family, i);
        if (definition.loadKey == loadKey)
            return definition;
    }
    return {};
}

} // namespace webview_gui::examples::presets
