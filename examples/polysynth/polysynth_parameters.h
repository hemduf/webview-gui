#pragma once

#include <clap/ext/params.h>

#include <array>
#include <charconv>
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

static_assert(kParameterCount == 13,
              "PolySynth internal parameter model must reserve Amp Level after stable ID 1011");

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

namespace detail {
inline bool boundedParameterDisplayLength(const char *text,
                                          std::size_t &length) noexcept {
    length = 0;
    if (!text)
        return false;
    while (length < CLAP_NAME_SIZE && text[length] != '\0')
        ++length;
    return length > 0 && length < CLAP_NAME_SIZE;
}

inline bool copyStaticParameterDisplayText(const char *text,
                                           char *display,
                                           std::uint32_t size) noexcept {
    if (!text || !display || size == 0u)
        return false;

    const std::size_t length = std::strlen(text);
    if (length >= static_cast<std::size_t>(size))
        return false;

    std::memcpy(display, text, length + 1u);
    return true;
}
} // namespace detail

inline const char *waveformNameForValue(double value) noexcept {
    const auto *spec = parameterSpecForId(
        kFirstParameterId + static_cast<clap_id>(ParameterSlot::Waveform));
    if (!spec || !std::isfinite(value) || value < spec->minValue || value > spec->maxValue)
        return nullptr;

    const auto index = static_cast<std::size_t>(std::trunc(value));
    return index < kWaveformNames.size() ? kWaveformNames[index] : nullptr;
}

inline bool waveformTextForValue(double value,
                                 char *display,
                                 std::uint32_t size) noexcept {
    return detail::copyStaticParameterDisplayText(
        waveformNameForValue(value), display, size);
}

inline bool waveformValueFromName(const char *name, double &value) noexcept {
    std::size_t length = 0;
    if (!detail::boundedParameterDisplayLength(name, length))
        return false;

    for (std::size_t index = 0; index < kWaveformNames.size(); ++index) {
        const auto *expected = kWaveformNames[index];
        const auto expectedLength = std::strlen(expected);
        if (length == expectedLength && std::memcmp(name, expected, length) == 0) {
            value = static_cast<double>(index);
            return true;
        }
    }
    return false;
}

inline bool coarseTuningTextForValue(double value,
                                     char *display,
                                     std::uint32_t size) noexcept {
    const auto *spec = parameterSpecForId(
        kFirstParameterId + static_cast<clap_id>(ParameterSlot::CoarseTuning));
    if (!spec || !std::isfinite(value) || value < spec->minValue || value > spec->maxValue)
        return false;

    std::array<char, 4> text{};
    const auto semitones = static_cast<int>(std::trunc(value));
    const auto result = std::to_chars(text.data(), text.data() + text.size() - 1u, semitones);
    if (result.ec != std::errc())
        return false;
    *result.ptr = '\0';
    return detail::copyStaticParameterDisplayText(text.data(), display, size);
}

inline bool coarseTuningValueFromText(const char *display, double &value) noexcept {
    const auto *spec = parameterSpecForId(
        kFirstParameterId + static_cast<clap_id>(ParameterSlot::CoarseTuning));
    if (!spec)
        return false;

    std::size_t length = 0;
    if (!detail::boundedParameterDisplayLength(display, length))
        return false;

    const char *end = display + length;
    int semitones = 0;
    const auto result = std::from_chars(display, end, semitones);
    if (result.ec != std::errc() || result.ptr != end ||
        static_cast<double>(semitones) < spec->minValue ||
        static_cast<double>(semitones) > spec->maxValue)
        return false;

    value = static_cast<double>(semitones);
    return true;
}

inline bool continuousParameterTextForValue(clap_id id,
                                            double value,
                                            char *display,
                                            std::uint32_t size) noexcept {
    const auto *spec = parameterSpecForId(id);
    if (!spec || (spec->flags & CLAP_PARAM_IS_STEPPED) != 0u ||
        !display || size == 0u || !std::isfinite(value) ||
        value < spec->minValue || value > spec->maxValue)
        return false;

    std::array<char, CLAP_NAME_SIZE> text{};
    const auto result = std::to_chars(text.data(),
                                      text.data() + text.size() - 1u,
                                      value,
                                      std::chars_format::general);
    if (result.ec != std::errc())
        return false;
    *result.ptr = '\0';
    return detail::copyStaticParameterDisplayText(text.data(), display, size);
}

inline bool continuousParameterValueFromText(clap_id id,
                                             const char *display,
                                             double &value) noexcept {
    const auto *spec = parameterSpecForId(id);
    if (!spec || (spec->flags & CLAP_PARAM_IS_STEPPED) != 0u)
        return false;

    std::size_t length = 0;
    if (!detail::boundedParameterDisplayLength(display, length))
        return false;

    const char *end = display + length;
    double parsed = 0.0;
    const auto result = std::from_chars(display, end, parsed, std::chars_format::general);
    if (result.ec != std::errc() || result.ptr != end || !std::isfinite(parsed) ||
        parsed < spec->minValue || parsed > spec->maxValue)
        return false;

    value = parsed;
    return true;
}

inline bool parameterTextForValue(clap_id id,
                                  double value,
                                  char *display,
                                  std::uint32_t size) noexcept {
    const auto *spec = parameterSpecForId(id);
    if (!spec)
        return false;

    if ((spec->flags & CLAP_PARAM_IS_STEPPED) == 0u)
        return continuousParameterTextForValue(id, value, display, size);
    if (spec->slot == ParameterSlot::Waveform)
        return waveformTextForValue(value, display, size);
    if (spec->slot == ParameterSlot::CoarseTuning)
        return coarseTuningTextForValue(value, display, size);
    return false;
}

inline bool parameterValueFromText(clap_id id,
                                   const char *display,
                                   double &value) noexcept {
    const auto *spec = parameterSpecForId(id);
    if (!spec)
        return false;

    if ((spec->flags & CLAP_PARAM_IS_STEPPED) == 0u)
        return continuousParameterValueFromText(id, display, value);
    if (spec->slot == ParameterSlot::Waveform)
        return waveformValueFromName(display, value);
    if (spec->slot == ParameterSlot::CoarseTuning)
        return coarseTuningValueFromText(display, value);
    return false;
}

} // namespace webview_gui::examples::polysynth
