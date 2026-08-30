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

        masterGainBaseDb_ = 0.0;
        masterGainGlobalModulationDb_ = 0.0;
        fineTuneBaseCents_ = 0.0;
        fineTuneGlobalModulationCents_ = 0.0;
        trackedVoices_.fill(false);
        trackedIdentities_.fill({});
        voiceTuningExpressionSemitones_.fill(0.0);
        voiceVolumeExpressions_.fill(1.0);
        voicePerformanceExpressions_.fill(1.0);
        voicePressureExpressions_.fill(0.0);
        clearTransientExpressionState();
        const auto slot = fineTuneSlot();
        return polyphonicState_.setGlobalBase(slot, fineTuneBaseCents_) &&
               polyphonicState_.setGlobalModulation(slot, 0.0) &&
               VoiceEngine::setFineTuningCents(0.0f) &&
               applyMasterGainState();
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
        if (!spec)
            return false;
        if (spec->slot == ParameterSlot::MasterGain) {
            value = masterGainBaseDb_;
            return true;
        }
        if (spec->slot == ParameterSlot::FineTuning) {
            value = fineTuneBaseCents_;
            return true;
        }
        return false;
    }

    void reset() noexcept {
        VoiceEngine::reset();
        polyphonicState_.reset();
        trackedVoices_.fill(false);
        trackedIdentities_.fill({});
        voiceTuningExpressionSemitones_.fill(0.0);
        voiceVolumeExpressions_.fill(1.0);
        voicePerformanceExpressions_.fill(1.0);
        voicePressureExpressions_.fill(0.0);
        clearTransientExpressionState();
        masterGainGlobalModulationDb_ = 0.0;
        fineTuneGlobalModulationCents_ = 0.0;
        (void)polyphonicState_.setGlobalBase(fineTuneSlot(), fineTuneBaseCents_);
        (void)VoiceEngine::setFineTuningCents(static_cast<float>(fineTuneBaseCents_));
        (void)applyMasterGainState();
    }

    // params.flush() is delivered on the audio thread while the plug-in is active
    // and is not concurrent with process(). Normalize its lost sample offset to
    // sample zero and route the event through the exact same value/modulation
    // validation, addressing, and voice-local composition used by process().
    bool applyParameterFlushEvent(const clap_event_header_t &header) noexcept {
        if (header.space_id != CLAP_CORE_EVENT_SPACE_ID ||
            header.size < sizeof(clap_event_header_t))
            return false;

        if (header.type == CLAP_EVENT_PARAM_VALUE) {
            if (header.size < sizeof(clap_event_param_value_t))
                return false;
            auto event = *reinterpret_cast<const clap_event_param_value_t *>(&header);
            event.header.time = 0;
            return applyCoreEvent(event.header);
        }

        if (header.type == CLAP_EVENT_PARAM_MOD) {
            if (header.size < sizeof(clap_event_param_mod_t))
                return false;
            auto event = *reinterpret_cast<const clap_event_param_mod_t *>(&header);
            event.header.time = 0;
            return applyCoreEvent(event.header);
        }

        return false;
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

    struct PendingVolumeExpression {
        VoiceIdentity address{};
        std::uint32_t time = 0;
        double gain = 1.0;
        bool active = false;
    };

    struct PendingPerformanceExpression {
        VoiceIdentity address{};
        std::uint32_t time = 0;
        double expression = 1.0;
        bool active = false;
    };

    struct PendingPressureExpression {
        VoiceIdentity address{};
        std::uint32_t time = 0;
        double pressure = 0.0;
        bool active = false;
    };

    struct PendingPanExpression {
        VoiceIdentity address{};
        std::uint32_t time = 0;
        double pan = 0.5;
        bool active = false;
    };

    struct PendingBrightnessExpression {
        VoiceIdentity address{};
        std::uint32_t time = 0;
        double brightness = 0.0;
        bool active = false;
    };

    static constexpr clap_id masterGainId() noexcept {
        return kFirstParameterId + static_cast<clap_id>(ParameterSlot::MasterGain);
    }

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

    static double pressureGain(double pressure) noexcept {
        return 1.0 + pressure;
    }

    void clearPendingExpressionEntries() noexcept {
        for (auto &entry : pendingTuningExpressions_)
            entry = {};
        for (auto &entry : pendingVolumeExpressions_)
            entry = {};
        for (auto &entry : pendingPerformanceExpressions_)
            entry = {};
        for (auto &entry : pendingPressureExpressions_)
            entry = {};
        for (auto &entry : pendingPanExpressions_)
            entry = {};
        for (auto &entry : pendingBrightnessExpressions_)
            entry = {};
    }

    void clearTransientExpressionState() noexcept {
        clearPendingExpressionEntries();
        pendingExpressionTime_ = 0;
        pendingExpressionTimeValid_ = false;
    }

    // Pending note-expression state only bridges events which share one sample
    // timestamp. Once the ordered CLAP stream advances, retaining older entries
    // wastes bounded RT capacity and can reject otherwise valid event streams.
    // Clear the fixed caches once per timestamp transition while preserving all
    // expressions at the current sample for a later NOTE_ON at that same sample.
    void prepareTransientExpressionTime(std::uint32_t time) noexcept {
        if (pendingExpressionTimeValid_ && pendingExpressionTime_ == time)
            return;
        clearPendingExpressionEntries();
        pendingExpressionTime_ = time;
        pendingExpressionTimeValid_ = true;
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

    bool storePendingVolume(const clap_event_note_expression_t &event) noexcept {
        const auto address = addressFrom(event);
        for (auto &entry : pendingVolumeExpressions_) {
            if (!entry.active || entry.time != event.header.time ||
                entry.address != address)
                continue;
            entry.gain = event.value;
            return true;
        }

        for (auto &entry : pendingVolumeExpressions_) {
            if (entry.active)
                continue;
            entry.address = address;
            entry.time = event.header.time;
            entry.gain = event.value;
            entry.active = true;
            return true;
        }
        return false;
    }

    bool storePendingPerformance(const clap_event_note_expression_t &event) noexcept {
        const auto address = addressFrom(event);
        for (auto &entry : pendingPerformanceExpressions_) {
            if (!entry.active || entry.time != event.header.time ||
                entry.address != address)
                continue;
            entry.expression = event.value;
            return true;
        }

        for (auto &entry : pendingPerformanceExpressions_) {
            if (entry.active)
                continue;
            entry.address = address;
            entry.time = event.header.time;
            entry.expression = event.value;
            entry.active = true;
            return true;
        }
        return false;
    }

    bool storePendingPressure(const clap_event_note_expression_t &event) noexcept {
        const auto address = addressFrom(event);
        for (auto &entry : pendingPressureExpressions_) {
            if (!entry.active || entry.time != event.header.time ||
                entry.address != address)
                continue;
            entry.pressure = event.value;
            return true;
        }

        for (auto &entry : pendingPressureExpressions_) {
            if (entry.active)
                continue;
            entry.address = address;
            entry.time = event.header.time;
            entry.pressure = event.value;
            entry.active = true;
            return true;
        }
        return false;
    }

    bool storePendingPan(const clap_event_note_expression_t &event) noexcept {
        const auto address = addressFrom(event);
        for (auto &entry : pendingPanExpressions_) {
            if (!entry.active || entry.time != event.header.time ||
                entry.address != address)
                continue;
            entry.pan = event.value;
            return true;
        }

        for (auto &entry : pendingPanExpressions_) {
            if (entry.active)
                continue;
            entry.address = address;
            entry.time = event.header.time;
            entry.pan = event.value;
            entry.active = true;
            return true;
        }
        return false;
    }

    bool storePendingBrightness(const clap_event_note_expression_t &event) noexcept {
        const auto address = addressFrom(event);
        for (auto &entry : pendingBrightnessExpressions_) {
            if (!entry.active || entry.time != event.header.time ||
                entry.address != address)
                continue;
            entry.brightness = event.value;
            return true;
        }

        for (auto &entry : pendingBrightnessExpressions_) {
            if (entry.active)
                continue;
            entry.address = address;
            entry.time = event.header.time;
            entry.brightness = event.value;
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

    bool pendingVolumeFor(const VoiceIdentity &identity,
                          std::uint32_t time,
                          double &gain) const noexcept {
        bool found = false;
        gain = 1.0;
        for (const auto &entry : pendingVolumeExpressions_) {
            if (!entry.active || entry.time != time ||
                !addressMatches(identity, entry.address))
                continue;
            gain = entry.gain;
            found = true;
        }
        return found;
    }

    bool pendingPerformanceFor(const VoiceIdentity &identity,
                               std::uint32_t time,
                               double &expression) const noexcept {
        bool found = false;
        expression = 1.0;
        for (const auto &entry : pendingPerformanceExpressions_) {
            if (!entry.active || entry.time != time ||
                !addressMatches(identity, entry.address))
                continue;
            expression = entry.expression;
            found = true;
        }
        return found;
    }

    bool pendingPressureFor(const VoiceIdentity &identity,
                            std::uint32_t time,
                            double &pressure) const noexcept {
        bool found = false;
        pressure = 0.0;
        for (const auto &entry : pendingPressureExpressions_) {
            if (!entry.active || entry.time != time ||
                !addressMatches(identity, entry.address))
                continue;
            pressure = entry.pressure;
            found = true;
        }
        return found;
    }

    bool pendingPanFor(const VoiceIdentity &identity,
                       std::uint32_t time,
                       double &pan) const noexcept {
        bool found = false;
        pan = 0.5;
        for (const auto &entry : pendingPanExpressions_) {
            if (!entry.active || entry.time != time ||
                !addressMatches(identity, entry.address))
                continue;
            pan = entry.pan;
            found = true;
        }
        return found;
    }

    bool pendingBrightnessFor(const VoiceIdentity &identity,
                              std::uint32_t time,
                              double &brightness) const noexcept {
        bool found = false;
        brightness = 0.0;
        for (const auto &entry : pendingBrightnessExpressions_) {
            if (!entry.active || entry.time != time ||
                !addressMatches(identity, entry.address))
                continue;
            brightness = entry.brightness;
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

    bool applyMasterGainState() noexcept {
        const auto *spec = parameterSpecForId(masterGainId());
        if (!spec)
            return false;
        const auto effectiveDb = std::clamp(masterGainBaseDb_ + masterGainGlobalModulationDb_,
                                            spec->minValue,
                                            spec->maxValue);
        // A one-sample prepared ramp preserves the VoiceEngine's click-safe gain
        // state machine while making the new target effective on the first sample
        // rendered after the CLAP event boundary.
        return VoiceEngine::setMasterGainDb(static_cast<float>(effectiveDb), 1u);
    }

    bool applyVoiceExpressionGain(VoiceAllocator::VoiceIndex index) noexcept {
        if (index >= capacity() || !trackedVoices_[index])
            return false;
        const double gain = voiceVolumeExpressions_[index] *
                            voicePerformanceExpressions_[index] *
                            pressureGain(voicePressureExpressions_[index]);
        return VoiceEngine::setVoiceVolumeExpression(index, static_cast<float>(gain));
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

        prepareTransientExpressionTime(event.time);

        if (!polyphonicState_.startVoice(index, event.identity))
            return false;
        trackedVoices_[index] = true;
        trackedIdentities_[index] = event.identity;
        voiceTuningExpressionSemitones_[index] = 0.0;
        voiceVolumeExpressions_[index] = 1.0;
        voicePerformanceExpressions_[index] = 1.0;
        voicePressureExpressions_[index] = 0.0;

        double expressionSemitones = 0.0;
        if (pendingTuningFor(event.identity, event.time, expressionSemitones))
            voiceTuningExpressionSemitones_[index] = expressionSemitones;

        if (!applyFineTuningState())
            return false;

        double volumeGain = 1.0;
        if (pendingVolumeFor(event.identity, event.time, volumeGain))
            voiceVolumeExpressions_[index] = volumeGain;

        double performanceExpression = 1.0;
        if (pendingPerformanceFor(event.identity, event.time, performanceExpression))
            voicePerformanceExpressions_[index] = performanceExpression;

        double pressure = 0.0;
        if (pendingPressureFor(event.identity, event.time, pressure))
            voicePressureExpressions_[index] = pressure;

        if (!applyVoiceExpressionGain(index))
            return false;

        double pan = 0.5;
        if (pendingPanFor(event.identity, event.time, pan) &&
            !VoiceEngine::setVoicePanExpression(index, static_cast<float>(pan)))
            return false;

        double brightness = 0.0;
        if (pendingBrightnessFor(event.identity, event.time, brightness) &&
            !VoiceEngine::setVoiceBrightnessExpression(
                index, static_cast<float>(brightness)))
            return false;
        return true;
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
                    voiceVolumeExpressions_[index] = 1.0;
                    voicePerformanceExpressions_[index] = 1.0;
                    voicePressureExpressions_[index] = 0.0;
                }
                continue;
            }

            if (!trackedVoices_[index] || trackedIdentities_[index] != identity) {
                if (!polyphonicState_.startVoice(index, identity))
                    return false;
                trackedVoices_[index] = true;
                trackedIdentities_[index] = identity;
                voiceTuningExpressionSemitones_[index] = 0.0;
                voiceVolumeExpressions_[index] = 1.0;
                voicePerformanceExpressions_[index] = 1.0;
                voicePressureExpressions_[index] = 0.0;
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

            // Fine Tune remains a +/-100-cent parameter domain. CLAP TUNING is
            // a separate statement-of-value offset in semitones and must retain
            // its full +/-120-semitone range rather than inheriting the parameter
            // clamp. The resulting voice-local offset is bounded to +/-12100c.
            const auto parameterCents = std::clamp(base + modulation, -100.0, 100.0);
            const auto effectiveCents =
                parameterCents + voiceTuningExpressionSemitones_[index] * 100.0;
            if (!VoiceEngine::setVoiceFineTuningCents(
                    index, static_cast<float>(effectiveCents)))
                return false;
        }
        return true;
    }

    bool applyTuningExpression(const clap_event_note_expression_t &event) noexcept {
        if (!validExpressionAddress(event))
            return false;
        // The pinned validator deliberately probes TUNING outside the semantic
        // +/-120-semitone domain. A structurally valid but unsupported statement
        // is ignored without poisoning the whole real-time process block.
        if (!std::isfinite(event.value) || event.value < -120.0 || event.value > 120.0)
            return true;
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

    bool applyVolumeExpression(const clap_event_note_expression_t &event) noexcept {
        if (!validExpressionAddress(event))
            return false;
        // clap-validator includes VOLUME=0 in robustness streams although the
        // CLAP semantic domain is 0 < x <= 4. Ignore such values rather than
        // converting a fuzz statement into CLAP_PROCESS_ERROR.
        if (!std::isfinite(event.value) || event.value <= 0.0 || event.value > 4.0)
            return true;
        if (!syncVoices())
            return false;

        const auto address = addressFrom(event);
        for (VoiceAllocator::VoiceIndex index = 0; index < capacity(); ++index) {
            if (!trackedVoices_[index] ||
                !addressMatches(trackedIdentities_[index], address))
                continue;
            voiceVolumeExpressions_[index] = event.value;
            if (!applyVoiceExpressionGain(index))
                return false;
        }
        return storePendingVolume(event);
    }

    bool applyPerformanceExpression(const clap_event_note_expression_t &event) noexcept {
        if (!std::isfinite(event.value) || event.value < 0.0 || event.value > 1.0 ||
            !validExpressionAddress(event))
            return false;
        if (!syncVoices())
            return false;

        const auto address = addressFrom(event);
        for (VoiceAllocator::VoiceIndex index = 0; index < capacity(); ++index) {
            if (!trackedVoices_[index] ||
                !addressMatches(trackedIdentities_[index], address))
                continue;
            voicePerformanceExpressions_[index] = event.value;
            if (!applyVoiceExpressionGain(index))
                return false;
        }
        return storePendingPerformance(event);
    }

    bool applyPressureExpression(const clap_event_note_expression_t &event) noexcept {
        if (!std::isfinite(event.value) || event.value < 0.0 || event.value > 1.0 ||
            !validExpressionAddress(event))
            return false;
        if (!syncVoices())
            return false;

        const auto address = addressFrom(event);
        for (VoiceAllocator::VoiceIndex index = 0; index < capacity(); ++index) {
            if (!trackedVoices_[index] ||
                !addressMatches(trackedIdentities_[index], address))
                continue;
            voicePressureExpressions_[index] = event.value;
            if (!applyVoiceExpressionGain(index))
                return false;
        }
        return storePendingPressure(event);
    }

    bool applyPanExpression(const clap_event_note_expression_t &event) noexcept {
        if (!std::isfinite(event.value) || event.value < 0.0 || event.value > 1.0 ||
            !validExpressionAddress(event))
            return false;
        if (!syncVoices())
            return false;

        const auto address = addressFrom(event);
        for (VoiceAllocator::VoiceIndex index = 0; index < capacity(); ++index) {
            if (!trackedVoices_[index] ||
                !addressMatches(trackedIdentities_[index], address))
                continue;
            if (!VoiceEngine::setVoicePanExpression(
                    index, static_cast<float>(event.value)))
                return false;
        }
        return storePendingPan(event);
    }

    bool applyBrightnessExpression(const clap_event_note_expression_t &event) noexcept {
        if (!std::isfinite(event.value) || event.value < 0.0 || event.value > 1.0 ||
            !validExpressionAddress(event))
            return false;
        if (!syncVoices())
            return false;

        const auto address = addressFrom(event);
        for (VoiceAllocator::VoiceIndex index = 0; index < capacity(); ++index) {
            if (!trackedVoices_[index] ||
                !addressMatches(trackedIdentities_[index], address))
                continue;
            if (!VoiceEngine::setVoiceBrightnessExpression(
                    index, static_cast<float>(event.value)))
                return false;
        }
        return storePendingBrightness(event);
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
            prepareTransientExpressionTime(event.header.time);
            switch (event.expression_id) {
                case CLAP_NOTE_EXPRESSION_TUNING:
                    return applyTuningExpression(event);
                case CLAP_NOTE_EXPRESSION_VOLUME:
                    return applyVolumeExpression(event);
                case CLAP_NOTE_EXPRESSION_PAN:
                    return applyPanExpression(event);
                case CLAP_NOTE_EXPRESSION_EXPRESSION:
                    return applyPerformanceExpression(event);
                case CLAP_NOTE_EXPRESSION_BRIGHTNESS:
                    return applyBrightnessExpression(event);
                case CLAP_NOTE_EXPRESSION_PRESSURE:
                    return applyPressureExpression(event);
                default:
                    return true;
            }
        }

        if (header.type == CLAP_EVENT_PARAM_VALUE) {
            if (header.size < sizeof(clap_event_param_value_t))
                return false;
            const auto &event =
                *reinterpret_cast<const clap_event_param_value_t *>(&header);
            const auto *spec = parameterSpecForId(event.param_id);
            if (!spec)
                return false;
            if (!std::isfinite(event.value))
                return false;

            if (spec->slot == ParameterSlot::MasterGain) {
                if (!isGlobalAddress(event.note_id,
                                     event.port_index,
                                     event.channel,
                                     event.key))
                    return true;
                // A host or fuzzing validator can send a structurally valid
                // statement whose value is outside this parameter's semantic
                // base range. Ignore that statement without mutating the retained
                // base or failing the entire real-time process block.
                if (event.value < spec->minValue || event.value > spec->maxValue)
                    return true;
                masterGainBaseDb_ = event.value;
                return applyMasterGainState();
            }

            if (event.value < spec->minValue || event.value > spec->maxValue)
                return false;
            if (spec->slot != ParameterSlot::FineTuning)
                return true;
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
            if (!std::isfinite(event.amount))
                return false;

            if (spec->slot == ParameterSlot::MasterGain) {
                if (!isGlobalAddress(event.note_id,
                                     event.port_index,
                                     event.channel,
                                     event.key))
                    return true;
                masterGainGlobalModulationDb_ = event.amount;
                return applyMasterGainState();
            }

            if (spec->slot != ParameterSlot::FineTuning)
                return true;
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
    std::array<double, VoiceAllocator::kMaximumVoices> voiceVolumeExpressions_{};
    std::array<double, VoiceAllocator::kMaximumVoices> voicePerformanceExpressions_{};
    std::array<double, VoiceAllocator::kMaximumVoices> voicePressureExpressions_{};
    std::array<PendingTuningExpression, VoiceAllocator::kMaximumVoices>
        pendingTuningExpressions_{};
    std::array<PendingVolumeExpression, VoiceAllocator::kMaximumVoices>
        pendingVolumeExpressions_{};
    std::array<PendingPerformanceExpression, VoiceAllocator::kMaximumVoices>
        pendingPerformanceExpressions_{};
    std::array<PendingPressureExpression, VoiceAllocator::kMaximumVoices>
        pendingPressureExpressions_{};
    std::array<PendingPanExpression, VoiceAllocator::kMaximumVoices>
        pendingPanExpressions_{};
    std::array<PendingBrightnessExpression, VoiceAllocator::kMaximumVoices>
        pendingBrightnessExpressions_{};
    std::uint32_t pendingExpressionTime_ = 0;
    bool pendingExpressionTimeValid_ = false;
    double masterGainBaseDb_ = 0.0;
    double masterGainGlobalModulationDb_ = 0.0;
    double fineTuneBaseCents_ = 0.0;
    double fineTuneGlobalModulationCents_ = 0.0;
};

} // namespace webview_gui::examples::polysynth
