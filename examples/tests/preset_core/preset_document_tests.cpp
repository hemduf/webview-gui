#include "preset_document.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace presets = webview_gui::examples::presets;

namespace {

template <typename T, typename = void>
struct has_modulation_member : std::false_type {};
template <typename T>
struct has_modulation_member<T, std::void_t<decltype(std::declval<T>().modulation)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_note_expression_member : std::false_type {};
template <typename T>
struct has_note_expression_member<T, std::void_t<decltype(std::declval<T>().noteExpression)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_active_voices_member : std::false_type {};
template <typename T>
struct has_active_voices_member<T, std::void_t<decltype(std::declval<T>().activeVoices)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_telemetry_member : std::false_type {};
template <typename T>
struct has_telemetry_member<T, std::void_t<decltype(std::declval<T>().telemetry)>>
    : std::true_type {};

presets::PresetDocument makeGainPreset() {
    presets::PresetDocument document;
    document.schemaVersion = presets::kCurrentPresetSchemaVersion;
    document.metadata.targetPluginId = "com.webview-gui.example.gain";
    document.metadata.name = "Unity";
    document.metadata.creator = "webview-gui";
    document.metadata.description = "Reference unity-gain preset";
    document.metadata.features = {"utility", "factory"};
    document.metadata.factoryLoadKey = "gain:unity";
    document.parameters = {
        {0x1000u, 0.0},
        {0x1001u, 0.0},
    };
    document.settings.push_back({"reference", std::string{"gain"}});
    return document;
}

presets::PresetDocument makePolySynthPreset() {
    presets::PresetDocument document;
    document.schemaVersion = presets::kCurrentPresetSchemaVersion;
    document.metadata.targetPluginId = "com.webview-gui.example.polysynth";
    document.metadata.name = "Init";
    document.metadata.creator = "webview-gui";
    document.metadata.description = "Reference PolySynth init preset";
    document.metadata.features = {"instrument", "synthesizer", "factory"};
    document.metadata.factoryLoadKey = "polysynth:init";

    for (std::uint32_t id = 1000u; id <= 1012u; ++id)
        document.parameters.push_back({id, 0.0});

    return document;
}

} // namespace

int main() {
    static_assert(presets::kCurrentPresetSchemaVersion == 1u,
                  "#99 freezes the first published preset schema as v1");
    static_assert(!has_modulation_member<presets::PresetDocument>::value,
                  "ephemeral host modulation must not have a public preset slot");
    static_assert(!has_note_expression_member<presets::PresetDocument>::value,
                  "note-expression runtime state must not have a public preset slot");
    static_assert(!has_active_voices_member<presets::PresetDocument>::value,
                  "active voice state must not have a public preset slot");
    static_assert(!has_telemetry_member<presets::PresetDocument>::value,
                  "telemetry must not have a public preset slot");

    auto gain = makeGainPreset();
    auto gainValidation = presets::validatePresetDocument(gain);
    assert(gainValidation.ok());
    assert(gainValidation.error == presets::PresetValidationError::None);

    const auto gainMetadata = presets::metadataView(gain);
    assert(gainMetadata.schemaVersion == 1u);
    assert(gainMetadata.targetPluginId == "com.webview-gui.example.gain");
    assert(gainMetadata.name == "Unity");
    assert(gainMetadata.creator == "webview-gui");
    assert(gainMetadata.factoryLoadKey == "gain:unity");

    auto poly = makePolySynthPreset();
    const auto polyValidation = presets::validatePresetDocument(poly);
    assert(polyValidation.ok());
    assert(poly.parameters.size() == 13u);
    for (std::uint32_t index = 0; index < 13u; ++index)
        assert(poly.parameters[index].stableParameterId == 1000u + index);

    auto duplicate = gain;
    duplicate.parameters.push_back({0x1000u, -6.0});
    assert(presets::validatePresetDocument(duplicate).error ==
           presets::PresetValidationError::DuplicateParameterId);

    auto emptyTarget = gain;
    emptyTarget.metadata.targetPluginId.clear();
    assert(presets::validatePresetDocument(emptyTarget).error ==
           presets::PresetValidationError::MissingTargetPluginId);

    auto zeroSchema = gain;
    zeroSchema.schemaVersion = 0u;
    assert(presets::validatePresetDocument(zeroSchema).error ==
           presets::PresetValidationError::UnsupportedSchemaVersion);

    auto futureSchema = gain;
    futureSchema.schemaVersion = presets::kCurrentPresetSchemaVersion + 1u;
    assert(presets::validatePresetDocument(futureSchema).error ==
           presets::PresetValidationError::UnsupportedSchemaVersion);

    auto nanValue = gain;
    nanValue.parameters[0].value = std::numeric_limits<double>::quiet_NaN();
    assert(presets::validatePresetDocument(nanValue).error ==
           presets::PresetValidationError::NonFiniteParameterValue);

    auto infiniteValue = gain;
    infiniteValue.parameters[0].value = std::numeric_limits<double>::infinity();
    assert(presets::validatePresetDocument(infiniteValue).error ==
           presets::PresetValidationError::NonFiniteParameterValue);

    auto duplicateSetting = gain;
    duplicateSetting.settings.push_back({"reference", true});
    assert(presets::validatePresetDocument(duplicateSetting).error ==
           presets::PresetValidationError::DuplicateSettingKey);

    return 0;
}
