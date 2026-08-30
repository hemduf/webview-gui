#include "polysynth_parameter_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
using webview_gui::examples::polysynth::ParameterVoiceEngine;

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

    bool pushTuning(std::uint32_t time,
                    double semitones,
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
        event.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.value = semitones;
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

    std::array<clap_event_note_t, 80> notes{};
    std::array<clap_event_note_expression_t, 80> expressions{};
    std::array<const clap_event_header_t *, 80> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};
}

int main() {
    ParameterVoiceEngine engine;
    if (!engine.configure(4, 48000.0, 16) ||
        !engine.setAmpEnvelope(0, 0, 1.0f, 16))
        return 1;

    InputEvents events;
    if (!events.pushNote(0, 301, 60))
        return 2;

    // Pending expression state only exists to bridge earlier same-sample events
    // into a later NOTE_ON at that same timestamp. Older timestamps must not
    // consume fixed RT capacity for the rest of the block. 65 successive tuning
    // statements intentionally exceed the 64-voice cache size while never
    // requiring more than one pending address at a time.
    for (std::uint32_t time = 0; time < 65; ++time) {
        const double tuning = time == 64 ? 1.0 : 0.5;
        if (!events.pushTuning(time, tuning, 301, 60))
            return 3;
    }

    std::array<float, 96> left{};
    std::array<float, 96> right{};
    auto noteEnd = [](const clap_event_note_t &) noexcept {};
    if (!engine.process(&events.input,
                        static_cast<std::uint32_t>(left.size()),
                        left.data(),
                        right.data(),
                        noteEnd)) {
        std::cerr << "note-expression pending cache retained expired timestamps\n";
        return 4;
    }

    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        if (!std::isfinite(left[frame]) || !std::isfinite(right[frame])) {
            std::cerr << "long note-expression stream produced non-finite audio\n";
            return 5;
        }
    }

    return 0;
}
