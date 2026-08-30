#include "polysynth_parameters.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
using webview_gui::examples::polysynth::ParameterSlot;
using webview_gui::examples::polysynth::ParameterSpec;
using webview_gui::examples::polysynth::coarseTuningTextForValue;
using webview_gui::examples::polysynth::coarseTuningValueFromText;
using webview_gui::examples::polysynth::kParameterCount;
using webview_gui::examples::polysynth::parameterSpecByIndex;
using webview_gui::examples::polysynth::parameterSpecForId;
using webview_gui::examples::polysynth::parameterSlotForId;
using webview_gui::examples::polysynth::supportsPolyphonicAddressing;
using webview_gui::examples::polysynth::waveformNameForValue;
using webview_gui::examples::polysynth::waveformTextForValue;
using webview_gui::examples::polysynth::waveformValueFromName;

constexpr std::uint32_t kPolyAutomationFlags =
    CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID |
    CLAP_PARAM_IS_AUTOMATABLE_PER_KEY |
    CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL |
    CLAP_PARAM_IS_AUTOMATABLE_PER_PORT;
constexpr std::uint32_t kPolyModulationFlags =
    CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID |
    CLAP_PARAM_IS_MODULATABLE_PER_KEY |
    CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL |
    CLAP_PARAM_IS_MODULATABLE_PER_PORT;

bool requirePolyphonic(ParameterSlot slot, const char *label) {
    const auto *spec = parameterSpecForId(static_cast<clap_id>(1000u + static_cast<unsigned>(slot)));
    if (!spec || spec->slot != slot || !supportsPolyphonicAddressing(*spec) ||
        (spec->flags & CLAP_PARAM_IS_AUTOMATABLE) == 0 ||
        (spec->flags & CLAP_PARAM_IS_MODULATABLE) == 0 ||
        (spec->flags & kPolyAutomationFlags) != kPolyAutomationFlags ||
        (spec->flags & kPolyModulationFlags) != kPolyModulationFlags ||
        (spec->flags & CLAP_PARAM_REQUIRES_PROCESS) == 0) {
        std::cerr << label << " is missing the full polyphonic CLAP flag surface\n";
        return false;
    }
    return true;
}

bool checkWaveformTextContract() {
    struct ExpectedWaveform {
        double value;
        const char *name;
    };
    constexpr std::array<ExpectedWaveform, 3> expected{{
        {0.0, "Sine"},
        {1.0, "Saw"},
        {2.0, "Square"},
    }};

    for (const auto &item : expected) {
        const char *name = waveformNameForValue(item.value);
        if (!name || std::strcmp(name, item.name) != 0) {
            std::cerr << "waveform value-to-text contract mismatch\n";
            return false;
        }

        double parsed = -1.0;
        if (!waveformValueFromName(item.name, parsed) || parsed != item.value) {
            std::cerr << "waveform text-to-value contract mismatch\n";
            return false;
        }
    }

    // CLAP stepped parameters convert in-range doubles to integers using a cast
    // (equivalent to truncation) before interpreting the stepped value.
    constexpr std::array<ExpectedWaveform, 2> steppedInputs{{
        {0.5, "Sine"},
        {1.9, "Saw"},
    }};
    for (const auto &item : steppedInputs) {
        const char *name = waveformNameForValue(item.value);
        if (!name || std::strcmp(name, item.name) != 0) {
            std::cerr << "waveform stepped value-to-text truncation mismatch\n";
            return false;
        }
    }

    for (double invalid : {-1.0,
                           2.0001,
                           3.0,
                           std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        if (waveformNameForValue(invalid) != nullptr) {
            std::cerr << "invalid waveform value unexpectedly formatted\n";
            return false;
        }
    }

    constexpr std::array<const char *, 6> invalidNames{{
        nullptr,
        "",
        "sine",
        "Saw ",
        "Triangle",
        "1",
    }};
    double preserved = 17.0;
    for (const char *invalid : invalidNames) {
        if (waveformValueFromName(invalid, preserved) || preserved != 17.0) {
            std::cerr << "invalid waveform name unexpectedly parsed or mutated output\n";
            return false;
        }
    }

    return true;
}

bool checkWaveformDisplayBufferContract() {
    struct DisplayCase {
        double value;
        const char *expected;
    };
    constexpr std::array<DisplayCase, 4> cases{{
        {0.0, "Sine"},
        {0.5, "Sine"},
        {1.9, "Saw"},
        {2.0, "Square"},
    }};

    for (const auto &item : cases) {
        std::array<char, CLAP_NAME_SIZE> display{};
        if (!waveformTextForValue(item.value,
                                  display.data(),
                                  static_cast<std::uint32_t>(display.size())) ||
            std::strcmp(display.data(), item.expected) != 0) {
            std::cerr << "waveform CLAP display-buffer formatting mismatch\n";
            return false;
        }
    }

    std::array<char, 7> exactSquare{{'x', 'x', 'x', 'x', 'x', 'x', 'x'}};
    if (!waveformTextForValue(2.0,
                              exactSquare.data(),
                              static_cast<std::uint32_t>(exactSquare.size())) ||
        std::strcmp(exactSquare.data(), "Square") != 0) {
        std::cerr << "waveform exact-size CLAP display buffer was rejected\n";
        return false;
    }

    std::array<char, 6> tooSmall{{'k', 'e', 'e', 'p', '!', '\0'}};
    const auto originalTooSmall = tooSmall;
    if (waveformTextForValue(2.0,
                             tooSmall.data(),
                             static_cast<std::uint32_t>(tooSmall.size())) ||
        tooSmall != originalTooSmall) {
        std::cerr << "waveform short display buffer unexpectedly succeeded or mutated output\n";
        return false;
    }

    std::array<char, 8> invalidValue{{'u', 'n', 'c', 'h', 'a', 'n', 'g', 'e'}};
    const auto originalInvalidValue = invalidValue;
    if (waveformTextForValue(std::numeric_limits<double>::quiet_NaN(),
                             invalidValue.data(),
                             static_cast<std::uint32_t>(invalidValue.size())) ||
        invalidValue != originalInvalidValue ||
        waveformTextForValue(0.0, nullptr, CLAP_NAME_SIZE) ||
        waveformTextForValue(0.0, invalidValue.data(), 0u)) {
        std::cerr << "invalid waveform CLAP display request was not rejected atomically\n";
        return false;
    }

    return true;
}

bool checkCoarseTuningTextContract() {
    struct DisplayCase {
        double value;
        const char *expected;
    };
    constexpr std::array<DisplayCase, 7> cases{{
        {-48.0, "-48"},
        {-12.9, "-12"},
        {-0.9, "0"},
        {0.0, "0"},
        {12.9, "12"},
        {47.9, "47"},
        {48.0, "48"},
    }};

    for (const auto &item : cases) {
        std::array<char, CLAP_NAME_SIZE> display{};
        if (!coarseTuningTextForValue(item.value,
                                      display.data(),
                                      static_cast<std::uint32_t>(display.size())) ||
            std::strcmp(display.data(), item.expected) != 0) {
            std::cerr << "coarse tuning stepped value-to-text contract mismatch\n";
            return false;
        }
    }

    struct ParseCase {
        const char *text;
        double expected;
    };
    constexpr std::array<ParseCase, 5> parseCases{{
        {"-48", -48.0},
        {"-12", -12.0},
        {"0", 0.0},
        {"12", 12.0},
        {"48", 48.0},
    }};
    for (const auto &item : parseCases) {
        double parsed = 99.0;
        if (!coarseTuningValueFromText(item.text, parsed) || parsed != item.expected) {
            std::cerr << "coarse tuning text-to-value contract mismatch\n";
            return false;
        }
    }

    std::array<char, 4> exactNegative{{'x', 'x', 'x', 'x'}};
    if (!coarseTuningTextForValue(-48.0,
                                  exactNegative.data(),
                                  static_cast<std::uint32_t>(exactNegative.size())) ||
        std::strcmp(exactNegative.data(), "-48") != 0) {
        std::cerr << "coarse tuning exact-size display buffer was rejected\n";
        return false;
    }

    std::array<char, 3> tooSmall{{'k', 'e', 'p'}};
    const auto originalTooSmall = tooSmall;
    if (coarseTuningTextForValue(-48.0,
                                 tooSmall.data(),
                                 static_cast<std::uint32_t>(tooSmall.size())) ||
        tooSmall != originalTooSmall) {
        std::cerr << "coarse tuning short display buffer unexpectedly succeeded or mutated output\n";
        return false;
    }

    std::array<char, 8> invalidValue{{'u', 'n', 'c', 'h', 'a', 'n', 'g', 'e'}};
    const auto originalInvalidValue = invalidValue;
    for (double invalid : {-48.0001,
                           48.0001,
                           std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        if (coarseTuningTextForValue(invalid,
                                     invalidValue.data(),
                                     static_cast<std::uint32_t>(invalidValue.size())) ||
            invalidValue != originalInvalidValue) {
            std::cerr << "invalid coarse tuning value unexpectedly formatted or mutated output\n";
            return false;
        }
    }

    constexpr std::array<const char *, 8> invalidTexts{{
        nullptr,
        "",
        "49",
        "-49",
        "12x",
        "12.5",
        "+12",
        " 12",
    }};
    double preserved = 17.0;
    for (const char *invalid : invalidTexts) {
        if (coarseTuningValueFromText(invalid, preserved) || preserved != 17.0) {
            std::cerr << "invalid coarse tuning text unexpectedly parsed or mutated output\n";
            return false;
        }
    }

    if (coarseTuningTextForValue(0.0, nullptr, CLAP_NAME_SIZE) ||
        coarseTuningTextForValue(0.0, invalidValue.data(), 0u)) {
        std::cerr << "invalid coarse tuning display request unexpectedly succeeded\n";
        return false;
    }

    return true;
}
}

int main() {
    if (kParameterCount != 12) {
        std::cerr << "unexpected PolySynth parameter count\n";
        return 1;
    }

    std::array<clap_id, kParameterCount> ids{};
    for (std::size_t index = 0; index < kParameterCount; ++index) {
        const ParameterSpec *spec = parameterSpecByIndex(static_cast<std::uint32_t>(index));
        if (!spec || !spec->name || !spec->module ||
            !std::isfinite(spec->minValue) || !std::isfinite(spec->maxValue) ||
            !std::isfinite(spec->defaultValue) || spec->minValue > spec->maxValue ||
            spec->defaultValue < spec->minValue || spec->defaultValue > spec->maxValue ||
            (spec->flags & CLAP_PARAM_REQUIRES_PROCESS) == 0) {
            std::cerr << "invalid parameter metadata at index " << index << "\n";
            return 2;
        }
        ids[index] = spec->id;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (ids[prior] == spec->id) {
                std::cerr << "duplicate stable parameter id\n";
                return 3;
            }
        }

        std::size_t slot = kParameterCount;
        if (!parameterSlotForId(spec->id, slot) || slot != index ||
            parameterSpecForId(spec->id) != spec) {
            std::cerr << "parameter id/slot mapping is not stable\n";
            return 4;
        }
    }

    if (parameterSpecByIndex(static_cast<std::uint32_t>(kParameterCount)) != nullptr ||
        parameterSpecForId(CLAP_INVALID_ID) != nullptr) {
        std::cerr << "invalid parameter lookup unexpectedly succeeded\n";
        return 5;
    }

    if (!requirePolyphonic(ParameterSlot::FineTuning, "fine tuning") ||
        !requirePolyphonic(ParameterSlot::FilterCutoff, "filter cutoff") ||
        !requirePolyphonic(ParameterSlot::FilterResonance, "filter resonance") ||
        !requirePolyphonic(ParameterSlot::FilterEnvelopeAmount, "filter envelope") ||
        !requirePolyphonic(ParameterSlot::Pan, "pan")) {
        return 6;
    }

    for (ParameterSlot slot : {ParameterSlot::MasterGain,
                               ParameterSlot::Waveform,
                               ParameterSlot::CoarseTuning,
                               ParameterSlot::AmpAttack,
                               ParameterSlot::AmpDecay,
                               ParameterSlot::AmpSustain,
                               ParameterSlot::AmpRelease}) {
        const auto *spec = parameterSpecForId(
            static_cast<clap_id>(1000u + static_cast<unsigned>(slot)));
        if (!spec || supportsPolyphonicAddressing(*spec) ||
            (spec->flags & (kPolyAutomationFlags | kPolyModulationFlags)) != 0) {
            std::cerr << "global-only parameter advertises polyphonic addressing\n";
            return 7;
        }
    }

    const auto *waveform = parameterSpecForId(1001u);
    const auto *coarse = parameterSpecForId(1002u);
    const auto *master = parameterSpecForId(1000u);
    if (!waveform || !coarse || !master ||
        (waveform->flags & (CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM)) !=
            (CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM) ||
        (coarse->flags & CLAP_PARAM_IS_STEPPED) == 0 ||
        (master->flags & CLAP_PARAM_IS_MODULATABLE) == 0 ||
        supportsPolyphonicAddressing(*master)) {
        std::cerr << "global parameter capabilities are inconsistent\n";
        return 8;
    }

    if (!checkWaveformTextContract())
        return 9;

    if (!checkWaveformDisplayBufferContract())
        return 10;

    if (!checkCoarseTuningTextContract())
        return 11;

    return 0;
}