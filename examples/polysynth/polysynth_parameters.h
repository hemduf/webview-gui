#pragma once

#include <clap/ext/params.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace webview_gui::examples::polysynth {

enum class ParameterSlot : std::uint8_t {
    MasterGain = 0,
    Waveform,
    CoarseTuning,
    FineTuning,
    FilterCutoff,
    FilterResonance,
    AmpAttack,
    AmpDecay,
    AmpSustain,
    AmpRelease,
    FilterEnvelopeAmount,
    Pan,
};

struct ParameterSpec {
    clap_id id = CLAP_INVALID_ID;
    ParameterSlot slot = ParameterSlot::MasterGain;
    const char *name = nullptr;
    const char *module = nullptr;
    double minValue = 0.0;
    double maxValue = 0.0;
    double defaultValue = 0.0;
    std::uint32_t flags = 0;
};

inline constexpr std::size_t kParameterCount = 12;
inline constexpr clap_id kFirstParameterId = 1000u;

inline constexpr std::uint32_t kBaseAutomatableFlags =
    CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
inline constexpr std::uint32_t kGlobalModulatableFlags =
    kBaseAutomatableFlags | CLAP_PARAM_IS_MODULATABLE;
inline constexpr std::uint32_t kPolyphonicAddressFlags =
    CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID |
    CLAP_PARAM_IS_AUTOMATABLE_PER_KEY |
    CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL |
    CLAP_PARAM_IS_AUTOMATABLE_PER_PORT |
    CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID |
    CLAP_PARAM_IS_MODULATABLE_PER_KEY |
    CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL |
    CLAP_PARAM_IS_MODULATABLE_PER_PORT;
inline constexpr std::uint32_t kPolyphonicParameterFlags =
    kGlobalModulatableFlags | kPolyphonicAddressFlags;

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs{{
    {1000u,
     ParameterSlot::MasterGain,
     "Master Gain",
     "Output",
     -60.0,
     12.0,
     0.0,
     kGlobalModulatableFlags},
    {1001u,
     ParameterSlot::Waveform,
     "Waveform",
     "Oscillator",
     0.0,
     2.0,
     0.0,
     kBaseAutomatableFlags | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM},
    {1002u,
     ParameterSlot::CoarseTuning,
     "Coarse Tune",
     "Oscillator",
     -48.0,
     48.0,
     0.0,
     kBaseAutomatableFlags | CLAP_PARAM_IS_STEPPED},
    {1003u,
     ParameterSlot::FineTuning,
     "Fine Tune",
     "Oscillator",
     -100.0,
     100.0,
     0.0,
     kPolyphonicParameterFlags},
    {1004u,
     ParameterSlot::FilterCutoff,
     "Cutoff",
     "Filter",
     20.0,
     20000.0,
     6000.0,
     kPolyphonicParameterFlags},
    {1005u,
     ParameterSlot::FilterResonance,
     "Resonance",
     "Filter",
     0.0,
     0.99,
     0.0,
     kPolyphonicParameterFlags},
    {1006u,
     ParameterSlot::AmpAttack,
     "Attack",
     "Amp Envelope",
     0.0,
     10.0,
     0.01,
     kBaseAutomatableFlags},
    {1007u,
     ParameterSlot::AmpDecay,
     "Decay",
     "Amp Envelope",
     0.0,
     10.0,
     0.1,
     kBaseAutomatableFlags},
    {1008u,
     ParameterSlot::AmpSustain,
     "Sustain",
     "Amp Envelope",
     0.0,
     1.0,
     0.8,
     kBaseAutomatableFlags},
    {1009u,
     ParameterSlot::AmpRelease,
     "Release",
     "Amp Envelope",
     0.001,
     10.0,
     0.25,
     kBaseAutomatableFlags},
    {1010u,
     ParameterSlot::FilterEnvelopeAmount,
     "Filter Env",
     "Filter",
     -1.0,
     1.0,
     0.0,
     kPolyphonicParameterFlags},
    {1011u,
     ParameterSlot::Pan,
     "Pan",
     "Output",
     -1.0,
     1.0,
     0.0,
     kPolyphonicParameterFlags},
}};

inline constexpr std::array<const char *, 3> kWaveformNames{{
    "Sine",
    "Saw",
    "Square",
}};

inline constexpr const ParameterSpec *parameterSpecByIndex(std::uint32_t index) noexcept {
    return index < kParameterSpecs.size() ? &kParameterSpecs[index] : nullptr;
}

inline constexpr const ParameterSpec *parameterSpecForId(clap_id id) noexcept {
    if (id < kFirstParameterId)
        return nullptr;
    const auto index = static_cast<std::uint32_t>(id - kFirstParameterId);
    return parameterSpecByIndex(index);
}

inline constexpr bool parameterSlotForId(clap_id id, std::size_t &slot) noexcept {
    const auto *spec = parameterSpecForId(id);
    if (!spec)
        return false;
    slot = static_cast<std::size_t>(spec->slot);
    return true;
}

inline constexpr bool supportsPolyphonicAddressing(const ParameterSpec &spec) noexcept {
    return (spec.flags & kPolyphonicAddressFlags) == kPolyphonicAddressFlags;
}

inline const char *waveformNameForValue(double value) noexcept {
    const auto *spec = parameterSpecForId(
        kFirstParameterId + static_cast<clap_id>(ParameterSlot::Waveform));
    if (!spec || !std::isfinite(value) || value < spec->minValue || value > spec->maxValue)
        return nullptr;

    const auto index = static_cast<std::size_t>(std::trunc(value));
    return index < kWaveformNames.size() ? kWaveformNames[index] : nullptr;
}

inline bool waveformValueFromName(const char *name, double &value) noexcept {
    if (!name)
        return false;

    for (std::size_t index = 0; index < kWaveformNames.size(); ++index) {
        if (std::strcmp(name, kWaveformNames[index]) == 0) {
            value = static_cast<double>(index);
            return true;
        }
    }
    return false;
}

} // namespace webview_gui::examples::polysynth
