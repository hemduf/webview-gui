#pragma once

#include "polysynth_parameters.h"
#include "polysynth_polyphonic_parameter_state.h"
#include "polysynth_voice_engine.h"

#include <clap/events.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace webview_gui::examples::polysynth {

// Host-facing parameter adapter for the #31 reference voice engine. Persistent
// CLAP base values, global modulation and voice-addressed modulation remain
// separate from the DSP state. The adapter consumes the same ordered core-event
// stream that drives NOTE events, so parameter changes are applied at the exact
// sample boundary without resetting oscillator phase or voice lifecycle state.
class ParameterVoiceEngine : public VoiceEngine {
public:
    bool configure(std::size_t requestedVoices,
                   double sampleRate,
                   std::uint32_t releaseSamples) noexcept {
        if (!VoiceEngine::configure(requestedVoices, sampleRate, releaseSamples) ||
            !polyphonicState_.configure(requestedVoices))
            return false;

        fineTuneBaseCents_ = 0.0;
        fineTuneGlobalModulationCents_ = 0.0;
        trackedVoices_.fill(false);
        trackedIdentities_.fill({});
        const auto slot = fineTuneSlot();
        return polyphonicState_.setGlobalBase(slot, fineTuneBaseCents_) &&
               polyphonicState_.setGlobalModulation(slot, 0.0) &&
               VoiceEngine::setFineTuningCents(0.0f);
    }

    bool setFineTuningCents(float cents) noexcept {
        if (!VoiceEngine::setFineTuningCents(cents))
            return false;

        fineTuneBaseCents_ = static_cast<double>(cents);
        fineTuneGlobalModulationCents_ = 0.0;
        return polyphonicState_.setGlobalBase(fineTuneSlot(), fineTuneBaseCents_) &&
               polyphonicState_.setGlobalModulation(fineTuneSlot(), 0.0);
    }

    [[nodiscard]] bool parameterBaseValue(clap_id id, double &value) const noexcept {
        const auto *spec = parameterSpecForId(id);
        if (!spec || spec->slot != ParameterSlot::FineTuning)
            return false;
        value = fineTuneBaseCents_;
        return true;
    }

    void reset() noexcept {
        VoiceEngine::reset();
        polyphonicState_.reset();
        trackedVoices_.fill(false);
        trackedIdentities_.fill({});
        fineTuneGlobalModulationCents_ = 0.0;
        (void)polyphonicState_.setGlobalBase(fineTuneSlot(), fineTuneBaseCents_);
        (void)VoiceEngine::setFineTuningCents(static_cast<float>(fineTuneBaseCents_));
    }

    template <typename NoteEndSink>
    bool process(const clap_input_events_t *events,
                 std::uint32_t framesCount,
                 float *left,
                 float *right,
                 NoteEndSink &noteEndSink) noexcept {
        auto coreEventSink = [this](const clap_event_header_t &header) noexcept -> bool {
            return applyCoreEvent(header);
        };

        const bool ok = VoiceEngine::processWithEvents(events,
                                                       framesCount,
                                                       left,
                                                       right,
                                                       coreEventSink,
                                                       noteEndSink);
        if (!syncVoices())
            return false;
        return ok;
    }

private:
    static constexpr std::size_t fineTuneSlot() noexcept {
        return static_cast<std::size_t>(ParameterSlot::FineTuning);
    }

    static bool isGlobalAddress(std::int32_t noteId,
                                std::int16_t portIndex,
                                std::int16_t channel,
                                std::int16_t key) noexcept {
        return noteId == -1 && portIndex == -1 && channel == -1 && key == -1;
    }

    bool syncVoices() noexcept {
        for (VoiceAllocator::VoiceIndex index = 0; index < capacity(); ++index) {
            VoiceIdentity identity{};
            const bool active = VoiceEngine::voiceIdentity(index, identity);
            if (!active) {
                if (trackedVoices_[index]) {
                    if (!polyphonicState_.stopVoice(index))
                        return false;
                    trackedVoices_[index] = false;
                    trackedIdentities_[index] = {};
                }
                continue;
            }

            if (!trackedVoices_[index] || trackedIdentities_[index] != identity) {
                if (!polyphonicState_.startVoice(index, identity))
                    return false;
                trackedVoices_[index] = true;
                trackedIdentities_[index] = identity;
            }
        }
        return true;
    }

    bool applyFineTuningState() noexcept {
        if (!syncVoices())
            return false;

        const auto globalEffective = std::clamp(fineTuneBaseCents_ +
                                                    fineTuneGlobalModulationCents_,
                                                -100.0,
                                                100.0);
        if (!VoiceEngine::setFineTuningCents(static_cast<float>(globalEffective)))
            return false;

        const auto slot = fineTuneSlot();
        for (VoiceAllocator::VoiceIndex index = 0; index < capacity(); ++index) {
            if (!trackedVoices_[index])
                continue;

            double base = 0.0;
            double modulation = 0.0;
            if (!polyphonicState_.baseValue(index, slot, base) ||
                !polyphonicState_.modulation(index, slot, modulation))
                return false;

            const auto effective = std::clamp(base + modulation, -100.0, 100.0);
            if (!VoiceEngine::setVoiceFineTuningCents(
                    index, static_cast<float>(effective)))
                return false;
        }
        return true;
    }

    bool applyCoreEvent(const clap_event_header_t &header) noexcept {
        if (header.space_id != CLAP_CORE_EVENT_SPACE_ID)
            return true;
        if (header.size < sizeof(clap_event_header_t))
            return false;

        if (header.type == CLAP_EVENT_PARAM_VALUE) {
            if (header.size < sizeof(clap_event_param_value_t))
                return false;
            const auto &event =
                *reinterpret_cast<const clap_event_param_value_t *>(&header);
            const auto *spec = parameterSpecForId(event.param_id);
            if (!spec)
                return false;
            if (spec->slot != ParameterSlot::FineTuning)
                return true;
            if (!std::isfinite(event.value) ||
                event.value < spec->minValue || event.value > spec->maxValue)
                return false;
            if (!syncVoices() || !polyphonicState_.applyValue(fineTuneSlot(), event))
                return false;

            if (isGlobalAddress(event.note_id,
                                event.port_index,
                                event.channel,
                                event.key))
                fineTuneBaseCents_ = event.value;
            return applyFineTuningState();
        }

        if (header.type == CLAP_EVENT_PARAM_MOD) {
            if (header.size < sizeof(clap_event_param_mod_t))
                return false;
            const auto &event =
                *reinterpret_cast<const clap_event_param_mod_t *>(&header);
            const auto *spec = parameterSpecForId(event.param_id);
            if (!spec)
                return false;
            if (spec->slot != ParameterSlot::FineTuning)
                return true;
            if (!std::isfinite(event.amount))
                return false;
            if (!syncVoices() ||
                !polyphonicState_.applyModulation(fineTuneSlot(), event))
                return false;

            if (isGlobalAddress(event.note_id,
                                event.port_index,
                                event.channel,
                                event.key))
                fineTuneGlobalModulationCents_ = event.amount;
            return applyFineTuningState();
        }

        return true;
    }

    PolyphonicParameterState polyphonicState_{};
    std::array<VoiceIdentity, VoiceAllocator::kMaximumVoices> trackedIdentities_{};
    std::array<bool, VoiceAllocator::kMaximumVoices> trackedVoices_{};
    double fineTuneBaseCents_ = 0.0;
    double fineTuneGlobalModulationCents_ = 0.0;
};

} // namespace webview_gui::examples::polysynth
