#include "polysynth_parameter_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
using webview_gui::examples::polysynth::ParameterVoiceEngine;

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kSampleRate = 48000.0;

struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(std::uint32_t time, std::int32_t noteId, std::int16_t key) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = notes[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = 1.0;
        headers[count++] = &event.header;
        return true;
    }

    bool pushPan(std::uint32_t time,
                 double pan,
                 std::int32_t noteId,
                 std::int16_t key) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = expressions[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
        event.expression_id = CLAP_NOTE_EXPRESSION_PAN;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.value = pan;
        headers[count++] = &event.header;
        return true;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        return events && events->ctx
                   ? static_cast<const InputEvents *>(events->ctx)->count
                   : 0;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(events->ctx);
        return index < self.count ? self.headers[index] : nullptr;
    }

    std::array<clap_event_note_t, 4> notes{};
    std::array<clap_event_note_expression_t, 4> expressions{};
    std::array<const clap_event_header_t *, 4> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

double incrementForKey(double key) noexcept {
    return (440.0 * std::exp2((key - 69.0) / 12.0)) / kSampleRate;
}

float sineAt(double phase) noexcept {
    phase -= std::floor(phase);
    return static_cast<float>(std::sin(phase * kTwoPi));
}
}

int main() {
    ParameterVoiceEngine engine;
    if (!engine.configure(4, kSampleRate, 16) ||
        !engine.setAmpEnvelope(0, 0, 1.0f, 16))
        return 1;

    InputEvents events;
    if (!events.pushNote(0, 201, 60) ||
        !events.pushNote(0, 202, 60) ||
        !events.pushPan(8, 0.0, 201, 60))
        return 2;

    std::array<float, 32> left{};
    std::array<float, 32> right{};
    auto noteEnd = [](const clap_event_note_t &) noexcept {};
    if (!engine.process(&events.input,
                        static_cast<std::uint32_t>(left.size()),
                        left.data(),
                        right.data(),
                        noteEnd))
        return 3;

    const double increment = incrementForKey(60.0);
    for (std::uint32_t frame = 0; frame < left.size(); ++frame) {
        const float sample = sineAt(0.25 + static_cast<double>(frame) * increment);
        const float expectedLeft = sample * 2.0f;
        const float expectedRight = sample * (frame < 8 ? 2.0f : 1.0f);
        if (std::fabs(left[frame] - expectedLeft) > 1.0e-5f ||
            std::fabs(right[frame] - expectedRight) > 1.0e-5f) {
            std::cerr << "targeted PAN note expression was not sample-accurate or leaked across voices\n";
            return 4;
        }
    }

    return 0;
}
