#pragma once

#if defined(__wasi__)

#include "../example_plugin_ids.h"

#include <cstddef>
#include <string_view>

namespace webview_gui::examples::presets {

inline constexpr std::string_view kPresetFileExtension = "wvpreset";
inline constexpr std::string_view kPresetFileSuffix = ".wvpreset";
inline constexpr std::string_view kGainFactoryTargetPluginId =
    plugin_ids::kGainPluginId;
inline constexpr std::string_view kPolySynthFactoryTargetPluginId =
    plugin_ids::kPolySynthPluginId;

// #94 owns real WCLAP Preset Discovery + factory loading. Until that bounded
// stage lands, keep the #92 instance build free of the CHOC JSON parser and do
// not synthesize factory data in WASI.
class FactoryPresetCatalog {
public:
    [[nodiscard]] constexpr std::size_t size() const noexcept { return 0u; }
    [[nodiscard]] static constexpr std::string_view fileExtension() noexcept {
        return kPresetFileExtension;
    }
};

[[nodiscard]] inline const FactoryPresetCatalog &gainFactoryPresetCatalog() {
    static const FactoryPresetCatalog catalog{};
    return catalog;
}

[[nodiscard]] inline const FactoryPresetCatalog &polySynthFactoryPresetCatalog() {
    static const FactoryPresetCatalog catalog{};
    return catalog;
}

} // namespace webview_gui::examples::presets

#else

#include "../example_plugin_ids.h"
#include "preset_codec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webview_gui::examples::presets {

inline constexpr std::string_view kPresetFileExtension = "wvpreset";
inline constexpr std::string_view kPresetFileSuffix = ".wvpreset";
inline constexpr std::string_view kGainFactoryTargetPluginId =
    plugin_ids::kGainPluginId;
inline constexpr std::string_view kPolySynthFactoryTargetPluginId =
    plugin_ids::kPolySynthPluginId;

enum class PresetContentKind : std::uint8_t {
    Factory,
    User,
};

enum class FactoryPresetCatalogError : std::uint8_t {
    None,
    NotFound,
    InvalidResource,
};

struct FactoryPresetResource {
    std::string loadKey;
    PresetContentKind contentKind = PresetContentKind::Factory;
    PresetMetadata metadata;
    PresetCodecError codecError = PresetCodecError::None;
    std::string bytes;

    [[nodiscard]] bool valid() const noexcept {
        return codecError == PresetCodecError::None && !bytes.empty() &&
               !loadKey.empty() && metadata.factoryLoadKey.has_value() &&
               *metadata.factoryLoadKey == loadKey;
    }
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

    [[nodiscard]] const FactoryPresetResource *at(std::size_t index) const noexcept {
        if (index >= count_)
            return nullptr;
        const auto *resource = resources_ + index;
        return resource->valid() ? resource : nullptr;
    }

    [[nodiscard]] FactoryPresetLookupResult find(std::string_view loadKey) const noexcept {
        for (std::size_t i = 0u; i < count_; ++i) {
            const auto *resource = resources_ + i;
            if (resource->loadKey != loadKey)
                continue;
            if (!resource->valid())
                return {FactoryPresetCatalogError::InvalidResource, nullptr};
            return {FactoryPresetCatalogError::None, resource};
        }
        return {FactoryPresetCatalogError::NotFound, nullptr};
    }

    // CLAP Preset Discovery expects the extension without the leading dot.
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

inline FactoryPresetResource makeFactoryResource(PresetDocument document) {
    FactoryPresetResource resource;
    resource.loadKey = document.metadata.factoryLoadKey.value_or(std::string{});
    resource.contentKind = PresetContentKind::Factory;
    resource.metadata = document.metadata;

    auto encoded = serializePresetDocument(document);
    resource.codecError = encoded.error;
    resource.bytes = std::move(encoded.bytes);
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

inline FactoryPresetResource makeGainResource(const GainFactoryDefinition &definition) {
    PresetDocument document;
    document.metadata = makeMetadata(kGainFactoryTargetPluginId,
                                     definition.loadKey,
                                     definition.name,
                                     definition.description,
                                     "gain");
    document.parameters = {
        {0x1000u, definition.gainDb},
        {0x1001u, 0.0},
    };
    return makeFactoryResource(std::move(document));
}

inline FactoryPresetResource makePolyResource(const PolyFactoryDefinition &definition) {
    PresetDocument document;
    document.metadata = makeMetadata(kPolySynthFactoryTargetPluginId,
                                     definition.loadKey,
                                     definition.name,
                                     definition.description,
                                     definition.category);
    document.parameters.reserve(definition.values.size());
    for (std::size_t i = 0u; i < definition.values.size(); ++i)
        document.parameters.push_back(
            {static_cast<StableParameterId>(1000u + i), definition.values[i]});
    return makeFactoryResource(std::move(document));
}

inline const std::array<FactoryPresetResource, kGainFactoryDefinitions.size()> &
gainFactoryResources() {
    static const auto resources = [] {
        std::array<FactoryPresetResource, kGainFactoryDefinitions.size()> result{};
        for (std::size_t i = 0u; i < kGainFactoryDefinitions.size(); ++i)
            result[i] = makeGainResource(kGainFactoryDefinitions[i]);
        return result;
    }();
    return resources;
}

inline const std::array<FactoryPresetResource, kPolyFactoryDefinitions.size()> &
polyFactoryResources() {
    static const auto resources = [] {
        std::array<FactoryPresetResource, kPolyFactoryDefinitions.size()> result{};
        for (std::size_t i = 0u; i < kPolyFactoryDefinitions.size(); ++i)
            result[i] = makePolyResource(kPolyFactoryDefinitions[i]);
        return result;
    }();
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

#endif
