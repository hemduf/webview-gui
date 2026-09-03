#pragma once

#include "polysynth_parameters.h"

#include <cmath>
#include <cstdint>

namespace webview_gui::examples::polysynth {

struct ParameterSnapshot {
    float fineTuneCents = 0.0f;
    float masterGainDb = 0.0f;
    std::uint32_t waveform = 0u;
    std::int32_t coarseTuneSemitones = 0;
    float pan = 0.0f;
    float filterCutoffHz = 0.0f;
    float filterResonance = 0.0f;
    float filterEnvelopeAmount = 0.0f;
    float ampLevel = 0.0f;
    float ampAttackSeconds = 0.0f;
    float ampDecaySeconds = 0.0f;
    float ampSustain = 0.0f;
    float ampReleaseSeconds = 0.0f;
};

[[nodiscard]] constexpr ParameterSnapshot defaultParameterSnapshot() noexcept {
    return {
        static_cast<float>(kParameterSpecs[3].defaultValue),
        static_cast<float>(kParameterSpecs[0].defaultValue),
        static_cast<std::uint32_t>(kParameterSpecs[1].defaultValue),
        static_cast<std::int32_t>(kParameterSpecs[2].defaultValue),
        static_cast<float>(kParameterSpecs[11].defaultValue),
        static_cast<float>(kParameterSpecs[4].defaultValue),
        static_cast<float>(kParameterSpecs[5].defaultValue),
        static_cast<float>(kParameterSpecs[10].defaultValue),
        static_cast<float>(kParameterSpecs[12].defaultValue),
        static_cast<float>(kParameterSpecs[6].defaultValue),
        static_cast<float>(kParameterSpecs[7].defaultValue),
        static_cast<float>(kParameterSpecs[8].defaultValue),
        static_cast<float>(kParameterSpecs[9].defaultValue),
    };
}

[[nodiscard]] constexpr bool parameterSnapshotsEqual(
    const ParameterSnapshot &a,
    const ParameterSnapshot &b) noexcept {
    return a.fineTuneCents == b.fineTuneCents &&
           a.masterGainDb == b.masterGainDb &&
           a.waveform == b.waveform &&
           a.coarseTuneSemitones == b.coarseTuneSemitones &&
           a.pan == b.pan &&
           a.filterCutoffHz == b.filterCutoffHz &&
           a.filterResonance == b.filterResonance &&
           a.filterEnvelopeAmount == b.filterEnvelopeAmount &&
           a.ampLevel == b.ampLevel &&
           a.ampAttackSeconds == b.ampAttackSeconds &&
           a.ampDecaySeconds == b.ampDecaySeconds &&
           a.ampSustain == b.ampSustain &&
           a.ampReleaseSeconds == b.ampReleaseSeconds;
}

[[nodiscard]] inline bool parameterSnapshotValue(const ParameterSnapshot &snapshot,
                                                  clap_id paramId,
                                                  double &value) noexcept {
    switch (paramId) {
        case 1000u:
            value = snapshot.masterGainDb;
            return true;
        case 1001u:
            value = snapshot.waveform;
            return true;
        case 1002u:
            value = snapshot.coarseTuneSemitones;
            return true;
        case 1003u:
            value = snapshot.fineTuneCents;
            return true;
        case 1004u:
            value = snapshot.filterCutoffHz;
            return true;
        case 1005u:
            value = snapshot.filterResonance;
            return true;
        case 1006u:
            value = snapshot.ampAttackSeconds;
            return true;
        case 1007u:
            value = snapshot.ampDecaySeconds;
            return true;
        case 1008u:
            value = snapshot.ampSustain;
            return true;
        case 1009u:
            value = snapshot.ampReleaseSeconds;
            return true;
        case 1010u:
            value = snapshot.filterEnvelopeAmount;
            return true;
        case 1011u:
            value = snapshot.pan;
            return true;
        case 1012u:
            value = snapshot.ampLevel;
            return true;
        default:
            return false;
    }
}

[[nodiscard]] inline bool setParameterSnapshotValue(ParameterSnapshot &snapshot,
                                                     clap_id paramId,
                                                     double value) noexcept {
    const auto *spec = parameterSpecForId(paramId);
    if (!spec || !std::isfinite(value) || value < spec->minValue ||
        value > spec->maxValue)
        return false;

    if ((spec->flags & CLAP_PARAM_IS_STEPPED) != 0u && std::floor(value) != value)
        return false;

    switch (paramId) {
        case 1000u:
            snapshot.masterGainDb = static_cast<float>(value);
            return true;
        case 1001u:
            snapshot.waveform = static_cast<std::uint32_t>(value);
            return true;
        case 1002u:
            snapshot.coarseTuneSemitones = static_cast<std::int32_t>(value);
            return true;
        case 1003u:
            snapshot.fineTuneCents = static_cast<float>(value);
            return true;
        case 1004u:
            snapshot.filterCutoffHz = static_cast<float>(value);
            return true;
        case 1005u:
            snapshot.filterResonance = static_cast<float>(value);
            return true;
        case 1006u:
            snapshot.ampAttackSeconds = static_cast<float>(value);
            return true;
        case 1007u:
            snapshot.ampDecaySeconds = static_cast<float>(value);
            return true;
        case 1008u:
            snapshot.ampSustain = static_cast<float>(value);
            return true;
        case 1009u:
            snapshot.ampReleaseSeconds = static_cast<float>(value);
            return true;
        case 1010u:
            snapshot.filterEnvelopeAmount = static_cast<float>(value);
            return true;
        case 1011u:
            snapshot.pan = static_cast<float>(value);
            return true;
        case 1012u:
            snapshot.ampLevel = static_cast<float>(value);
            return true;
        default:
            return false;
    }
}

} // namespace webview_gui::examples::polysynth
