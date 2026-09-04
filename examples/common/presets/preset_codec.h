#pragma once

#include "preset_document.h"

#include <choc/text/choc_JSON.h>

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

struct PresetSerializeResult {
    PresetCodecError error = PresetCodecError::None;
    std::string bytes;

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetCodecError::None;
    }
};

struct PresetParseResult {
    PresetCodecError error = PresetCodecError::None;
    std::optional<PresetDocument> document;

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetCodecError::None && document.has_value();
    }
};

struct PresetMetadataParseResult {
    PresetCodecError error = PresetCodecError::None;
    std::uint32_t schemaVersion = 0u;
    PresetMetadata metadata;

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetCodecError::None;
    }
};

namespace detail {

inline constexpr std::string_view kPresetMagic = "WVPRESET\n";
inline constexpr std::size_t kMaxPresetBytes = 1024u * 1024u;
inline constexpr std::size_t kMaxMetadataBytes = 64u * 1024u;
inline constexpr std::size_t kMaxStringBytes = 64u * 1024u;
inline constexpr std::size_t kMaxParameters = 4096u;
inline constexpr std::size_t kMaxSettings = 1024u;
inline constexpr std::size_t kMaxExtensions = 1024u;
inline constexpr std::size_t kMaxStringListEntries = 1024u;
inline constexpr std::size_t kMaxJsonNesting = 64u;

struct PresetFrame {
    std::string_view metadataJson;
    std::string_view payloadJson;
};

[[nodiscard]] inline PresetCodecError splitFrame(std::string_view bytes,
                                                  PresetFrame &frame) noexcept {
    if (bytes.size() > kMaxPresetBytes)
        return PresetCodecError::InputTooLarge;

    if (bytes.size() < kPresetMagic.size()) {
        if (kPresetMagic.substr(0u, bytes.size()) == bytes)
            return PresetCodecError::TruncatedInput;
        return PresetCodecError::MalformedInput;
    }

    if (bytes.substr(0u, kPresetMagic.size()) != kPresetMagic)
        return PresetCodecError::MalformedInput;

    if (bytes.empty() || bytes.back() != '\n')
        return PresetCodecError::TruncatedInput;

    const auto metadataStart = kPresetMagic.size();
    const auto metadataEnd = bytes.find('\n', metadataStart);
    if (metadataEnd == std::string_view::npos)
        return PresetCodecError::TruncatedInput;

    if (metadataEnd - metadataStart > kMaxMetadataBytes)
        return PresetCodecError::InputTooLarge;

    const auto payloadStart = metadataEnd + 1u;
    const auto payloadEnd = bytes.find('\n', payloadStart);
    if (payloadEnd == std::string_view::npos)
        return PresetCodecError::TruncatedInput;

    if (payloadEnd != bytes.size() - 1u)
        return PresetCodecError::MalformedInput;

    frame.metadataJson = bytes.substr(metadataStart, metadataEnd - metadataStart);
    frame.payloadJson = bytes.substr(payloadStart, payloadEnd - payloadStart);
    return PresetCodecError::None;
}

[[nodiscard]] inline bool jsonNestingWithinLimit(std::string_view json) noexcept {
    std::size_t depth = 0u;
    bool inString = false;
    bool escaped = false;

    for (const char c : json) {
        if (inString) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '"')
                inString = false;
            continue;
        }

        if (c == '"') {
            inString = true;
            continue;
        }

        if (c == '{' || c == '[') {
            ++depth;
            if (depth > kMaxJsonNesting)
                return false;
            continue;
        }

        if ((c == '}' || c == ']') && depth != 0u)
            --depth;
    }

    return true;
}

[[nodiscard]] inline PresetCodecError textCodecError(
    std::string_view text) noexcept {
    if (text.size() > kMaxStringBytes)
        return PresetCodecError::InputTooLarge;
    if (text.find('\0') != std::string_view::npos)
        return PresetCodecError::InvalidDocument;
    return PresetCodecError::None;
}

[[nodiscard]] inline PresetCodecError scalarTextCodecError(
    const PresetScalarValue &value) noexcept {
    if (const auto *text = std::get_if<std::string>(&value))
        return textCodecError(*text);
    return PresetCodecError::None;
}

[[nodiscard]] inline PresetCodecError documentTextCodecError(
    const PresetDocument &document) noexcept {
    const std::string_view metadataStrings[] = {
        document.metadata.targetPluginId,
        document.metadata.name,
        document.metadata.creator,
        document.metadata.description,
    };
    for (const auto text : metadataStrings) {
        const auto error = textCodecError(text);
        if (error != PresetCodecError::None)
            return error;
    }

    if (document.metadata.factoryLoadKey) {
        const auto error = textCodecError(*document.metadata.factoryLoadKey);
        if (error != PresetCodecError::None)
            return error;
    }

    for (const auto &tag : document.metadata.tags) {
        const auto error = textCodecError(tag);
        if (error != PresetCodecError::None)
            return error;
    }
    for (const auto &feature : document.metadata.features) {
        const auto error = textCodecError(feature);
        if (error != PresetCodecError::None)
            return error;
    }
    for (const auto &extension : document.metadata.extensions) {
        if (const auto error = textCodecError(extension.key);
            error != PresetCodecError::None)
            return error;
        if (const auto error = scalarTextCodecError(extension.value);
            error != PresetCodecError::None)
            return error;
    }
    for (const auto &setting : document.settings) {
        if (const auto error = textCodecError(setting.key);
            error != PresetCodecError::None)
            return error;
        if (const auto error = scalarTextCodecError(setting.value);
            error != PresetCodecError::None)
            return error;
    }
    return PresetCodecError::None;
}

[[nodiscard]] inline PresetCodecError mapValidationError(PresetValidationError error) noexcept {
    switch (error) {
        case PresetValidationError::None:
            return PresetCodecError::None;
        case PresetValidationError::UnsupportedSchemaVersion:
            return PresetCodecError::UnsupportedSchemaVersion;
        case PresetValidationError::DuplicateParameterId:
            return PresetCodecError::DuplicateParameterId;
        case PresetValidationError::NonFiniteParameterValue:
            return PresetCodecError::NonFiniteParameterValue;
        default:
            return PresetCodecError::InvalidDocument;
    }
}

inline void appendJsonString(std::string &out, std::string_view value) {
    out += choc::json::getEscapedQuotedString(value);
}

inline void appendStringArray(std::string &out,
                              const std::vector<std::string> &values) {
    out.push_back('[');
    for (std::size_t i = 0u; i < values.size(); ++i) {
        if (i != 0u)
            out.push_back(',');
        appendJsonString(out, values[i]);
    }
    out.push_back(']');
}

inline void appendScalar(std::string &out, const PresetScalarValue &value) {
    std::visit(
        [&out](const auto &typedValue) {
            using T = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<T, bool>) {
                out += typedValue ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                out += std::to_string(typedValue);
            } else if constexpr (std::is_same_v<T, double>) {
                out += choc::json::doubleToString(typedValue);
            } else {
                appendJsonString(out, typedValue);
            }
        },
        value);
}

inline std::string scalarTypeName(const PresetScalarValue &value) {
    return std::visit(
        [](const auto &typedValue) -> std::string {
            using T = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<T, bool>)
                return "bool";
            if constexpr (std::is_same_v<T, std::int64_t>)
                return "i64";
            if constexpr (std::is_same_v<T, double>)
                return "f64";
            return "string";
        },
        value);
}

inline void appendTaggedScalarEntry(std::string &out,
                                    std::string_view key,
                                    const PresetScalarValue &value) {
    out += "{\"key\":";
    appendJsonString(out, key);
    out += ",\"type\":";
    appendJsonString(out, scalarTypeName(value));
    out += ",\"value\":";
    appendScalar(out, value);
    out.push_back('}');
}

[[nodiscard]] inline bool copyString(const choc::value::ValueView &value,
                                     std::string &out) {
    if (!value.isString())
        return false;
    const auto text = value.getString();
    if (text.size() > kMaxStringBytes)
        return false;
    out.assign(text.data(), text.size());
    return true;
}

[[nodiscard]] inline bool requiredStringMember(const choc::value::ValueView &object,
                                                const char *name,
                                                std::string &out) {
    const auto value = object[name];
    return !value.isVoid() && copyString(value, out);
}

[[nodiscard]] inline bool optionalStringMember(const choc::value::ValueView &object,
                                                const char *name,
                                                std::string &out) {
    const auto value = object[name];
    if (value.isVoid()) {
        out.clear();
        return true;
    }
    return copyString(value, out);
}

[[nodiscard]] inline bool optionalStringArrayMember(const choc::value::ValueView &object,
                                                     const char *name,
                                                     std::vector<std::string> &out) {
    const auto value = object[name];
    if (value.isVoid()) {
        out.clear();
        return true;
    }
    if (!value.isArray() || value.size() > kMaxStringListEntries)
        return false;

    std::vector<std::string> candidate;
    candidate.reserve(value.size());
    for (std::uint32_t i = 0u; i < value.size(); ++i) {
        std::string text;
        if (!copyString(value[i], text))
            return false;
        candidate.push_back(std::move(text));
    }
    out = std::move(candidate);
    return true;
}

[[nodiscard]] inline bool readInteger(const choc::value::ValueView &value,
                                      std::int64_t &out) {
    if (!value.isInt())
        return false;
    out = value.get<std::int64_t>();
    return true;
}

[[nodiscard]] inline bool optionalIntegerMember(const choc::value::ValueView &object,
                                                 const char *name,
                                                 std::optional<std::int64_t> &out) {
    const auto value = object[name];
    if (value.isVoid()) {
        out.reset();
        return true;
    }

    std::int64_t candidate = 0;
    if (!readInteger(value, candidate))
        return false;
    out = candidate;
    return true;
}

[[nodiscard]] inline bool parseTaggedScalar(const choc::value::ValueView &entry,
                                             PresetScalarValue &out,
                                             bool &nonFinite) {
    nonFinite = false;
    if (!entry.isObject())
        return false;

    std::string type;
    if (!requiredStringMember(entry, "type", type))
        return false;

    const auto value = entry["value"];
    if (value.isVoid())
        return false;

    if (type == "bool") {
        if (!value.isBool())
            return false;
        out = value.get<bool>();
        return true;
    }

    if (type == "i64") {
        std::int64_t integer = 0;
        if (!readInteger(value, integer))
            return false;
        out = integer;
        return true;
    }

    if (type == "f64") {
        if (!value.isFloat() && !value.isInt()) {
            if (value.isString()) {
                const auto text = value.getString();
                if (text == "Infinity" || text == "-Infinity" || text == "NaN")
                    nonFinite = true;
            }
            return false;
        }
        const auto number = value.get<double>();
        if (!std::isfinite(number)) {
            nonFinite = true;
            return false;
        }
        out = number;
        return true;
    }

    if (type == "string") {
        std::string text;
        if (!copyString(value, text))
            return false;
        out = std::move(text);
        return true;
    }

    return false;
}

[[nodiscard]] inline PresetCodecError parseExtensions(
    const choc::value::ValueView &metadata,
    std::vector<PresetExtensionField> &out) {
    const auto value = metadata["extensions"];
    if (value.isVoid()) {
        out.clear();
        return PresetCodecError::None;
    }
    if (!value.isArray() || value.size() > kMaxExtensions)
        return PresetCodecError::MalformedInput;

    std::vector<PresetExtensionField> candidate;
    candidate.reserve(value.size());
    std::unordered_set<std::string> keys;
    keys.reserve(value.size());

    for (std::uint32_t i = 0u; i < value.size(); ++i) {
        const auto entry = value[i];
        std::string key;
        if (!entry.isObject() || !requiredStringMember(entry, "key", key) || key.empty())
            return PresetCodecError::MalformedInput;
        if (!keys.insert(key).second)
            return PresetCodecError::InvalidDocument;

        PresetScalarValue scalar;
        bool nonFinite = false;
        if (!parseTaggedScalar(entry, scalar, nonFinite))
            return nonFinite ? PresetCodecError::InvalidDocument
                             : PresetCodecError::MalformedInput;
        candidate.push_back({std::move(key), std::move(scalar)});
    }

    out = std::move(candidate);
    return PresetCodecError::None;
}

[[nodiscard]] inline PresetCodecError parseV1Metadata(
    const choc::value::ValueView &root,
    PresetMetadata &metadata) {
    if (!root.isObject())
        return PresetCodecError::MalformedInput;

    PresetMetadata candidate;
    if (!requiredStringMember(root, "targetPluginId", candidate.targetPluginId) ||
        candidate.targetPluginId.empty() ||
        !requiredStringMember(root, "name", candidate.name) ||
        candidate.name.empty() ||
        !optionalStringMember(root, "creator", candidate.creator) ||
        !optionalStringMember(root, "description", candidate.description) ||
        !optionalStringArrayMember(root, "tags", candidate.tags) ||
        !optionalStringArrayMember(root, "features", candidate.features) ||
        !optionalIntegerMember(root, "creationTimestamp", candidate.creationTimestamp) ||
        !optionalIntegerMember(root, "modificationTimestamp", candidate.modificationTimestamp))
        return PresetCodecError::MalformedInput;

    const auto loadKey = root["factoryLoadKey"];
    if (!loadKey.isVoid()) {
        std::string key;
        if (!copyString(loadKey, key) || key.empty())
            return PresetCodecError::MalformedInput;
        candidate.factoryLoadKey = std::move(key);
    }

    const auto extensionError = parseExtensions(root, candidate.extensions);
    if (extensionError != PresetCodecError::None)
        return extensionError;

    metadata = std::move(candidate);
    return PresetCodecError::None;
}

[[nodiscard]] inline PresetCodecError parseV0Metadata(
    const choc::value::ValueView &root,
    PresetMetadata &metadata) {
    if (!root.isObject())
        return PresetCodecError::MigrationFailed;

    PresetMetadata candidate;
    if (!requiredStringMember(root, "pluginId", candidate.targetPluginId) ||
        candidate.targetPluginId.empty() ||
        !requiredStringMember(root, "name", candidate.name) ||
        candidate.name.empty())
        return PresetCodecError::MigrationFailed;

    if (!optionalStringMember(root, "author", candidate.creator) ||
        !optionalStringMember(root, "description", candidate.description) ||
        !optionalStringArrayMember(root, "tags", candidate.tags) ||
        !optionalStringArrayMember(root, "features", candidate.features))
        return PresetCodecError::MigrationFailed;

    const auto loadKey = root["factoryLoadKey"];
    if (!loadKey.isVoid()) {
        std::string key;
        if (!copyString(loadKey, key) || key.empty())
            return PresetCodecError::MigrationFailed;
        candidate.factoryLoadKey = std::move(key);
    }

    metadata = std::move(candidate);
    return PresetCodecError::None;
}

[[nodiscard]] inline PresetCodecError readSchemaVersion(
    const choc::value::ValueView &root,
    std::uint32_t &schemaVersion) {
    if (!root.isObject())
        return PresetCodecError::MalformedInput;

    std::int64_t version = 0;
    if (!readInteger(root["schemaVersion"], version) || version < 0)
        return PresetCodecError::MalformedInput;
    if (version > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
        return PresetCodecError::UnsupportedSchemaVersion;

    schemaVersion = static_cast<std::uint32_t>(version);
    if (schemaVersion > kCurrentPresetSchemaVersion)
        return PresetCodecError::UnsupportedSchemaVersion;
    return PresetCodecError::None;
}

[[nodiscard]] inline PresetCodecError parseParameters(
    const choc::value::ValueView &payload,
    std::vector<PresetParameterValue> &out) {
    const auto parameters = payload["parameters"];
    if (parameters.isVoid()) {
        out.clear();
        return PresetCodecError::None;
    }
    if (!parameters.isArray() || parameters.size() > kMaxParameters)
        return PresetCodecError::InvalidParameter;

    std::vector<PresetParameterValue> candidate;
    candidate.reserve(parameters.size());
    std::unordered_set<StableParameterId> ids;
    ids.reserve(parameters.size());

    for (std::uint32_t i = 0u; i < parameters.size(); ++i) {
        const auto entry = parameters[i];
        if (!entry.isObject())
            return PresetCodecError::InvalidParameter;

        std::int64_t id = 0;
        if (!readInteger(entry["id"], id) || id < 0 ||
            id > static_cast<std::int64_t>(std::numeric_limits<StableParameterId>::max()))
            return PresetCodecError::InvalidParameter;

        const auto stableId = static_cast<StableParameterId>(id);
        if (!ids.insert(stableId).second)
            return PresetCodecError::DuplicateParameterId;

        const auto value = entry["value"];
        if (value.isVoid())
            return PresetCodecError::InvalidParameter;

        if (value.isString()) {
            const auto text = value.getString();
            if (text == "Infinity" || text == "-Infinity" || text == "NaN")
                return PresetCodecError::NonFiniteParameterValue;
            return PresetCodecError::InvalidParameter;
        }
        if (!value.isFloat() && !value.isInt())
            return PresetCodecError::InvalidParameter;

        const auto number = value.get<double>();
        if (!std::isfinite(number))
            return PresetCodecError::NonFiniteParameterValue;
        candidate.push_back({stableId, number});
    }

    out = std::move(candidate);
    return PresetCodecError::None;
}

[[nodiscard]] inline PresetCodecError parseSettings(
    const choc::value::ValueView &payload,
    std::vector<PersistentSetting> &out) {
    const auto settings = payload["settings"];
    if (settings.isVoid()) {
        out.clear();
        return PresetCodecError::None;
    }
    if (!settings.isArray() || settings.size() > kMaxSettings)
        return PresetCodecError::InvalidSetting;

    std::vector<PersistentSetting> candidate;
    candidate.reserve(settings.size());
    std::unordered_set<std::string> keys;
    keys.reserve(settings.size());

    for (std::uint32_t i = 0u; i < settings.size(); ++i) {
        const auto entry = settings[i];
        std::string key;
        if (!entry.isObject() || !requiredStringMember(entry, "key", key) || key.empty())
            return PresetCodecError::InvalidSetting;
        if (!keys.insert(key).second)
            return PresetCodecError::InvalidSetting;

        PresetScalarValue scalar;
        bool nonFinite = false;
        if (!parseTaggedScalar(entry, scalar, nonFinite))
            return PresetCodecError::InvalidSetting;
        candidate.push_back({std::move(key), std::move(scalar)});
    }

    out = std::move(candidate);
    return PresetCodecError::None;
}

inline std::string serializeMetadata(const PresetDocument &document) {
    std::string out;
    out.reserve(512u);
    out += "{\"schemaVersion\":";
    out += std::to_string(document.schemaVersion);
    out += ",\"targetPluginId\":";
    appendJsonString(out, document.metadata.targetPluginId);
    out += ",\"name\":";
    appendJsonString(out, document.metadata.name);
    out += ",\"creator\":";
    appendJsonString(out, document.metadata.creator);
    out += ",\"description\":";
    appendJsonString(out, document.metadata.description);
    out += ",\"tags\":";
    appendStringArray(out, document.metadata.tags);
    out += ",\"features\":";
    appendStringArray(out, document.metadata.features);

    if (document.metadata.factoryLoadKey) {
        out += ",\"factoryLoadKey\":";
        appendJsonString(out, *document.metadata.factoryLoadKey);
    }
    if (document.metadata.creationTimestamp) {
        out += ",\"creationTimestamp\":";
        out += std::to_string(*document.metadata.creationTimestamp);
    }
    if (document.metadata.modificationTimestamp) {
        out += ",\"modificationTimestamp\":";
        out += std::to_string(*document.metadata.modificationTimestamp);
    }

    auto extensions = document.metadata.extensions;
    std::sort(extensions.begin(), extensions.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    out += ",\"extensions\":[";
    for (std::size_t i = 0u; i < extensions.size(); ++i) {
        if (i != 0u)
            out.push_back(',');
        appendTaggedScalarEntry(out, extensions[i].key, extensions[i].value);
    }
    out += "]}";
    return out;
}

inline std::string serializePayload(const PresetDocument &document) {
    auto parameters = document.parameters;
    std::sort(parameters.begin(), parameters.end(),
              [](const auto &a, const auto &b) {
                  return a.stableParameterId < b.stableParameterId;
              });
    auto settings = document.settings;
    std::sort(settings.begin(), settings.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });

    std::string out;
    out.reserve(512u + parameters.size() * 40u + settings.size() * 64u);
    out += "{\"parameters\":[";
    for (std::size_t i = 0u; i < parameters.size(); ++i) {
        if (i != 0u)
            out.push_back(',');
        out += "{\"id\":";
        out += std::to_string(parameters[i].stableParameterId);
        out += ",\"value\":";
        out += choc::json::doubleToString(parameters[i].value);
        out.push_back('}');
    }

    out += "],\"settings\":[";
    for (std::size_t i = 0u; i < settings.size(); ++i) {
        if (i != 0u)
            out.push_back(',');
        appendTaggedScalarEntry(out, settings[i].key, settings[i].value);
    }
    out += "]}";
    return out;
}

} // namespace detail

[[nodiscard]] inline PresetSerializeResult serializePresetDocument(
    const PresetDocument &document) {
    const auto validation = validatePresetDocument(document);
    if (!validation.ok())
        return {detail::mapValidationError(validation.error), {}};

    if (document.parameters.size() > detail::kMaxParameters ||
        document.settings.size() > detail::kMaxSettings ||
        document.metadata.extensions.size() > detail::kMaxExtensions ||
        document.metadata.tags.size() > detail::kMaxStringListEntries ||
        document.metadata.features.size() > detail::kMaxStringListEntries)
        return {PresetCodecError::InputTooLarge, {}};

    if (const auto textError = detail::documentTextCodecError(document);
        textError != PresetCodecError::None)
        return {textError, {}};

    const auto metadata = detail::serializeMetadata(document);
    const auto payload = detail::serializePayload(document);
    if (metadata.size() > detail::kMaxMetadataBytes ||
        detail::kPresetMagic.size() + metadata.size() + payload.size() + 2u >
            detail::kMaxPresetBytes)
        return {PresetCodecError::InputTooLarge, {}};

    PresetSerializeResult result;
    result.bytes.reserve(detail::kPresetMagic.size() + metadata.size() + payload.size() + 2u);
    result.bytes.append(detail::kPresetMagic.data(), detail::kPresetMagic.size());
    result.bytes += metadata;
    result.bytes.push_back('\n');
    result.bytes += payload;
    result.bytes.push_back('\n');
    return result;
}

[[nodiscard]] inline PresetMetadataParseResult parsePresetMetadata(
    std::string_view bytes,
    std::string_view expectedTargetPluginId = {}) {
    detail::PresetFrame frame;
    const auto frameError = detail::splitFrame(bytes, frame);
    if (frameError != PresetCodecError::None)
        return {frameError, 0u, {}};

    if (!detail::jsonNestingWithinLimit(frame.metadataJson))
        return {PresetCodecError::NestingTooDeep, 0u, {}};

    try {
        const auto rootHolder = choc::json::parse(frame.metadataJson);
        const auto root = rootHolder.getView();

        std::uint32_t sourceSchemaVersion = 0u;
        const auto schemaError = detail::readSchemaVersion(root, sourceSchemaVersion);
        if (schemaError != PresetCodecError::None)
            return {schemaError, 0u, {}};

        PresetMetadata metadata;
        PresetCodecError metadataError = PresetCodecError::None;
        if (sourceSchemaVersion == 0u)
            metadataError = detail::parseV0Metadata(root, metadata);
        else if (sourceSchemaVersion == kCurrentPresetSchemaVersion)
            metadataError = detail::parseV1Metadata(root, metadata);
        else
            return {PresetCodecError::UnsupportedSchemaVersion, 0u, {}};

        if (metadataError != PresetCodecError::None)
            return {metadataError, 0u, {}};

        if (!expectedTargetPluginId.empty() &&
            metadata.targetPluginId != expectedTargetPluginId)
            return {PresetCodecError::WrongTargetPlugin, 0u, {}};

        PresetDocument metadataCandidate;
        metadataCandidate.schemaVersion = kCurrentPresetSchemaVersion;
        metadataCandidate.metadata = metadata;
        const auto validation = validatePresetDocument(metadataCandidate);
        if (!validation.ok())
            return {sourceSchemaVersion == 0u ? PresetCodecError::MigrationFailed
                                             : detail::mapValidationError(validation.error),
                    0u,
                    {}};

        return {PresetCodecError::None,
                kCurrentPresetSchemaVersion,
                std::move(metadata)};
    } catch (const choc::json::ParseError &) {
        return {PresetCodecError::MalformedInput, 0u, {}};
    } catch (const choc::value::Error &) {
        return {PresetCodecError::MalformedInput, 0u, {}};
    }
}

[[nodiscard]] inline PresetParseResult parsePresetDocument(
    std::string_view bytes,
    std::string_view expectedTargetPluginId = {}) {
    detail::PresetFrame frame;
    const auto frameError = detail::splitFrame(bytes, frame);
    if (frameError != PresetCodecError::None)
        return {frameError, std::nullopt};

    if (!detail::jsonNestingWithinLimit(frame.metadataJson) ||
        !detail::jsonNestingWithinLimit(frame.payloadJson))
        return {PresetCodecError::NestingTooDeep, std::nullopt};

    try {
        const auto metadataRootHolder = choc::json::parse(frame.metadataJson);
        const auto metadataRoot = metadataRootHolder.getView();

        std::uint32_t sourceSchemaVersion = 0u;
        const auto schemaError = detail::readSchemaVersion(metadataRoot, sourceSchemaVersion);
        if (schemaError != PresetCodecError::None)
            return {schemaError, std::nullopt};

        PresetDocument candidate;
        candidate.schemaVersion = kCurrentPresetSchemaVersion;

        PresetCodecError metadataError = PresetCodecError::None;
        if (sourceSchemaVersion == 0u)
            metadataError = detail::parseV0Metadata(metadataRoot, candidate.metadata);
        else if (sourceSchemaVersion == kCurrentPresetSchemaVersion)
            metadataError = detail::parseV1Metadata(metadataRoot, candidate.metadata);
        else
            return {PresetCodecError::UnsupportedSchemaVersion, std::nullopt};

        if (metadataError != PresetCodecError::None)
            return {metadataError, std::nullopt};

        if (!expectedTargetPluginId.empty() &&
            candidate.metadata.targetPluginId != expectedTargetPluginId)
            return {PresetCodecError::WrongTargetPlugin, std::nullopt};

        const auto payloadRootHolder = choc::json::parse(frame.payloadJson);
        const auto payloadRoot = payloadRootHolder.getView();
        if (!payloadRoot.isObject())
            return {sourceSchemaVersion == 0u ? PresetCodecError::MigrationFailed
                                             : PresetCodecError::MalformedInput,
                    std::nullopt};

        const auto parameterError = detail::parseParameters(payloadRoot, candidate.parameters);
        if (parameterError != PresetCodecError::None)
            return {sourceSchemaVersion == 0u &&
                            parameterError != PresetCodecError::DuplicateParameterId &&
                            parameterError != PresetCodecError::NonFiniteParameterValue
                        ? PresetCodecError::MigrationFailed
                        : parameterError,
                    std::nullopt};

        const auto settingError = detail::parseSettings(payloadRoot, candidate.settings);
        if (settingError != PresetCodecError::None)
            return {sourceSchemaVersion == 0u ? PresetCodecError::MigrationFailed
                                             : settingError,
                    std::nullopt};

        const auto validation = validatePresetDocument(candidate);
        if (!validation.ok())
            return {sourceSchemaVersion == 0u ? PresetCodecError::MigrationFailed
                                             : detail::mapValidationError(validation.error),
                    std::nullopt};

        return {PresetCodecError::None, std::move(candidate)};
    } catch (const choc::json::ParseError &) {
        return {PresetCodecError::MalformedInput, std::nullopt};
    } catch (const choc::value::Error &) {
        return {PresetCodecError::MalformedInput, std::nullopt};
    }
}

} // namespace webview_gui::examples::presets
