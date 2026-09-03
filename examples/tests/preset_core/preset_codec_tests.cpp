#include "preset_codec.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace presets = webview_gui::examples::presets;

namespace {

presets::PresetDocument makeDocument() {
    presets::PresetDocument document;
    document.schemaVersion = presets::kCurrentPresetSchemaVersion;
    document.metadata.targetPluginId = "com.webview-gui.example.polysynth";
    document.metadata.name = "Codec Round Trip";
    document.metadata.creator = "webview-gui";
    document.metadata.description = "Deterministic codec fixture";
    document.metadata.tags = {"factory", "test"};
    document.metadata.features = {"instrument", "synthesizer"};
    document.metadata.factoryLoadKey = "polysynth:codec-roundtrip";
    document.metadata.creationTimestamp = 100;
    document.metadata.modificationTimestamp = 200;
    document.metadata.extensions = {
        {"future-authoring-field", std::int64_t{42}},
        {"future-label", std::string{"preserve-me"}},
    };
    document.parameters = {
        {1000u, 0.125},
        {1001u, 0.5},
        {424242u, 0.75}, // deliberately unknown/removed to current adapters
    };
    document.settings = {
        {"oversampling", std::int64_t{2}},
        {"legacy-mode", false},
        {"drive", 0.25},
    };
    return document;
}

std::string wire(std::string_view metadataJson,
                 std::string_view payloadJson) {
    std::string result = "WVPRESET\n";
    result.append(metadataJson.data(), metadataJson.size());
    result.push_back('\n');
    result.append(payloadJson.data(), payloadJson.size());
    result.push_back('\n');
    return result;
}

bool sameScalar(const presets::PresetScalarValue &a,
                const presets::PresetScalarValue &b) {
    return a == b;
}

bool sameDocument(const presets::PresetDocument &a,
                  const presets::PresetDocument &b) {
    if (a.schemaVersion != b.schemaVersion ||
        a.metadata.targetPluginId != b.metadata.targetPluginId ||
        a.metadata.name != b.metadata.name ||
        a.metadata.creator != b.metadata.creator ||
        a.metadata.description != b.metadata.description ||
        a.metadata.tags != b.metadata.tags ||
        a.metadata.features != b.metadata.features ||
        a.metadata.factoryLoadKey != b.metadata.factoryLoadKey ||
        a.metadata.creationTimestamp != b.metadata.creationTimestamp ||
        a.metadata.modificationTimestamp != b.metadata.modificationTimestamp ||
        a.parameters.size() != b.parameters.size() ||
        a.settings.size() != b.settings.size() ||
        a.metadata.extensions.size() != b.metadata.extensions.size())
        return false;

    auto aParameters = a.parameters;
    auto bParameters = b.parameters;
    const auto parameterLess = [](const auto &lhs, const auto &rhs) {
        return lhs.stableParameterId < rhs.stableParameterId;
    };
    std::sort(aParameters.begin(), aParameters.end(), parameterLess);
    std::sort(bParameters.begin(), bParameters.end(), parameterLess);
    for (std::size_t i = 0; i < aParameters.size(); ++i) {
        if (aParameters[i].stableParameterId != bParameters[i].stableParameterId ||
            aParameters[i].value != bParameters[i].value)
            return false;
    }

    auto aSettings = a.settings;
    auto bSettings = b.settings;
    const auto keyLess = [](const auto &lhs, const auto &rhs) {
        return lhs.key < rhs.key;
    };
    std::sort(aSettings.begin(), aSettings.end(), keyLess);
    std::sort(bSettings.begin(), bSettings.end(), keyLess);
    for (std::size_t i = 0; i < aSettings.size(); ++i) {
        if (aSettings[i].key != bSettings[i].key ||
            !sameScalar(aSettings[i].value, bSettings[i].value))
            return false;
    }

    auto aExtensions = a.metadata.extensions;
    auto bExtensions = b.metadata.extensions;
    std::sort(aExtensions.begin(), aExtensions.end(), keyLess);
    std::sort(bExtensions.begin(), bExtensions.end(), keyLess);
    for (std::size_t i = 0; i < aExtensions.size(); ++i) {
        if (aExtensions[i].key != bExtensions[i].key ||
            !sameScalar(aExtensions[i].value, bExtensions[i].value))
            return false;
    }

    return true;
}

} // namespace

int main() {
    const auto source = makeDocument();

    // Full v1 round-trip, including metadata, persistent settings and an
    // unknown/removed stable parameter ID that must remain representable.
    const auto encoded = presets::serializePresetDocument(source);
    assert(encoded.ok());
    assert(!encoded.bytes.empty());
    assert(encoded.bytes.rfind("WVPRESET\n", 0u) == 0u);
    assert(encoded.bytes.back() == '\n');

    const auto parsed = presets::parsePresetDocument(encoded.bytes);
    assert(parsed.ok());
    assert(parsed.document.has_value());
    assert(sameDocument(source, *parsed.document));

    // Equivalent key/value documents serialize byte-for-byte identically even
    // if map-like vectors arrive in a different order.
    auto reordered = source;
    std::reverse(reordered.parameters.begin(), reordered.parameters.end());
    std::reverse(reordered.settings.begin(), reordered.settings.end());
    std::reverse(reordered.metadata.extensions.begin(),
                 reordered.metadata.extensions.end());
    const auto encodedReordered = presets::serializePresetDocument(reordered);
    assert(encodedReordered.ok());
    assert(encoded.bytes == encodedReordered.bytes);

    // Metadata-only parsing is an owning result and does not decode the payload.
    const auto metadata = presets::parsePresetMetadata(encoded.bytes);
    assert(metadata.ok());
    assert(metadata.schemaVersion == presets::kCurrentPresetSchemaVersion);
    assert(metadata.metadata.targetPluginId == source.metadata.targetPluginId);
    assert(metadata.metadata.name == source.metadata.name);
    assert(metadata.metadata.creator == source.metadata.creator);
    assert(metadata.metadata.description == source.metadata.description);
    assert(metadata.metadata.tags == source.metadata.tags);
    assert(metadata.metadata.factoryLoadKey == source.metadata.factoryLoadKey);

    const auto metadataOnlyFixture = wire(
        R"({"schemaVersion":1,"targetPluginId":"com.webview-gui.example.gain","name":"Fast Metadata","creator":"webview-gui","description":"metadata path","tags":["factory"],"features":["utility"],"factoryLoadKey":"gain:fast","extensions":[]})",
        "this payload is deliberately not JSON");
    const auto fastMetadata = presets::parsePresetMetadata(metadataOnlyFixture);
    assert(fastMetadata.ok());
    assert(fastMetadata.metadata.name == "Fast Metadata");
    const auto invalidPayloadFullParse = presets::parsePresetDocument(metadataOnlyFixture);
    assert(!invalidPayloadFullParse.ok());
    assert(!invalidPayloadFullParse.document.has_value());
    assert(invalidPayloadFullParse.error == presets::PresetCodecError::MalformedInput);

    // Expected-target checking is part of parse, never a later live-state mutation.
    const auto wrongTarget = presets::parsePresetDocument(
        encoded.bytes, "com.webview-gui.example.gain");
    assert(!wrongTarget.ok());
    assert(!wrongTarget.document.has_value());
    assert(wrongTarget.error == presets::PresetCodecError::WrongTargetPlugin);

    // Malformed input and truncation are explicit and never return partial candidates.
    const auto malformed = presets::parsePresetDocument("not-a-wvpreset");
    assert(!malformed.ok());
    assert(!malformed.document.has_value());
    assert(malformed.error == presets::PresetCodecError::MalformedInput);

    const auto malformedJson = presets::parsePresetDocument(
        wire("{broken-json}", R"({"parameters":[],"settings":[]})"));
    assert(!malformedJson.ok());
    assert(!malformedJson.document.has_value());
    assert(malformedJson.error == presets::PresetCodecError::MalformedInput);

    const std::size_t cuts[] = {
        0u,
        1u,
        encoded.bytes.size() / 2u,
        encoded.bytes.size() - 1u,
    };
    for (const auto cut : cuts) {
        const auto truncated = presets::parsePresetDocument(
            std::string_view{encoded.bytes}.substr(0u, cut));
        assert(!truncated.ok());
        assert(!truncated.document.has_value());
        assert(truncated.error == presets::PresetCodecError::TruncatedInput);
    }

    // Unknown optional JSON members are tolerated. Unknown stable parameter IDs
    // remain intact and do not corrupt neighboring values.
    const auto futureOptional = wire(
        R"({"schemaVersion":1,"targetPluginId":"com.webview-gui.example.polysynth","name":"Future Optional","creator":"webview-gui","description":"forward compatible","tags":[],"features":["instrument"],"factoryLoadKey":"polysynth:future","extensions":[],"futureOptional":{"anything":true}})",
        R"({"parameters":[{"id":1000,"value":0.25},{"id":900001,"value":0.75}],"settings":[],"futurePayloadField":[1,2,3]})");
    const auto futureOptionalParsed = presets::parsePresetDocument(futureOptional);
    assert(futureOptionalParsed.ok());
    assert(futureOptionalParsed.document.has_value());
    assert(futureOptionalParsed.document->parameters.size() == 2u);
    assert(futureOptionalParsed.document->parameters[1].stableParameterId == 900001u);
    assert(futureOptionalParsed.document->parameters[1].value == 0.75);

    // Future schema versions are rejected explicitly, not partially accepted.
    const auto futureSchema = wire(
        R"({"schemaVersion":99,"targetPluginId":"com.webview-gui.example.gain","name":"Future","creator":"webview-gui","description":"future","tags":[],"features":[],"extensions":[]})",
        R"({"parameters":[],"settings":[]})");
    const auto futureSchemaParsed = presets::parsePresetDocument(futureSchema);
    assert(!futureSchemaParsed.ok());
    assert(!futureSchemaParsed.document.has_value());
    assert(futureSchemaParsed.error == presets::PresetCodecError::UnsupportedSchemaVersion);

    // Duplicate and non-finite encoded parameter values are typed parse errors.
    const auto duplicateWire = wire(
        R"({"schemaVersion":1,"targetPluginId":"com.webview-gui.example.gain","name":"Duplicate","creator":"webview-gui","description":"duplicate","tags":[],"features":[],"extensions":[]})",
        R"({"parameters":[{"id":1000,"value":0.25},{"id":1000,"value":0.5}],"settings":[]})");
    const auto duplicateParsed = presets::parsePresetDocument(duplicateWire);
    assert(!duplicateParsed.ok());
    assert(!duplicateParsed.document.has_value());
    assert(duplicateParsed.error == presets::PresetCodecError::DuplicateParameterId);

    const auto nonFiniteWire = wire(
        R"({"schemaVersion":1,"targetPluginId":"com.webview-gui.example.gain","name":"Non finite","creator":"webview-gui","description":"nonfinite","tags":[],"features":[],"extensions":[]})",
        R"({"parameters":[{"id":1000,"value":"Infinity"}],"settings":[]})");
    const auto nonFiniteParsed = presets::parsePresetDocument(nonFiniteWire);
    assert(!nonFiniteParsed.ok());
    assert(!nonFiniteParsed.document.has_value());
    assert(nonFiniteParsed.error == presets::PresetCodecError::NonFiniteParameterValue);

    // Validation errors on serialization are typed and emit no bytes.
    auto duplicate = source;
    duplicate.parameters.push_back(duplicate.parameters.front());
    const auto duplicateEncoded = presets::serializePresetDocument(duplicate);
    assert(!duplicateEncoded.ok());
    assert(duplicateEncoded.bytes.empty());
    assert(duplicateEncoded.error == presets::PresetCodecError::DuplicateParameterId);

    auto nonFinite = source;
    nonFinite.parameters.front().value = std::numeric_limits<double>::infinity();
    const auto nonFiniteEncoded = presets::serializePresetDocument(nonFinite);
    assert(!nonFiniteEncoded.ok());
    assert(nonFiniteEncoded.bytes.empty());
    assert(nonFiniteEncoded.error == presets::PresetCodecError::NonFiniteParameterValue);

    // Serializer and parser must share string bounds: never emit a file that the
    // parser rejects merely because a persistent scalar string exceeds the limit.
    auto oversizedString = source;
    oversizedString.settings.push_back(
        {"oversized", std::string(64u * 1024u + 1u, 'x')});
    const auto oversizedEncoded = presets::serializePresetDocument(oversizedString);
    assert(!oversizedEncoded.ok());
    assert(oversizedEncoded.bytes.empty());
    assert(oversizedEncoded.error == presets::PresetCodecError::InputTooLarge);

    // Unknown fields are forward-compatible but their recursive JSON structure
    // is bounded before entering CHOC's recursive parser.
    std::string deeplyNestedMetadata =
        R"({"schemaVersion":1,"targetPluginId":"com.webview-gui.example.gain","name":"Deep","creator":"webview-gui","description":"depth guard","tags":[],"features":[],"extensions":[],"future":)";
    deeplyNestedMetadata.append(80u, '[');
    deeplyNestedMetadata += "0";
    deeplyNestedMetadata.append(80u, ']');
    deeplyNestedMetadata += "}";
    const auto deeplyNested = wire(
        deeplyNestedMetadata,
        R"({"parameters":[],"settings":[]})");
    const auto deepMetadataParsed = presets::parsePresetMetadata(deeplyNested);
    assert(!deepMetadataParsed.ok());
    assert(deepMetadataParsed.error == presets::PresetCodecError::NestingTooDeep);
    const auto deepFullParsed = presets::parsePresetDocument(deeplyNested);
    assert(!deepFullParsed.ok());
    assert(!deepFullParsed.document.has_value());
    assert(deepFullParsed.error == presets::PresetCodecError::NestingTooDeep);

    // Synthetic v0 maps legacy pluginId/author fields into the canonical v1 model.
    const auto legacyV0 = wire(
        R"({"schemaVersion":0,"pluginId":"com.webview-gui.example.polysynth","name":"Legacy Init","author":"Legacy Author","description":"legacy fixture","tags":["legacy"],"features":["instrument"],"factoryLoadKey":"polysynth:legacy-init"})",
        R"({"parameters":[{"id":1000,"value":0.25},{"id":424242,"value":0.5}],"settings":[]})");
    const auto migrated = presets::parsePresetDocument(legacyV0);
    assert(migrated.ok());
    assert(migrated.document.has_value());
    assert(migrated.document->schemaVersion == presets::kCurrentPresetSchemaVersion);
    assert(migrated.document->metadata.targetPluginId ==
           "com.webview-gui.example.polysynth");
    assert(migrated.document->metadata.creator == "Legacy Author");
    assert(migrated.document->metadata.name == "Legacy Init");
    assert(migrated.document->metadata.factoryLoadKey ==
           std::optional<std::string>{"polysynth:legacy-init"});
    assert(migrated.document->parameters.size() == 2u);
    assert(migrated.document->parameters[1].stableParameterId == 424242u);

    // Migration failure is atomic: no partially constructed v1 document escapes.
    const auto brokenLegacyV0 = wire(
        R"({"schemaVersion":0,"name":"Broken Legacy","author":"Legacy Author","description":"missing plugin id","tags":[],"features":[]})",
        R"({"parameters":[],"settings":[]})");
    const auto migrationFailure = presets::parsePresetDocument(brokenLegacyV0);
    assert(!migrationFailure.ok());
    assert(!migrationFailure.document.has_value());
    assert(migrationFailure.error == presets::PresetCodecError::MigrationFailed);

    return 0;
}
