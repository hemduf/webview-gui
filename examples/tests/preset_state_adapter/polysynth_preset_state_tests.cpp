#include "polysynth_preset_state.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace poly = webview_gui::examples::polysynth;
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
struct has_meter_member : std::false_type {};
template <typename T>
struct has_meter_member<T, std::void_t<decltype(std::declval<T>().meter)>>
    : std::true_type {};

bool approximately(double actual, double expected) noexcept {
    return std::fabs(actual - expected) <= 1.0e-5;
}

void verifySnapshot(const poly::ParameterSnapshot &snapshot,
                    const std::array<double, poly::kParameterCount> &expected) {
    for (std::size_t index = 0u; index < poly::kParameterCount; ++index) {
        double actual = 0.0;
        assert(poly::parameterSnapshotValue(
            snapshot,
            poly::kFirstParameterId + static_cast<clap_id>(index),
            actual));
        assert(approximately(actual, expected[index]));
    }
}

} // namespace

int main() {
    static_assert(!has_modulation_member<poly::ParameterSnapshot>::value,
                  "global/per-note modulation must not enter persistent snapshots");
    static_assert(!has_note_expression_member<poly::ParameterSnapshot>::value,
                  "note expression must not enter persistent snapshots");
    static_assert(!has_active_voices_member<poly::ParameterSnapshot>::value,
                  "active voice/envelope state must not enter persistent snapshots");
    static_assert(!has_meter_member<poly::ParameterSnapshot>::value,
                  "telemetry must not enter persistent snapshots");

    poly::ParameterSnapshot source = poly::defaultParameterSnapshot();
    source.masterGainDb = -9.0f;
    source.waveform = 2u;
    source.coarseTuneSemitones = -7;
    source.fineTuneCents = 17.5f;
    source.filterCutoffHz = 4321.0f;
    source.filterResonance = 0.4f;
    source.ampAttackSeconds = 0.15f;
    source.ampDecaySeconds = 0.3f;
    source.ampSustain = 0.55f;
    source.ampReleaseSeconds = 0.8f;
    source.filterEnvelopeAmount = -0.65f;
    source.pan = 0.25f;
    source.ampLevel = 0.7f;

    presets::PresetMetadata metadata;
    metadata.name = "Captured PolySynth";
    metadata.creator = "webview-gui";
    metadata.description = "PolySynth persistent-state round trip";

    const auto captured = poly::capturePolySynthPreset(source, metadata);
    assert(captured.schemaVersion == presets::kCurrentPresetSchemaVersion);
    assert(captured.metadata.targetPluginId ==
           webview_gui::examples::plugin_ids::kPolySynthPluginId);
    assert(captured.parameters.size() == poly::kParameterCount);
    assert(captured.settings.empty());

    // Capture order is stable-ID order, deliberately independent of the CLAP
    // host-index order used by the plug-in ABI.
    for (std::size_t index = 0u; index < captured.parameters.size(); ++index)
        assert(captured.parameters[index].stableParameterId ==
               poly::kFirstParameterId + static_cast<clap_id>(index));

    const std::array<double, poly::kParameterCount> expected{{
        -9.0,
        2.0,
        -7.0,
        17.5,
        4321.0,
        0.4,
        0.15,
        0.3,
        0.55,
        0.8,
        -0.65,
        0.25,
        0.7,
    }};

    const auto roundTrip = poly::makePolySynthPresetCandidate(captured);
    assert(roundTrip.ok());
    assert(roundTrip.candidate.has_value());
    verifySnapshot(*roundTrip.candidate, expected);

    // Reorder into the plug-in's intentionally non-ID-sorted host ABI order.
    constexpr std::array<clap_id, poly::kParameterCount> kHostIndexOrder{{
        1003u, 1000u, 1001u, 1002u, 1011u, 1004u, 1005u,
        1010u, 1012u, 1006u, 1007u, 1008u, 1009u,
    }};
    auto hostOrdered = captured;
    std::array<presets::PresetParameterValue, poly::kParameterCount> reordered{};
    for (std::size_t hostIndex = 0u; hostIndex < kHostIndexOrder.size(); ++hostIndex) {
        const auto id = kHostIndexOrder[hostIndex];
        const auto found = std::find_if(
            captured.parameters.begin(), captured.parameters.end(),
            [id](const auto &value) { return value.stableParameterId == id; });
        assert(found != captured.parameters.end());
        reordered[hostIndex] = *found;
    }
    hostOrdered.parameters.assign(reordered.begin(), reordered.end());
    const auto hostOrderedResult = poly::makePolySynthPresetCandidate(hostOrdered);
    assert(hostOrderedResult.ok());
    verifySnapshot(*hostOrderedResult.candidate, expected);

    // Unknown/removed IDs are ignored without disturbing neighboring known IDs.
    auto withUnknown = hostOrdered;
    withUnknown.parameters.insert(withUnknown.parameters.begin(),
                                  {900001u, 0.987654});
    const auto unknownResult = poly::makePolySynthPresetCandidate(withUnknown);
    assert(unknownResult.ok());
    verifySnapshot(*unknownResult.candidate, expected);

    // Older sparse documents migrate missing published parameters from their
    // deterministic parameter-model defaults, never from the current instance.
    auto sparse = captured;
    sparse.parameters.erase(
        std::remove_if(sparse.parameters.begin(), sparse.parameters.end(),
                       [](const auto &value) {
                           return value.stableParameterId >= 1009u;
                       }),
        sparse.parameters.end());
    const auto sparseResult = poly::makePolySynthPresetCandidate(sparse);
    assert(sparseResult.ok());
    assert(sparseResult.candidate.has_value());
    double release = 0.0;
    double filterEnvelope = 0.0;
    double pan = 0.0;
    double ampLevel = 0.0;
    assert(poly::parameterSnapshotValue(*sparseResult.candidate, 1009u, release));
    assert(poly::parameterSnapshotValue(*sparseResult.candidate, 1010u, filterEnvelope));
    assert(poly::parameterSnapshotValue(*sparseResult.candidate, 1011u, pan));
    assert(poly::parameterSnapshotValue(*sparseResult.candidate, 1012u, ampLevel));
    assert(approximately(release, 0.25));
    assert(approximately(filterEnvelope, 0.0));
    assert(approximately(pan, 0.0));
    assert(approximately(ampLevel, 1.0));

    // Wrong target and any invalid known parameter reject the complete candidate.
    auto wrongTarget = captured;
    wrongTarget.metadata.targetPluginId =
        webview_gui::examples::plugin_ids::kGainPluginId;
    const auto wrongTargetResult = poly::makePolySynthPresetCandidate(wrongTarget);
    assert(!wrongTargetResult.ok());
    assert(!wrongTargetResult.candidate.has_value());
    assert(wrongTargetResult.error == presets::PresetStateAdapterError::WrongTargetPlugin);

    auto invalidCutoff = captured;
    for (auto &value : invalidCutoff.parameters) {
        if (value.stableParameterId == 1004u)
            value.value = 50000.0;
    }
    const auto invalidCutoffResult = poly::makePolySynthPresetCandidate(invalidCutoff);
    assert(!invalidCutoffResult.ok());
    assert(!invalidCutoffResult.candidate.has_value());
    assert(invalidCutoffResult.error ==
           presets::PresetStateAdapterError::InvalidKnownParameter);

    auto fractionalWaveform = captured;
    for (auto &value : fractionalWaveform.parameters) {
        if (value.stableParameterId == 1001u)
            value.value = 1.5;
    }
    const auto fractionalWaveformResult =
        poly::makePolySynthPresetCandidate(fractionalWaveform);
    assert(!fractionalWaveformResult.ok());
    assert(!fractionalWaveformResult.candidate.has_value());
    assert(fractionalWaveformResult.error ==
           presets::PresetStateAdapterError::InvalidKnownParameter);

    // Applying a failed candidate cannot mutate an already-visible snapshot.
    auto previouslyVisible = source;
    if (invalidCutoffResult.ok())
        previouslyVisible = *invalidCutoffResult.candidate;
    verifySnapshot(previouslyVisible, expected);

    return 0;
}
