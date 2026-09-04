#include "gain_preset_state.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace gain = webview_gui::examples::gain;
namespace presets = webview_gui::examples::presets;

namespace {

template <typename T, typename = void>
struct has_events_member : std::false_type {};
template <typename T>
struct has_events_member<T, std::void_t<decltype(std::declval<T>().events)>>
    : std::true_type {};

bool approximately(double actual, double expected) noexcept {
    return std::fabs(actual - expected) <= 1.0e-6;
}

} // namespace

int main() {
    static_assert(!has_events_member<gain::GainParameterSnapshot>::value,
                  "persistent preset state must not contain automation/event output");

    gain::GainParameterSnapshot source{};
    source.gainDb = -12.0f;
    source.bypassed = true;

    presets::PresetMetadata metadata;
    metadata.name = "Captured Gain";
    metadata.creator = "webview-gui";
    metadata.description = "Gain persistent-state round trip";

    const auto captured = gain::captureGainPreset(source, metadata);
    assert(captured.schemaVersion == presets::kCurrentPresetSchemaVersion);
    assert(captured.metadata.targetPluginId ==
           webview_gui::examples::plugin_ids::kGainPluginId);
    assert(captured.metadata.name == "Captured Gain");
    assert(captured.parameters.size() == 2u);
    assert(captured.parameters[0].stableParameterId == gain::kGainParamId);
    assert(captured.parameters[1].stableParameterId == gain::kBypassParamId);
    assert(approximately(captured.parameters[0].value, -12.0));
    assert(captured.parameters[1].value == 1.0);
    assert(captured.settings.empty());

    const auto roundTrip = gain::makeGainPresetCandidate(captured);
    assert(roundTrip.ok());
    assert(roundTrip.candidate.has_value());
    assert(approximately(roundTrip.candidate->gainDb, -12.0));
    assert(roundTrip.candidate->bypassed);

    // Stable IDs, not host/index order, define the mapping.
    auto reordered = captured;
    std::swap(reordered.parameters[0], reordered.parameters[1]);
    const auto reorderedResult = gain::makeGainPresetCandidate(reordered);
    assert(reorderedResult.ok());
    assert(approximately(reorderedResult.candidate->gainDb, -12.0));
    assert(reorderedResult.candidate->bypassed);

    // Removed/unknown stable IDs are ignored and cannot corrupt known values.
    auto withUnknown = reordered;
    withUnknown.parameters.push_back({0xdeadbeefu, 1234.0});
    const auto unknownResult = gain::makeGainPresetCandidate(withUnknown);
    assert(unknownResult.ok());
    assert(approximately(unknownResult.candidate->gainDb, -12.0));
    assert(unknownResult.candidate->bypassed);

    // Missing historical fields migrate deterministically from published defaults,
    // never from the instance's previous state.
    auto legacySparse = captured;
    legacySparse.parameters = {{gain::kGainParamId, -3.0}};
    const auto sparseResult = gain::makeGainPresetCandidate(legacySparse);
    assert(sparseResult.ok());
    assert(approximately(sparseResult.candidate->gainDb, -3.0));
    assert(!sparseResult.candidate->bypassed);

    // Wrong target and one invalid known value reject the whole candidate.
    gain::GainParameterSnapshot previouslyVisible{};
    previouslyVisible.gainDb = -24.0f;
    previouslyVisible.bypassed = true;

    auto wrongTarget = captured;
    wrongTarget.metadata.targetPluginId =
        webview_gui::examples::plugin_ids::kPolySynthPluginId;
    const auto wrongTargetResult = gain::makeGainPresetCandidate(wrongTarget);
    assert(!wrongTargetResult.ok());
    assert(!wrongTargetResult.candidate.has_value());
    assert(wrongTargetResult.error == presets::PresetStateAdapterError::WrongTargetPlugin);
    assert(approximately(previouslyVisible.gainDb, -24.0));
    assert(previouslyVisible.bypassed);

    auto invalidGain = captured;
    invalidGain.parameters[0].value = 13.0;
    const auto invalidGainResult = gain::makeGainPresetCandidate(invalidGain);
    assert(!invalidGainResult.ok());
    assert(!invalidGainResult.candidate.has_value());
    assert(invalidGainResult.error ==
           presets::PresetStateAdapterError::InvalidKnownParameter);
    assert(approximately(previouslyVisible.gainDb, -24.0));
    assert(previouslyVisible.bypassed);

    auto invalidBypass = captured;
    invalidBypass.parameters[1].value = 0.5;
    const auto invalidBypassResult = gain::makeGainPresetCandidate(invalidBypass);
    assert(!invalidBypassResult.ok());
    assert(!invalidBypassResult.candidate.has_value());
    assert(invalidBypassResult.error ==
           presets::PresetStateAdapterError::InvalidKnownParameter);

    return 0;
}
