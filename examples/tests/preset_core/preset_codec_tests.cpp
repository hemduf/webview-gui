#include "preset_codec.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

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
    };
    return document;
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

    for (std::size_t i = 0; i < a.parameters.size(); ++i) {
        if (a.parameters[i].stableParameterId != b.parameters[i].stableParameterId ||
            a.parameters[i].value != b.parameters[i].value)
            return false;
    }

    for (std::size_t i = 0; i < a.settings.size(); ++i) {
        if (a.settings[i].key != b.settings[i].key ||
            !sameScalar(a.settings[i].value, b.settings[i].value))
            return false;
    }

    for (std::size_t i = 0; i < a.metadata.extensions.size(); ++i) {
        if (a.metadata.extensions[i].key != b.metadata.extensions[i].key ||
            !sameScalar(a.metadata.extensions[i].value,
                        b.metadata.extensions[i].value))
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

    // Metadata-only parsing is an owning result and does not require a
    // processor/WebView instance or parameter payload materialization.
    const auto metadata = presets::parsePresetMetadata(encoded.bytes);
    assert(metadata.ok());
    assert(metadata.schemaVersion == presets::kCurrentPresetSchemaVersion);
    assert(metadata.metadata.targetPluginId == source.metadata.targetPluginId);
    assert(metadata.metadata.name == source.metadata.name);
    assert(metadata.metadata.creator == source.metadata.creator);
    assert(metadata.metadata.description == source.metadata.description);
    assert(metadata.metadata.tags == source.metadata.tags);
    assert(metadata.metadata.factoryLoadKey == source.metadata.factoryLoadKey);

    // Expected-target checking is part of parse, never a later live-state
    // mutation step.
    const auto wrongTarget = presets::parsePresetDocument(
        encoded.bytes, "com.webview-gui.example.gain");
    assert(!wrongTarget.ok());
    assert(!wrongTarget.document.has_value());
    assert(wrongTarget.error == presets::PresetCodecError::WrongTargetPlugin);

    // Malformed input and truncation are explicit and never return partial
    // candidates.
    const auto malformed = presets::parsePresetDocument("not-a-wvpreset");
    assert(!malformed.ok());
    assert(!malformed.document.has_value());
    assert(malformed.error == presets::PresetCodecError::MalformedInput);

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

    // Validation errors are surfaced as codec errors, with no partial output.
    auto duplicate = source;
    duplicate.parameters.push_back(duplicate.parameters.front());
    const auto duplicateEncoded = presets::serializePresetDocument(duplicate);
    assert(!duplicateEncoded.ok());
    assert(duplicateEncoded.bytes.empty());
    assert(duplicateEncoded.error == presets::PresetCodecError::DuplicateParameterId);

    auto nonFinite = source;
    nonFinite.parameters.front().value =
        std::numeric_limits<double>::infinity();
    const auto nonFiniteEncoded = presets::serializePresetDocument(nonFinite);
    assert(!nonFiniteEncoded.ok());
    assert(nonFiniteEncoded.bytes.empty());
    assert(nonFiniteEncoded.error == presets::PresetCodecError::NonFiniteParameterValue);

    return 0;
}
