#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace webview_gui::examples::presets {

inline constexpr std::uint32_t kCurrentPresetSchemaVersion = 1u;
using StableParameterId = std::uint32_t;

struct PresetParameterValue {
    StableParameterId stableParameterId = 0u;
    double value = 0.0;
};

using PresetSettingValue = std::variant<bool, std::int64_t, double, std::string>;

struct PersistentSetting {
    std::string key;
    PresetSettingValue value;
};

struct PresetMetadata {
    std::string targetPluginId;
    std::string name;
    std::string creator;
    std::string description;
    std::vector<std::string> tags;
    std::vector<std::string> features;
    std::optional<std::string> factoryLoadKey;
    std::optional<std::int64_t> creationTimestamp;
    std::optional<std::int64_t> modificationTimestamp;
};

struct PresetDocument {
    std::uint32_t schemaVersion = kCurrentPresetSchemaVersion;
    PresetMetadata metadata;
    std::vector<PresetParameterValue> parameters;
    std::vector<PersistentSetting> settings;
};

enum class PresetValidationError : std::uint8_t {
    None,
    UnsupportedSchemaVersion,
    MissingTargetPluginId,
    DuplicateParameterId,
    NonFiniteParameterValue,
    MissingSettingKey,
    DuplicateSettingKey,
    NonFiniteSettingValue,
};

struct PresetValidationResult {
    PresetValidationError error = PresetValidationError::None;
    std::size_t index = 0u;

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == PresetValidationError::None;
    }
};

struct PresetMetadataView {
    std::uint32_t schemaVersion = 0u;
    std::string_view targetPluginId;
    std::string_view name;
    std::string_view creator;
    std::string_view description;
    std::string_view factoryLoadKey;
    const std::vector<std::string> *tags = nullptr;
    const std::vector<std::string> *features = nullptr;
    const std::optional<std::int64_t> *creationTimestamp = nullptr;
    const std::optional<std::int64_t> *modificationTimestamp = nullptr;
};

[[nodiscard]] inline PresetMetadataView metadataView(const PresetDocument &document) noexcept {
    return {
        document.schemaVersion,
        document.metadata.targetPluginId,
        document.metadata.name,
        document.metadata.creator,
        document.metadata.description,
        document.metadata.factoryLoadKey ? std::string_view{*document.metadata.factoryLoadKey}
                                         : std::string_view{},
        &document.metadata.tags,
        &document.metadata.features,
        &document.metadata.creationTimestamp,
        &document.metadata.modificationTimestamp,
    };
}

[[nodiscard]] inline PresetValidationResult validatePresetDocument(
    const PresetDocument &document) noexcept {
    if (document.schemaVersion != kCurrentPresetSchemaVersion)
        return {PresetValidationError::UnsupportedSchemaVersion, 0u};

    if (document.metadata.targetPluginId.empty())
        return {PresetValidationError::MissingTargetPluginId, 0u};

    for (std::size_t i = 0; i < document.parameters.size(); ++i) {
        if (!std::isfinite(document.parameters[i].value))
            return {PresetValidationError::NonFiniteParameterValue, i};

        for (std::size_t j = 0; j < i; ++j) {
            if (document.parameters[j].stableParameterId ==
                document.parameters[i].stableParameterId)
                return {PresetValidationError::DuplicateParameterId, i};
        }
    }

    for (std::size_t i = 0; i < document.settings.size(); ++i) {
        const auto &setting = document.settings[i];
        if (setting.key.empty())
            return {PresetValidationError::MissingSettingKey, i};

        if (const auto *value = std::get_if<double>(&setting.value);
            value && !std::isfinite(*value))
            return {PresetValidationError::NonFiniteSettingValue, i};

        for (std::size_t j = 0; j < i; ++j) {
            if (document.settings[j].key == setting.key)
                return {PresetValidationError::DuplicateSettingKey, i};
        }
    }

    return {};
}

} // namespace webview_gui::examples::presets
