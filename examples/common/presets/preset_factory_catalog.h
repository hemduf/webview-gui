#pragma once

#include "preset_codec.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace webview_gui::examples::presets {

inline constexpr std::string_view kPresetFileExtension = ".wvpreset";
inline constexpr std::string_view kGainFactoryTargetPluginId =
    "com.webview-gui.example.gain";
inline constexpr std::string_view kPolySynthFactoryTargetPluginId =
    "com.webview-gui.example.polysynth";

enum class PresetContentKind : std::uint8_t {
    Factory,
    User,
};

enum class FactoryPresetCatalogError : std::uint8_t {
    None,
    NotFound,
};

struct FactoryPresetResource {
    std::string loadKey;
    PresetContentKind contentKind = PresetContentKind::Factory;
    PresetMetadata metadata;
    std::string bytes;
};

struct FactoryPresetLookupResult {
    FactoryPresetCatalogError error = FactoryPresetCatalogError::None;
    const FactoryPresetResource *resource = nullptr;

    [[nodiscard]] bool ok() const noexcept {
        return error == FactoryPresetCatalogError::None && resource != nullptr;
    }
};

class FactoryPresetCatalog {
public:
    constexpr FactoryPresetCatalog(const FactoryPresetResource *resources,
                                   std::size_t count) noexcept
        : resources_(resources), count_(count) {}

    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }

    [[nodiscard]] constexpr const FactoryPresetResource *at(
        std::size_t index) const noexcept {
        return index < count_ ? resources_ + index : nullptr;
    }

    [[nodiscard]] FactoryPresetLookupResult find(std::string_view loadKey) const noexcept {
        for (std::size_t i = 0u; i < count_; ++i) {
            if (resources_[i].loadKey == loadKey)
                return {FactoryPresetCatalogError::None, resources_ + i};
        }
        return {FactoryPresetCatalogError::NotFound, nullptr};
    }

    [[nodiscard]] static constexpr std::string_view fileExtension() noexcept {
        return kPresetFileExtension;
    }

private:
    const FactoryPresetResource *resources_ = nullptr;
    std::size_t count_ = 0u;
};

template <std::size_t N>
[[nodiscard]] constexpr bool factoryLoadKeysAreUnique(
    const std::array<std::string_view, N> &keys) noexcept {
    for (std::size_t i = 0u; i < N; ++i) {
        if (keys[i].empty())
            return false;
        for (std::size_t j = 0u; j < i; ++j) {
            if (keys[j] == keys[i])
                return false;
        }
    }
    return true;
}

namespace detail {

inline constexpr std::array<std::string_view, 3> kGainFactoryLoadKeys{{
    "gain:unity",
    "gain:trim-minus-6db",
    "gain:boost-plus-6db",
}};

inline constexpr std::array<std::string_view, 6> kPolyFactoryLoadKeys{{
    "polysynth:init",
    "polysynth:bass",
    "polysynth:lead",
    "polysynth:pad",
    "polysynth:pluck",
    "polysynth:poly-expression-demo",
}};

static_assert(factoryLoadKeysAreUnique(kGainFactoryLoadKeys),
              "Gain factory load keys must remain unique and non-empty");
static_assert(factoryLoadKeysAreUnique(kPolyFactoryLoadKeys),
              "PolySynth factory load keys must remain unique and non-empty");

inline FactoryPresetResource makeFactoryResource(PresetDocument document) {
    FactoryPresetResource resource;
    resource.loadKey = document.metadata.factoryLoadKey.value_or(std::string{});
    resource.contentKind = PresetContentKind::Factory;
    resource.metadata = document.metadata;

    const auto encoded = serializePresetDocument(document);
    if (encoded.ok())
        resource.bytes = encoded.bytes;
    return resource;
}

inline PresetMetadata makeMetadata(std::string_view targetPluginId,
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

inline FactoryPresetResource makeGainResource(std::string_view loadKey,
                                              std::string_view name,
                                              std::string_view description,
                                              double gainDb) {
    PresetDocument document;
    document.metadata = makeMetadata(kGainFactoryTargetPluginId,
                                     loadKey,
                                     name,
                                     description,
                                     "gain");
    document.parameters = {
        {0x1000u, gainDb},
        {0x1001u, 0.0},
    };
    return makeFactoryResource(std::move(document));
}

inline FactoryPresetResource makePolyResource(
    std::string_view loadKey,
    std::string_view name,
    std::string_view description,
    std::string_view category,
    const std::array<double, 13> &values) {
    PresetDocument document;
    document.metadata = makeMetadata(kPolySynthFactoryTargetPluginId,
                                     loadKey,
                                     name,
                                     description,
                                     category);
    document.parameters.reserve(values.size());
    for (std::size_t i = 0u; i < values.size(); ++i)
        document.parameters.push_back(
            {static_cast<StableParameterId>(1000u + i), values[i]});
    return makeFactoryResource(std::move(document));
}

inline const std::array<FactoryPresetResource, 3> &gainFactoryResources() {
    static const std::array<FactoryPresetResource, 3> resources{{
        makeGainResource("gain:unity",
                         "Unity",
                         "Neutral unity-gain starting point.",
                         0.0),
        makeGainResource("gain:trim-minus-6db",
                         "-6 dB Trim",
                         "Clean six-decibel attenuation for gain staging.",
                         -6.0),
        makeGainResource("gain:boost-plus-6db",
                         "+6 dB Boost",
                         "Clean six-decibel boost for level matching.",
                         6.0),
    }};
    return resources;
}

inline const std::array<FactoryPresetResource, 6> &polyFactoryResources() {
    static const std::array<FactoryPresetResource, 6> resources{{
        makePolyResource(
            "polysynth:init",
            "Init",
            "Neutral PolySynth starting point with the published defaults.",
            "init",
            {{0.0, 0.0, 0.0, 0.0, 6000.0, 0.0, 0.01, 0.1, 0.8, 0.25, 0.0, 0.0, 1.0}}),
        makePolyResource(
            "polysynth:bass",
            "Bass",
            "Compact square-wave bass with a short filter contour.",
            "bass",
            {{-3.0, 2.0, -12.0, 0.0, 220.0, 0.25, 0.005, 0.15, 0.7, 0.2, 0.55, 0.0, 1.0}}),
        makePolyResource(
            "polysynth:lead",
            "Lead",
            "Focused saw lead with a responsive envelope.",
            "lead",
            {{-3.0, 1.0, 0.0, 0.0, 2500.0, 0.2, 0.01, 0.08, 0.75, 0.15, 0.3, 0.0, 1.0}}),
        makePolyResource(
            "polysynth:pad",
            "Pad",
            "Slow saw pad with a soft filter and long release.",
            "pad",
            {{-6.0, 1.0, 0.0, 0.0, 3500.0, 0.15, 1.5, 1.0, 0.8, 2.5, 0.2, 0.0, 0.85}}),
        makePolyResource(
            "polysynth:pluck",
            "Pluck",
            "Fast bright pluck demonstrating filter-envelope movement.",
            "pluck",
            {{-4.0, 1.0, 0.0, 0.0, 1800.0, 0.2, 0.001, 0.25, 0.05, 0.3, 0.7, 0.0, 1.0}}),
        makePolyResource(
            "polysynth:poly-expression-demo",
            "Poly Expression Demo",
            "Balanced base patch intended for per-note expressive control.",
            "expression",
            {{-6.0, 1.0, 0.0, 0.0, 5000.0, 0.2, 0.01, 0.15, 0.8, 0.5, 0.35, 0.0, 0.8}}),
    }};
    return resources;
}

} // namespace detail

[[nodiscard]] inline const FactoryPresetCatalog &gainFactoryPresetCatalog() {
    const auto &resources = detail::gainFactoryResources();
    static const FactoryPresetCatalog catalog{resources.data(), resources.size()};
    return catalog;
}

[[nodiscard]] inline const FactoryPresetCatalog &polySynthFactoryPresetCatalog() {
    const auto &resources = detail::polyFactoryResources();
    static const FactoryPresetCatalog catalog{resources.data(), resources.size()};
    return catalog;
}

} // namespace webview_gui::examples::presets
