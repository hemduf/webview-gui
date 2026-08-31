#include "polysynth_parameters.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {
using webview_gui::examples::polysynth::ParameterSlot;
using webview_gui::examples::polysynth::ParameterSpec;
using webview_gui::examples::polysynth::kParameterCount;
using webview_gui::examples::polysynth::parameterSpecByIndex;
using webview_gui::examples::polysynth::parameterSpecForId;
using webview_gui::examples::polysynth::parameterSlotForId;
using webview_gui::examples::polysynth::supportsPolyphonicAddressing;

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

    return 0;
}
