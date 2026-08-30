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
    auto noteEnd = [](const clap_event_note_t &) noexcept {};

    // Hosts and validation tools can send semantically invalid expression values
    // while fuzzing robustness. The event header is structurally valid, so the
    // plug-in must ignore the unsupported statement rather than fail the entire
    // real-time process block with CLAP_PROCESS_ERROR.
    ParameterVoiceEngine robustnessEngine;
    if (!robustnessEngine.configure(4, 48000.0, 16) ||
        !robustnessEngine.setAmpEnvelope(0, 0, 1.0f, 16))
        return 1;

    InputEvents invalidSemanticExpression;
    if (!invalidSemanticExpression.pushNote(0, 901, 60) ||
        !invalidSemanticExpression.pushTuning(1, 128.0, 901, 60))
        return 2;

    std::array<float, 16> robustnessLeft{};
    std::array<float, 16> robustnessRight{};
    if (!robustnessEngine.process(&invalidSemanticExpression.input,
                                  static_cast<std::uint32_t>(robustnessLeft.size()),
                                  robustnessLeft.data(),
                                  robustnessRight.data(),
                                  noteEnd)) {
        std::cerr << "semantically invalid note expression failed the process block\n";
        return 3;
    }
    for (std::size_t frame = 0; frame < robustnessLeft.size(); ++frame) {
        if (!std::isfinite(robustnessLeft[frame]) ||
            !std::isfinite(robustnessRight[frame])) {
            std::cerr << "semantic note-expression robustness produced non-finite audio\n";
            return 4;
        }
    }

    ParameterVoiceEngine engine;
    if (!engine.configure(4, 48000.0, 16) ||
        !engine.setAmpEnvelope(0, 0, 1.0f, 16))
        return 5;

    InputEvents events;
    if (!events.pushNote(0, 301, 60))
        return 6;

    // Pending expression state only exists to bridge earlier same-sample events
    // into a later NOTE_ON at that same timestamp. Older timestamps must not
    // consume fixed RT capacity for the rest of the block. 65 successive tuning
    // statements intentionally exceed the 64-voice cache size while never
    // requiring more than one pending address at a time.
    for (std::uint32_t time = 0; time < 65; ++time) {
        const double tuning = time == 64 ? 1.0 : 0.5;
        if (!events.pushTuning(time, tuning, 301, 60))
            return 7;
    }

    std::array<float, 96> left{};
    std::array<float, 96> right{};
    if (!engine.process(&events.input,
                        static_cast<std::uint32_t>(left.size()),
                        left.data(),
                        right.data(),
                        noteEnd)) {
        std::cerr << "note-expression pending cache retained expired timestamps\n";
        return 8;
    }

    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        if (!std::isfinite(left[frame]) || !std::isfinite(right[frame])) {
            std::cerr << "long note-expression stream produced non-finite audio\n";
            return 9;
        }
    }

    return 0;
}
