#include "polysynth_parameter_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
using webview_gui::examples::polysynth::ParameterVoiceEngine;

constexpr double kSampleRate = 48000.0;
constexpr std::uint32_t kFrames = 64;

struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(std::uint32_t time,
                  std::int32_t noteId,
                  std::int16_t key,
                  std::uint16_t type = CLAP_EVENT_NOTE_ON) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = notes[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = 1.0;
        headers[count++] = &event.header;
        return true;
    }

    bool pushExpression(std::uint32_t time,
                        double value,
                        std::int32_t noteId,
                        std::int16_t key,
                        clap_note_expression expressionId =
                            CLAP_NOTE_EXPRESSION_PRESSURE) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = expressions[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
        event.expression_id = expressionId;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.value = value;
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

    std::array<clap_event_note_t, 16> notes{};
    std::array<clap_event_note_expression_t, 16> expressions{};
    std::array<const clap_event_header_t *, 16> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

using Buffer = std::array<float, kFrames>;

bool render(ParameterVoiceEngine &engine,
            InputEvents &events,
            Buffer &left,
            Buffer &right) noexcept {
    auto noteEnd = [](const clap_event_note_t &) noexcept {};
    return engine.process(&events.input,
                          static_cast<std::uint32_t>(left.size()),
                          left.data(),
                          right.data(),
                          noteEnd);
}

bool configure(ParameterVoiceEngine &engine, std::size_t voices) noexcept {
    return engine.configure(voices, kSampleRate, 32) &&
           engine.setAmpEnvelope(0, 0, 1.0f, 32) &&
           engine.setFilter(12000.0f, 0.0f);
}

bool nearlyEqual(float a, float b, float tolerance = 2.0e-5f) noexcept {
    return std::fabs(a - b) <= tolerance;
}
}

int main() {
    // PRESSURE is normalized [0, 1]. This reference maps it to an independent
    // post-filter performance gain of 1x..2x, so pressure=0 preserves the voice
    // default and pressure=1 adds +6.02 dB without perturbing oscillator/filter
    // history. A timestamp-N event must therefore change output starting at N.
    ParameterVoiceEngine baselineEngine;
    ParameterVoiceEngine pressureEngine;
    if (!configure(baselineEngine, 2) || !configure(pressureEngine, 2))
        return 1;

    InputEvents baselineEvents;
    InputEvents pressureEvents;
    if (!baselineEvents.pushNote(0, 1101, 69) ||
        !pressureEvents.pushNote(0, 1101, 69) ||
        !pressureEvents.pushExpression(16, 1.0, 1101, 69))
        return 2;

    Buffer baselineLeft{};
    Buffer baselineRight{};
    Buffer pressureLeft{};
    Buffer pressureRight{};
    if (!render(baselineEngine, baselineEvents, baselineLeft, baselineRight) ||
        !render(pressureEngine, pressureEvents, pressureLeft, pressureRight))
        return 3;

    for (std::uint32_t frame = 0; frame < 16; ++frame) {
        if (!nearlyEqual(pressureLeft[frame], baselineLeft[frame])) {
            std::cerr << "PRESSURE changed output before its sample timestamp\n";
            return 4;
        }
    }
    for (std::uint32_t frame = 16; frame < kFrames; ++frame) {
        if (!nearlyEqual(pressureLeft[frame], baselineLeft[frame] * 2.0f)) {
            std::cerr << "PRESSURE was not applied as sample-accurate post-filter performance gain\n";
            return 5;
        }
    }

    // Full CLAP note addressing must isolate overlapping same-key generations.
    ParameterVoiceEngine targetedEngine;
    if (!configure(targetedEngine, 4))
        return 6;
    InputEvents targetedEvents;
    if (!targetedEvents.pushNote(0, 1101, 69) ||
        !targetedEvents.pushNote(0, 1102, 69) ||
        !targetedEvents.pushExpression(16, 1.0, 1101, 69))
        return 7;
    Buffer targetedLeft{};
    Buffer targetedRight{};
    if (!render(targetedEngine, targetedEvents, targetedLeft, targetedRight))
        return 8;
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        const float expected = baselineLeft[frame] + pressureLeft[frame];
        if (!nearlyEqual(targetedLeft[frame], expected)) {
            std::cerr << "targeted PRESSURE leaked across same-key voices or missed its target\n";
            return 9;
        }
    }

    // Same-sample expression-before-NOTE_ON must affect the new generation before
    // its first rendered sample, independent of host ordering at that timestamp.
    ParameterVoiceEngine beforeNoteEngine;
    ParameterVoiceEngine afterNoteEngine;
    if (!configure(beforeNoteEngine, 2) || !configure(afterNoteEngine, 2))
        return 10;
    InputEvents beforeNoteEvents;
    InputEvents afterNoteEvents;
    if (!beforeNoteEvents.pushExpression(0, 0.75, 1201, 69) ||
        !beforeNoteEvents.pushNote(0, 1201, 69) ||
        !afterNoteEvents.pushNote(0, 1201, 69) ||
        !afterNoteEvents.pushExpression(0, 0.75, 1201, 69))
        return 11;
    Buffer beforeLeft{};
    Buffer beforeRight{};
    Buffer afterLeft{};
    Buffer afterRight{};
    if (!render(beforeNoteEngine, beforeNoteEvents, beforeLeft, beforeRight) ||
        !render(afterNoteEngine, afterNoteEvents, afterLeft, afterRight))
        return 12;
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        if (!nearlyEqual(beforeLeft[frame], afterLeft[frame])) {
            std::cerr << "same-sample PRESSURE before NOTE_ON was not applied to the new voice\n";
            return 13;
        }
    }

    // PRESSURE must compose with VOLUME and EXPRESSION rather than overwrite
    // either dimension: 2.0 * 0.25 * (1 + 1.0) == unity.
    ParameterVoiceEngine composedEngine;
    if (!configure(composedEngine, 2))
        return 14;
    InputEvents composedEvents;
    if (!composedEvents.pushNote(0, 1301, 69) ||
        !composedEvents.pushExpression(0, 2.0, 1301, 69, CLAP_NOTE_EXPRESSION_VOLUME) ||
        !composedEvents.pushExpression(0, 0.25, 1301, 69, CLAP_NOTE_EXPRESSION_EXPRESSION) ||
        !composedEvents.pushExpression(0, 1.0, 1301, 69))
        return 15;
    Buffer composedLeft{};
    Buffer composedRight{};
    if (!render(composedEngine, composedEvents, composedLeft, composedRight))
        return 16;
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        if (!nearlyEqual(composedLeft[frame], baselineLeft[frame])) {
            std::cerr << "PRESSURE did not compose independently with VOLUME and EXPRESSION\n";
            return 17;
        }
    }

    // A new generation in a recycled slot must restore the implicit pressure=0
    // default instead of inheriting performance state from the previous voice.
    ParameterVoiceEngine reusedEngine;
    ParameterVoiceEngine freshEngine;
    if (!configure(reusedEngine, 1) || !configure(freshEngine, 1))
        return 18;
    InputEvents reusedEvents;
    InputEvents freshEvents;
    if (!reusedEvents.pushNote(0, 1401, 69) ||
        !reusedEvents.pushExpression(0, 1.0, 1401, 69) ||
        !reusedEvents.pushNote(8, 1401, 69, CLAP_EVENT_NOTE_CHOKE) ||
        !reusedEvents.pushNote(8, 1402, 69) ||
        !freshEvents.pushNote(8, 1402, 69))
        return 19;
    Buffer reusedLeft{};
    Buffer reusedRight{};
    Buffer freshLeft{};
    Buffer freshRight{};
    if (!render(reusedEngine, reusedEvents, reusedLeft, reusedRight) ||
        !render(freshEngine, freshEvents, freshLeft, freshRight))
        return 20;
    for (std::uint32_t frame = 8; frame < kFrames; ++frame) {
        if (!nearlyEqual(reusedLeft[frame], freshLeft[frame])) {
            std::cerr << "PRESSURE survived voice-generation reuse\n";
            return 21;
        }
    }

    // The pinned CLAP domain is [0, 1]. Reject invalid input before it can poison
    // the fixed per-voice RT state.
    ParameterVoiceEngine invalidEngine;
    if (!configure(invalidEngine, 1))
        return 22;
    InputEvents invalidEvents;
    if (!invalidEvents.pushNote(0, 1501, 69) ||
        !invalidEvents.pushExpression(1, 1.01, 1501, 69))
        return 23;
    Buffer invalidLeft{};
    Buffer invalidRight{};
    if (render(invalidEngine, invalidEvents, invalidLeft, invalidRight)) {
        std::cerr << "out-of-range PRESSURE was accepted\n";
        return 24;
    }

    return 0;
}
