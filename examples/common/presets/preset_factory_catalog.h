#pragma once

#include "preset_factory_definitions.h"

#if defined(__wasi__)

#include <cstddef>
#include <string_view>

namespace webview_gui::examples::presets {

class FactoryPresetCatalog {
public:
    constexpr explicit FactoryPresetCatalog(FactoryPresetFamily family) noexcept
        : family_(family) {}

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return factoryPresetCount(family_);
    }

    [[nodiscard]] constexpr FactoryPresetDefinitionView at(std::size_t index) const noexcept {
        return factoryPresetDefinitionAt(family_, index);
    }

    [[nodiscard]] constexpr FactoryPresetDefinitionView find(std::string_view loadKey) const noexcept {
        return findFactoryPresetDefinition(family_, loadKey);
    }

    [[nodiscard]] constexpr std::string_view targetPluginId() const noexcept {
        return factoryPresetTargetPluginId(family_);
    }

    [[nodiscard]] static constexpr std::string_view fileExtension() noexcept {
        return kPresetFileExtension;
    }

private:
    FactoryPresetFamily family_ = FactoryPresetFamily::Gain;
};

[[nodiscard]] inline const FactoryPresetCatalog &gainFactoryPresetCatalog() {
    static constexpr FactoryPresetCatalog catalog{FactoryPresetFamily::Gain};
    return catalog;
}

[[nodiscard]] inline const FactoryPresetCatalog &polySynthFactoryPresetCatalog() {
    static constexpr FactoryPresetCatalog catalog{FactoryPresetFamily::PolySynth};
    return catalog;
}

} // namespace webview_gui::examples::presets

#else

#include "preset_codec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webview_gui::examples::presets {

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
        {0x1000u, definition.values[0]},
        {0x1001u, definition.values[1]},
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
