#pragma once

#include "gain_processor.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace webview_gui::examples::gain {

inline constexpr clap_id kGainParamId = 0x1000u;
inline constexpr clap_id kBypassParamId = 0x1001u;

class GainEventProcessor {
public:
    [[nodiscard]] GainProcessor &processor() noexcept { return processor_; }
    [[nodiscard]] const GainProcessor &processor() const noexcept { return processor_; }

    bool applyParameterValue(const clap_event_param_value_t &event) noexcept {
        if (!isSupportedValue(event))
            return false;
        applyValue(event);
        return true;
    }

    bool process(const clap_process_t &process) noexcept {
        if (!validateAudio(process))
            return false;

        const auto *events = process.in_events;
        if (events && (!events->size || !events->get))
            return false;

        uint32_t cursor = 0;
        uint32_t previousEventTime = 0;
        bool havePreviousEvent = false;
        const uint32_t eventCount = events ? events->size(events) : 0;

        for (uint32_t index = 0; index < eventCount; ++index) {
            const auto *header = events->get(events, index);
            if (!header || header->size < sizeof(clap_event_header_t))
                return false;
            if (header->time > process.frames_count)
                return false;
            if (havePreviousEvent && header->time < previousEventTime)
                return false;

            previousEventTime = header->time;
            havePreviousEvent = true;

            if (header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
                header->type != CLAP_EVENT_PARAM_VALUE ||
                header->size < sizeof(clap_event_param_value_t))
                continue;

            const auto &event = *reinterpret_cast<const clap_event_param_value_t *>(header);
            if (!isSupportedValue(event))
                continue;

            if (header->time > cursor) {
                if (!processRange(process, cursor, header->time))
                    return false;
                cursor = header->time;
            }

            (void)applyParameterValue(event);
        }

        return processRange(process, cursor, process.frames_count);
    }

private:
    static bool validateAudio(const clap_process_t &process) noexcept {
        if (process.audio_inputs_count != 1 || process.audio_outputs_count != 1 ||
            !process.audio_inputs || !process.audio_outputs)
            return false;

        const auto &input = process.audio_inputs[0];
        const auto &output = process.audio_outputs[0];
        if (input.channel_count != 2 || output.channel_count != 2 ||
            !input.data32 || !output.data32 ||
            !input.data32[0] || !input.data32[1] ||
            !output.data32[0] || !output.data32[1])
            return false;

        return true;
    }

    static bool hasGlobalAddress(const clap_event_param_value_t &event) noexcept {
        return event.note_id == -1 && event.port_index == -1 &&
               event.channel == -1 && event.key == -1;
    }

    static bool isSupportedValue(const clap_event_param_value_t &event) noexcept {
        if (!hasGlobalAddress(event) || !std::isfinite(event.value))
            return false;

        if (event.param_id == kGainParamId)
            return event.value >= GainProcessor::kMinimumGainDb &&
                   event.value <= GainProcessor::kMaximumGainDb;

        if (event.param_id == kBypassParamId)
            return event.value >= 0.0 && event.value <= 1.0;

        return false;
    }

    void applyValue(const clap_event_param_value_t &event) noexcept {
        if (event.param_id == kGainParamId) {
            (void)processor_.setGainDb(event.value);
        } else if (event.param_id == kBypassParamId) {
            processor_.setBypassed(event.value >= 0.5);
        }
    }

    bool processRange(const clap_process_t &process,
                      uint32_t begin,
                      uint32_t end) noexcept {
        if (end < begin)
            return false;
        if (end == begin)
            return true;

        const auto &input = process.audio_inputs[0];
        auto &output = process.audio_outputs[0];
        std::array<const float *, 2> inputs{
            input.data32[0] + begin,
            input.data32[1] + begin,
        };
        std::array<float *, 2> outputs{
            output.data32[0] + begin,
            output.data32[1] + begin,
        };
        return processor_.process(inputs.data(), outputs.data(), end - begin);
    }

    GainProcessor processor_{};
};

} // namespace webview_gui::examples::gain
