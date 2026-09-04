#pragma once

#include "preset_document.h"

#if __has_include(<choc/text/choc_JSON.h>)
#include <choc/text/choc_JSON.h>
#elif __has_include("../../../include/webview-gui/_impl/platform/choc/choc/text/choc_JSON.h")
#include "../../../include/webview-gui/_impl/platform/choc/choc/text/choc_JSON.h"
#else
#error "Preset codec requires the pinned CHOC JSON headers"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace webview_gui::examples::presets {

enum class PresetCodecError : std::uint8_t {
    None,
    MalformedInput,
    TruncatedInput,
    InputTooLarge,
    NestingTooDeep,
    UnsupportedSchemaVersion,
    WrongTargetPlugin,
    InvalidDocument,
    InvalidParameter,
    DuplicateParameterId,
    NonFiniteParameterValue,
    InvalidSetting,
    MigrationFailed,
};

inline constexpr std::size_t kPresetCodecMaximumDocumentBytes = 512u * 1024u;
inline constexpr std::size_t kPresetCodecMaximumJsonDepth = 64u;

struct PresetCodecLimits {
    std::size_t maxDocumentBytes = kPresetCodecMaximumDocumentBytes;
    std::size_t maxJsonDepth = kPresetCodecMaximumJsonDepth;
};

struct PresetCodecResult {
    PresetCodecError error = PresetCodecError::None;
    std::string bytes;

    [[nodiscard]] bool ok() const noexcept { return error == PresetCodecError::None; }
};

struct PresetParseResult {
    PresetCodecError error = PresetCodecError::None;
    std::optional<PresetDocument> document;

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetCodecError::None && document.has_value();
    }
};

namespace detail {

[[nodiscard]] inline bool withinSizeLimit(std::size_t size,
                                          const PresetCodecLimits &limits) noexcept {
    return limits.maxDocumentBytes != 0u && size <= limits.maxDocumentBytes;
}

[[nodiscard]] inline std::size_t jsonDepth(std::string_view bytes) noexcept {
    std::size_t depth = 0u;
    std::size_t maxDepth = 0u;
    bool inString = false;
    bool escaped = false;
    for (const char c : bytes) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
            maxDepth = std::max(maxDepth, depth);
        } else if (c == '}' || c == ']') {
            if (depth == 0u)
                return std::numeric_limits<std::size_t>::max();
            --depth;
        }
    }
    if (inString || depth != 0u)
        return std::numeric_limits<std::size_t>::max();
    return maxDepth;
}

[[nodiscard]] inline bool validJsonDepth(std::string_view bytes,
                                         const PresetCodecLimits &limits) noexcept {
    if (limits.maxJsonDepth == 0u)
        return false;
    const auto depth = jsonDepth(bytes);
    return depth != std::numeric_limits<std::size_t>::max() &&
           depth <= limits.maxJsonDepth;
}

[[nodiscard]] inline PresetCodecError mapValidationError(
    PresetDocumentValidationError error) noexcept {
    switch (error) {
        case PresetDocumentValidationError::None:
            return PresetCodecError::None;
        case PresetDocumentValidationError::DuplicateParameterId:
            return PresetCodecError::DuplicateParameterId;
        case PresetDocumentValidationError::NonFiniteParameterValue:
            return PresetCodecError::NonFiniteParameterValue;
        case PresetDocumentValidationError::EmptySettingKey:
        case PresetDocumentValidationError::DuplicateSettingKey:
        case PresetDocumentValidationError::NonFiniteSettingValue:
            return PresetCodecError::InvalidSetting;
        case PresetDocumentValidationError::UnsupportedSchemaVersion:
            return PresetCodecError::UnsupportedSchemaVersion;
        case PresetDocumentValidationError::MissingTargetPluginId:
        case PresetDocumentValidationError::MissingPresetName:
        case PresetDocumentValidationError::InvalidFactoryLoadKey:
        case PresetDocumentValidationError::InvalidTimestamp:
        case PresetDocumentValidationError::FactoryMetadataMismatch:
            return PresetCodecError::InvalidDocument;
    }
    return PresetCodecError::InvalidDocument;
}

[[nodiscard]] inline choc::value::Value metadataToJson(const PresetMetadata &metadata) {
    auto result = choc::value::createObject("metadata");
    result.addMember("targetPluginId", metadata.targetPluginId);
    result.addMember("name", metadata.name);
    result.addMember("creator", metadata.creator);
    result.addMember("description", metadata.description);

    auto tags = choc::value::createArray();
    for (const auto &tag : metadata.tags)
        tags.addArrayElement(tag);
    result.addMember("tags", std::move(tags));

    auto features = choc::value::createArray();
    for (const auto &feature : metadata.features)
        features.addArrayElement(feature);
    result.addMember("features", std::move(features));

    if (metadata.creationTimestamp)
        result.addMember("creationTimestamp", *metadata.creationTimestamp);
    if (metadata.modificationTimestamp)
        result.addMember("modificationTimestamp", *metadata.modificationTimestamp);
    if (metadata.factoryLoadKey)
        result.addMember("factoryLoadKey", *metadata.factoryLoadKey);
    return result;
}

[[nodiscard]] inline choc::value::Value parametersToJson(
    const std::vector<PresetParameterValue> &parameters) {
    auto result = choc::value::createArray();
    for (const auto &parameter : parameters) {
        auto entry = choc::value::createObject("parameter");
        entry.addMember("id", static_cast<std::int64_t>(parameter.stableParameterId));
        entry.addMember("value", parameter.value);
        result.addArrayElement(std::move(entry));
    }
    return result;
}

[[nodiscard]] inline choc::value::Value settingValueToJson(const PresetSettingValue &value) {
    return std::visit(
        [](const auto &typedValue) -> choc::value::Value {
            using T = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<T, std::int64_t>)
                return choc::value::Value(typedValue);
            else if constexpr (std::is_same_v<T, double>)
                return choc::value::Value(typedValue);
            else if constexpr (std::is_same_v<T, bool>)
                return choc::value::Value(typedValue);
            else
                return choc::value::Value(typedValue);
        },
        value);
}

[[nodiscard]] inline choc::value::Value settingsToJson(
    const std::vector<PresetPersistentSetting> &settings) {
    auto result = choc::value::createArray();
    for (const auto &setting : settings) {
        auto entry = choc::value::createObject("setting");
        entry.addMember("key", setting.key);
        entry.addMember("value", settingValueToJson(setting.value));
        result.addArrayElement(std::move(entry));
    }
    return result;
}

[[nodiscard]] inline bool readStringMember(const choc::value::ValueView &object,
                                           std::string_view name,
                                           std::string &output) {
    if (!object.hasObjectMember(name))
        return false;
    const auto value = object[name];
    if (!value.isString())
        return false;
    output = value.getString();
    return true;
}

[[nodiscard]] inline bool readOptionalTimestamp(const choc::value::ValueView &object,
                                                std::string_view name,
                                                std::optional<std::int64_t> &output) {
    if (!object.hasObjectMember(name)) {
        output.reset();
        return true;
    }
    const auto value = object[name];
    if (!value.isInt64())
        return false;
    output = value.getInt64();
    return true;
}

[[nodiscard]] inline bool readStringArray(const choc::value::ValueView &object,
                                          std::string_view name,
                                          std::vector<std::string> &output) {
    if (!object.hasObjectMember(name))
        return false;
    const auto value = object[name];
    if (!value.isArray())
        return false;
    output.clear();
    output.reserve(value.size());
    for (std::uint32_t i = 0; i < value.size(); ++i) {
        const auto item = value[i];
        if (!item.isString())
            return false;
        output.emplace_back(item.getString());
    }
    return true;
}

[[nodiscard]] inline bool metadataFromJson(const choc::value::ValueView &value,
                                           PresetMetadata &metadata) {
    if (!value.isObject() ||
        !readStringMember(value, "targetPluginId", metadata.targetPluginId) ||
        !readStringMember(value, "name", metadata.name) ||
        !readStringMember(value, "creator", metadata.creator) ||
        !readStringMember(value, "description", metadata.description) ||
        !readStringArray(value, "tags", metadata.tags) ||
        !readStringArray(value, "features", metadata.features) ||
        !readOptionalTimestamp(value, "creationTimestamp", metadata.creationTimestamp) ||
        !readOptionalTimestamp(value, "modificationTimestamp", metadata.modificationTimestamp))
        return false;

    if (value.hasObjectMember("factoryLoadKey")) {
        const auto loadKey = value["factoryLoadKey"];
        if (!loadKey.isString())
            return false;
        metadata.factoryLoadKey = std::string{loadKey.getString()};
    } else {
        metadata.factoryLoadKey.reset();
    }
    return true;
}

[[nodiscard]] inline bool parametersFromJson(const choc::value::ValueView &value,
                                             std::vector<PresetParameterValue> &parameters) {
    if (!value.isArray())
        return false;
    parameters.clear();
    parameters.reserve(value.size());
    std::unordered_set<std::uint32_t> ids;
    ids.reserve(value.size());
    for (std::uint32_t i = 0; i < value.size(); ++i) {
        const auto entry = value[i];
        if (!entry.isObject() || !entry.hasObjectMember("id") ||
            !entry.hasObjectMember("value"))
            return false;
        const auto id = entry["id"];
        const auto parameterValue = entry["value"];
        if (!id.isInt64() || !parameterValue.isFloat64())
            return false;
        const auto rawId = id.getInt64();
        const auto rawValue = parameterValue.getFloat64();
        if (rawId < 0 || rawId > std::numeric_limits<std::uint32_t>::max() ||
            !std::isfinite(rawValue))
            return false;
        const auto stableId = static_cast<std::uint32_t>(rawId);
        if (!ids.insert(stableId).second)
            return false;
        parameters.push_back({stableId, rawValue});
    }
    return true;
}

[[nodiscard]] inline bool settingValueFromJson(const choc::value::ValueView &value,
                                               PresetSettingValue &output) {
    if (value.isInt64()) {
        output = value.getInt64();
        return true;
    }
    if (value.isFloat64()) {
        const auto parsed = value.getFloat64();
        if (!std::isfinite(parsed))
            return false;
        output = parsed;
        return true;
    }
    if (value.isBool()) {
        output = value.getBool();
        return true;
    }
    if (value.isString()) {
        output = std::string{value.getString()};
        return true;
    }
    return false;
}

[[nodiscard]] inline bool settingsFromJson(const choc::value::ValueView &value,
                                           std::vector<PresetPersistentSetting> &settings) {
    if (!value.isArray())
        return false;
    settings.clear();
    settings.reserve(value.size());
    std::unordered_set<std::string> keys;
    keys.reserve(value.size());
    for (std::uint32_t i = 0; i < value.size(); ++i) {
        const auto entry = value[i];
        if (!entry.isObject() || !entry.hasObjectMember("key") ||
            !entry.hasObjectMember("value"))
            return false;
        const auto keyValue = entry["key"];
        if (!keyValue.isString())
            return false;
        std::string key{keyValue.getString()};
        if (key.empty() || !keys.insert(key).second)
            return false;
        PresetSettingValue settingValue;
        if (!settingValueFromJson(entry["value"], settingValue))
            return false;
        settings.push_back({std::move(key), std::move(settingValue)});
    }
    return true;
}

[[nodiscard]] inline bool migratePresetDocument(PresetDocument &document) noexcept {
    while (document.schemaVersion < kPresetSchemaCurrentVersion) {
        switch (document.schemaVersion) {
            case 1u:
                document.schemaVersion = 2u;
                break;
            case 2u:
                document.schemaVersion = 3u;
                break;
            default:
                return false;
        }
    }
    return document.schemaVersion == kPresetSchemaCurrentVersion;
}

} // namespace detail

[[nodiscard]] inline PresetCodecResult serializePresetDocument(
    const PresetDocument &document,
    const PresetCodecLimits &limits = {}) {
    const auto validation = validatePresetDocument(document);
    if (!validation.ok())
        return {detail::mapValidationError(validation.error), {}};

    auto root = choc::value::createObject("webview_gui_preset");
    root.addMember("schemaVersion", static_cast<std::int64_t>(document.schemaVersion));
    root.addMember("metadata", detail::metadataToJson(document.metadata));
    root.addMember("parameters", detail::parametersToJson(document.parameters));
    root.addMember("settings", detail::settingsToJson(document.settings));

    auto bytes = choc::json::toString(root, false);
    if (!detail::withinSizeLimit(bytes.size(), limits))
        return {PresetCodecError::InputTooLarge, {}};
    if (!detail::validJsonDepth(bytes, limits))
        return {PresetCodecError::NestingTooDeep, {}};
    return {PresetCodecError::None, std::move(bytes)};
}

[[nodiscard]] inline PresetParseResult parsePresetDocument(
    std::string_view bytes,
    std::string_view expectedTargetPluginId,
    const PresetCodecLimits &limits = {}) {
    if (!detail::withinSizeLimit(bytes.size(), limits))
        return {PresetCodecError::InputTooLarge, std::nullopt};
    if (!detail::validJsonDepth(bytes, limits))
        return {PresetCodecError::NestingTooDeep, std::nullopt};

    choc::value::Value parsed;
    try {
        parsed = choc::json::parse(bytes);
    } catch (...) {
        return {PresetCodecError::MalformedInput, std::nullopt};
    }
    if (!parsed.isObject() || !parsed.hasObjectMember("schemaVersion") ||
        !parsed.hasObjectMember("metadata") || !parsed.hasObjectMember("parameters") ||
        !parsed.hasObjectMember("settings"))
        return {PresetCodecError::MalformedInput, std::nullopt};

    const auto schemaValue = parsed["schemaVersion"];
    if (!schemaValue.isInt64())
        return {PresetCodecError::MalformedInput, std::nullopt};
    const auto schema = schemaValue.getInt64();
    if (schema <= 0 || schema > std::numeric_limits<std::uint32_t>::max())
        return {PresetCodecError::UnsupportedSchemaVersion, std::nullopt};

    PresetDocument document;
    document.schemaVersion = static_cast<std::uint32_t>(schema);
    if (!detail::metadataFromJson(parsed["metadata"], document.metadata) ||
        !detail::parametersFromJson(parsed["parameters"], document.parameters) ||
        !detail::settingsFromJson(parsed["settings"], document.settings))
        return {PresetCodecError::MalformedInput, std::nullopt};

    if (!expectedTargetPluginId.empty() &&
        document.metadata.targetPluginId != expectedTargetPluginId)
        return {PresetCodecError::WrongTargetPlugin, std::nullopt};

    if (!detail::migratePresetDocument(document))
        return {PresetCodecError::MigrationFailed, std::nullopt};

    const auto validation = validatePresetDocument(document);
    if (!validation.ok())
        return {detail::mapValidationError(validation.error), std::nullopt};
    return {PresetCodecError::None, std::move(document)};
}

} // namespace webview_gui::examples::presets
