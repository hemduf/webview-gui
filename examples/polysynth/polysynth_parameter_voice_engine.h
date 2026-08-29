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
// CLAP base values, global modulation, voice-addressed modulation and note
// expressions remain separate from the DSP state. The adapter consumes the same
// ordered core-event stream that drives NOTE events, so changes are applied at
// the exact sample boundary without resetting oscillator phase or lifecycle state.
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
        voiceTuningExpressionSemitones_.fill(0.0);
        clearTransientExpressionState();
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
        voiceTuningExpressionSemitones_.fill(0.0);
        clearTransientExpressionState();
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
        clearTransientExpressionState();

        struct CoreEventSink {
            ParameterVoiceEngine *owner = nullptr;

            bool operator()(const clap_event_header_t &header) noexcept {
                return owner && owner->applyCoreEvent(header);
            }

            bool noteOnDispatched(const ScheduledNoteEvent &event) noexcept {
                return owner && owner->noteOnDispatched(event);
            }
        } coreEventSink{this};

        const bool ok = VoiceEngine::processWithEvents(events,
                                                       framesCount,
                                                       left,
                                                       right,
                                                       coreEventSink,
                                                       noteEndSink);
        const bool synced = syncVoices();
        const bool restored = restoreGlobalFineTuningDefault();
        clearTransientExpressionState();
        return ok && synced && restored;
    }

private:
    struct PendingTuningExpression {
        VoiceIdentity address{};
        std::uint32_t time = 0;
        double semitones = 0.0;
        bool active = false;
    };

    static constexpr std::size_t fineTuneSlot() noexcept {
        return static_cast<std::size_t>(ParameterSlot::FineTuning);
    }

    static bool isGlobalAddress(std::int32_t noteId,
                                std::int16_t portIndex,
                                std::int16_t channel,
                                std::int16_t key) noexcept {
        return noteId == -1 && portIndex == -1 && channel == -1 && key == -1;
    }

    static bool addressMatches(const VoiceIdentity &identity,
                               const VoiceIdentity &address) noexcept {
        return (address.noteId == -1 || address.noteId == identity.noteId) &&
               (address.portIndex == -1 || address.portIndex == identity.portIndex) &&
               (address.channel == -1 || address.channel == identity.channel) &&
               (address.key == -1 || address.key == identity.key);
    }

    static VoiceIdentity addressFrom(const clap_event_note_expression_t &event) noexcept {
        return {event.note_id, event.port_index, event.channel, event.key};
    }

    static bool validExpressionAddress(const clap_event_note_expression_t &event) noexcept {
        return event.note_id >= -1 &&
               event.port_index >= -1 &&
               event.channel >= -1 && event.channel <= 15 &&
               event.key >= -1 && event.key <= 127;
    }

    void clearTransientExpressionState() noexcept {
        for (auto &entry : pendingTuningExpressions_)
            entry = {};
    }

    bool storePendingTuning(const clap_event_note_expression_t &event) noexcept {
        const auto address = addressFrom(event);
        for (auto &entry : pendingTuningExpressions_) {
            if (!entry.active || entry.time != event.header.time ||
                entry.address != address)
                continue;
            entry.semitones = event.value;
            return true;
        }

        for (auto &entry : pendingTuningExpressions_) {
            if (entry.active)
                continue;
            entry.address = address;
            entry.time = event.header.time;
            entry.semitones = event.value;
            entry.active = true;
            return true;
        }
        return false;
    }

    bool pendingTuningFor(const VoiceIdentity &identity,
                          std::uint32_t time,
                          double &semitones) const noexcept {
        bool found = false;
        semitones = 0.0;
        for (const auto &entry : pendingTuningExpressions_) {
            if (!entry.active || entry.time != time ||
                !addressMatches(identity, entry.address))
                continue;
            semitones = entry.semitones;
            found = true;
        }
        return found;
    }

    double globalFineTuningCents() const noexcept {
        return std::clamp(fineTuneBaseCents_ + fineTuneGlobalModulationCents_,
                          -100.0,
                          100.0);
    }

    bool restoreGlobalFineTuningDefault() noexcept {
        return VoiceEngine::setFineTuningCents(
            static_cast<float>(globalFineTuningCents()));
    }

    // The scheduler supplies the exact allocated slot after lifecycle/DSP NOTE_ON
    // dispatch but before rendering resumes. Reset adapter-local state for every
    // generation, even when a host without note IDs reuses the identical visible
    // tuple in the same slot. Then apply any earlier same-sample expression before
    // the first sample of the new generation is rendered.
    bool noteOnDispatched(const ScheduledNoteEvent &event) noexcept {
        const auto index = event.voiceIndex;
        if (index >= capacity() || event.kind != ScheduledNoteKind::NoteOn)
            return false;

        if (!polyphonicState_.startVoice(index, event.identity))
            return false;
        trackedVoices_[index] = true;
        trackedIdentities_[index] = event.identity;
        voiceTuningExpressionSemitones_[index] = 0.0;

        double expressionSemitones = 0.0;
        if (pendingTuningFor(event.identity, event.time, expressionSemitones))
            voiceTuningExpressionSemitones_[index] = expressionSemitones;

        return applyFineTuningState();
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
                    voiceTuningExpressionSemitones_[index] = 0.0;
                }
                continue;
            }

            if (!trackedVoices_[index] || trackedIdentities_[index] != identity) {
                if (!polyphonicState_.startVoice(index, identity))
                    return false;
                trackedVoices_[index] = true;
                trackedIdentities_[index] = identity;
                voiceTuningExpressionSemitones_[index] = 0.0;
            }
        }
        return true;
    }

    bool applyFineTuningState() noexcept {
        if (!syncVoices() || !restoreGlobalFineTuningDefault())
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

            const auto effective = std::clamp(
                base + modulation + voiceTuningExpressionSemitones_[index] * 100.0,
                -100.0,
                100.0);
            if (!VoiceEngine::setVoiceFineTuningCents(
                    index, static_cast<float>(effective)))
                return false;
        }
        return true;
    }

    bool applyTuningExpression(const clap_event_note_expression_t &event) noexcept {
        if (!std::isfinite(event.value) || event.value < -120.0 || event.value > 120.0 ||
            !validExpressionAddress(event))
            return false;
        if (!syncVoices())
            return false;

        const auto address = addressFrom(event);
        bool matched = false;
        for (VoiceAllocator::VoiceIndex index = 0; index < capacity(); ++index) {
            if (!trackedVoices_[index] ||
                !addressMatches(trackedIdentities_[index], address))
                continue;
            voiceTuningExpressionSemitones_[index] = event.value;
            matched = true;
        }

        if (!storePendingTuning(event))
            return false;
        return matched ? applyFineTuningState() : true;
    }

    bool applyCoreEvent(const clap_event_header_t &header) noexcept {
        if (header.space_id != CLAP_CORE_EVENT_SPACE_ID)
            return true;
        if (header.size < sizeof(clap_event_header_t))
            return false;

        if (header.type == CLAP_EVENT_NOTE_EXPRESSION) {
            if (header.size < sizeof(clap_event_note_expression_t))
                return false;
            const auto &event =
                *reinterpret_cast<const clap_event_note_expression_t *>(&header);
            if (event.expression_id != CLAP_NOTE_EXPRESSION_TUNING)
                return true;
            return applyTuningExpression(event);
        }

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
    std::array<double, VoiceAllocator::kMaximumVoices> voiceTuningExpressionSemitones_{};
    std::array<PendingTuningExpression, VoiceAllocator::kMaximumVoices>
        pendingTuningExpressions_{};
    double fineTuneBaseCents_ = 0.0;
    double fineTuneGlobalModulationCents_ = 0.0;
};

} // namespace webview_gui::examples::polysynth
