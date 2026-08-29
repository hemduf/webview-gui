#pragma once

#include "polysynth_parameters.h"
#include "polysynth_voice_engine.h"

#include <clap/events.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace webview_gui::examples::polysynth {

// Host-facing parameter adapter for the #31 reference voice engine. The adapter
// owns persistent CLAP base values separately from process-time modulation and
// deliberately leaves VoiceEngine free of host protocol state.
//
// This first TDD increment handles the globally addressed Fine Tune events which
// precede a same-sample NOTE_ON. Later increments extend the same ordered event
// path to active-voice polyphonic modulation and note expressions. Unsupported
// parameter timing fails closed instead of silently quantizing it.
class ParameterVoiceEngine : public VoiceEngine {
public:
    bool configure(std::size_t requestedVoices,
                   double sampleRate,
                   std::uint32_t releaseSamples) noexcept {
        if (!VoiceEngine::configure(requestedVoices, sampleRate, releaseSamples))
            return false;
        fineTuneBaseCents_ = 0.0;
        fineTuneModulationCents_ = 0.0;
        return VoiceEngine::setFineTuningCents(0.0f);
    }

    bool setFineTuningCents(float cents) noexcept {
        if (!VoiceEngine::setFineTuningCents(cents))
            return false;
        fineTuneBaseCents_ = static_cast<double>(cents);
        fineTuneModulationCents_ = 0.0;
        return true;
    }

    [[nodiscard]] bool parameterBaseValue(clap_id id, double &value) const noexcept {
        const auto *spec = parameterSpecForId(id);
        if (!spec)
            return false;
        if (spec->slot != ParameterSlot::FineTuning)
            return false;
        value = fineTuneBaseCents_;
        return true;
    }

    void reset() noexcept {
        VoiceEngine::reset();
        fineTuneModulationCents_ = 0.0;
        (void)VoiceEngine::setFineTuningCents(static_cast<float>(fineTuneBaseCents_));
    }

    template <typename NoteEndSink>
    bool process(const clap_input_events_t *events,
                 std::uint32_t framesCount,
                 float *left,
                 float *right,
                 NoteEndSink &noteEndSink) noexcept {
        if (!applyLeadingParameterEvents(events))
            return false;
        return VoiceEngine::process(events, framesCount, left, right, noteEndSink);
    }

private:
    bool applyLeadingParameterEvents(const clap_input_events_t *events) noexcept {
        if (!events)
            return true;
        if (!events->size || !events->get)
            return false;

        const auto eventCount = events->size(events);
        bool noteSeenAtZero = false;
        for (std::uint32_t index = 0; index < eventCount; ++index) {
            const auto *header = events->get(events, index);
            if (!header || header->size < sizeof(clap_event_header_t))
                return false;
            if (header->time > 0)
                break;
            if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
                continue;

            if (header->type == CLAP_EVENT_NOTE_ON ||
                header->type == CLAP_EVENT_NOTE_OFF ||
                header->type == CLAP_EVENT_NOTE_CHOKE) {
                noteSeenAtZero = true;
                continue;
            }

            if (header->type != CLAP_EVENT_PARAM_VALUE &&
                header->type != CLAP_EVENT_PARAM_MOD)
                continue;

            // A parameter event after a note at the same timestamp must not be
            // moved in front of that note. Active-voice handling is introduced by
            // the next TDD increment, so reject this ordering for now.
            if (noteSeenAtZero)
                return false;

            if (header->type == CLAP_EVENT_PARAM_VALUE) {
                if (header->size < sizeof(clap_event_param_value_t))
                    return false;
                const auto &event =
                    *reinterpret_cast<const clap_event_param_value_t *>(header);
                if (!isGlobalFineTune(event.param_id,
                                      event.note_id,
                                      event.port_index,
                                      event.channel,
                                      event.key) ||
                    !std::isfinite(event.value) ||
                    event.value < -100.0 || event.value > 100.0)
                    return false;
                fineTuneBaseCents_ = event.value;
            } else {
                if (header->size < sizeof(clap_event_param_mod_t))
                    return false;
                const auto &event =
                    *reinterpret_cast<const clap_event_param_mod_t *>(header);
                if (!isGlobalFineTune(event.param_id,
                                      event.note_id,
                                      event.port_index,
                                      event.channel,
                                      event.key) ||
                    !std::isfinite(event.amount))
                    return false;
                fineTuneModulationCents_ = event.amount;
            }

            const auto effective = std::clamp(fineTuneBaseCents_ +
                                                  fineTuneModulationCents_,
                                              -100.0,
                                              100.0);
            if (!VoiceEngine::setFineTuningCents(static_cast<float>(effective)))
                return false;
        }
        return true;
    }

    static bool isGlobalFineTune(clap_id id,
                                 std::int32_t noteId,
                                 std::int16_t portIndex,
                                 std::int16_t channel,
                                 std::int16_t key) noexcept {
        const auto *spec = parameterSpecForId(id);
        return spec && spec->slot == ParameterSlot::FineTuning &&
               noteId == -1 && portIndex == -1 && channel == -1 && key == -1;
    }

    double fineTuneBaseCents_ = 0.0;
    double fineTuneModulationCents_ = 0.0;
};

} // namespace webview_gui::examples::polysynth
