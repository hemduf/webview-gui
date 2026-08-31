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
                            CLAP_NOTE_EXPRESSION_EXPRESSION) noexcept {
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

    std::array<clap_event_note_t, 12> notes{};
    std::array<clap_event_note_expression_t, 12> expressions{};
    std::array<const clap_event_header_t *, 12> headers{};
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
    // EXPRESSION is an absolute normalized per-note performance control. This
    // reference maps it to a post-filter voice gain so an event at sample N is
    // audible starting exactly at N without perturbing oscillator/filter history.
    ParameterVoiceEngine baselineEngine;
    ParameterVoiceEngine expressionEngine;
    if (!configure(baselineEngine, 2) || !configure(expressionEngine, 2))
        return 1;

    InputEvents baselineEvents;
    InputEvents expressionEvents;
    if (!baselineEvents.pushNote(0, 601, 69) ||
        !expressionEvents.pushNote(0, 601, 69) ||
        !expressionEvents.pushExpression(16, 0.25, 601, 69))
        return 2;

    Buffer baselineLeft{};
    Buffer baselineRight{};
    Buffer expressionLeft{};
    Buffer expressionRight{};
    if (!render(baselineEngine, baselineEvents, baselineLeft, baselineRight) ||
        !render(expressionEngine, expressionEvents, expressionLeft, expressionRight))
        return 3;

    for (std::uint32_t frame = 0; frame < 16; ++frame) {
        if (!nearlyEqual(expressionLeft[frame], baselineLeft[frame])) {
            std::cerr << "EXPRESSION changed output before its sample timestamp\n";
            return 4;
        }
    }
    for (std::uint32_t frame = 16; frame < kFrames; ++frame) {
        if (!nearlyEqual(expressionLeft[frame], baselineLeft[frame] * 0.25f)) {
            std::cerr << "EXPRESSION was not applied as sample-accurate post-filter gain\n";
            return 5;
        }
    }

    // Full CLAP note addressing must isolate overlapping same-key generations.
    ParameterVoiceEngine targetedEngine;
    if (!configure(targetedEngine, 4))
        return 6;
    InputEvents targetedEvents;
    if (!targetedEvents.pushNote(0, 601, 69) ||
        !targetedEvents.pushNote(0, 602, 69) ||
        !targetedEvents.pushExpression(16, 0.25, 601, 69))
        return 7;
    Buffer targetedLeft{};
    Buffer targetedRight{};
    if (!render(targetedEngine, targetedEvents, targetedLeft, targetedRight))
        return 8;
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        const float expected = baselineLeft[frame] + expressionLeft[frame];
        if (!nearlyEqual(targetedLeft[frame], expected)) {
            std::cerr << "targeted EXPRESSION leaked across same-key voices or missed its target\n";
            return 9;
        }
    }

    // An expression arriving before NOTE_ON at the same sample must affect every
    // generated sample exactly like the inverse host ordering.
    ParameterVoiceEngine beforeNoteEngine;
    ParameterVoiceEngine afterNoteEngine;
    if (!configure(beforeNoteEngine, 2) || !configure(afterNoteEngine, 2))
        return 10;
    InputEvents beforeNoteEvents;
    InputEvents afterNoteEvents;
    if (!beforeNoteEvents.pushExpression(0, 0.4, 701, 69) ||
        !beforeNoteEvents.pushNote(0, 701, 69) ||
        !afterNoteEvents.pushNote(0, 701, 69) ||
        !afterNoteEvents.pushExpression(0, 0.4, 701, 69))
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
            std::cerr << "same-sample EXPRESSION before NOTE_ON was not applied to the new voice\n";
            return 13;
        }
    }

    // VOLUME and EXPRESSION are independent note-expression dimensions and must
    // compose rather than overwrite one another: 2.0 * 0.5 == unity.
    ParameterVoiceEngine composedEngine;
    if (!configure(composedEngine, 2))
        return 14;
    InputEvents composedEvents;
    if (!composedEvents.pushNote(0, 801, 69) ||
        !composedEvents.pushExpression(0, 2.0, 801, 69, CLAP_NOTE_EXPRESSION_VOLUME) ||
        !composedEvents.pushExpression(0, 0.5, 801, 69))
        return 15;
    Buffer composedLeft{};
    Buffer composedRight{};
    if (!render(composedEngine, composedEvents, composedLeft, composedRight))
        return 16;
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        if (!nearlyEqual(composedLeft[frame], baselineLeft[frame])) {
            std::cerr << "VOLUME and EXPRESSION did not compose independently\n";
            return 17;
        }
    }

    // Generation reuse must restore the default EXPRESSION value of 1.0.
    ParameterVoiceEngine reusedEngine;
    ParameterVoiceEngine freshEngine;
    if (!configure(reusedEngine, 1) || !configure(freshEngine, 1))
        return 18;
    InputEvents reusedEvents;
    InputEvents freshEvents;
    if (!reusedEvents.pushNote(0, 901, 69) ||
        !reusedEvents.pushExpression(0, 0.2, 901, 69) ||
        !reusedEvents.pushNote(8, 901, 69, CLAP_EVENT_NOTE_CHOKE) ||
        !reusedEvents.pushNote(8, 902, 69) ||
        !freshEvents.pushNote(8, 902, 69))
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
            std::cerr << "EXPRESSION survived voice-generation reuse\n";
            return 21;
        }
    }

    // REVIEW RED: CLAP explicitly permits EXPRESSION=0. It must produce an exact
    // voice mute while preserving the voice lifecycle for later expression events.
    ParameterVoiceEngine zeroEngine;
    if (!configure(zeroEngine, 1))
        return 22;
    InputEvents zeroEvents;
    if (!zeroEvents.pushNote(0, 1000, 69) ||
        !zeroEvents.pushExpression(8, 0.0, 1000, 69))
        return 23;
    Buffer zeroLeft{};
    Buffer zeroRight{};
    if (!render(zeroEngine, zeroEvents, zeroLeft, zeroRight)) {
        std::cerr << "legal EXPRESSION=0 was rejected\n";
        return 24;
    }
    for (std::uint32_t frame = 8; frame < kFrames; ++frame) {
        if (zeroLeft[frame] != 0.0f || zeroRight[frame] != 0.0f) {
            std::cerr << "EXPRESSION=0 did not mute the voice exactly\n";
            return 25;
        }
    }

    // The pinned CLAP domain for EXPRESSION is [0, 1]. Reject non-finite/out of
    // range values instead of letting them poison persistent RT voice state.
    ParameterVoiceEngine invalidEngine;
    if (!configure(invalidEngine, 1))
        return 26;
    InputEvents invalidEvents;
    if (!invalidEvents.pushNote(0, 1001, 69) ||
        !invalidEvents.pushExpression(1, 1.01, 1001, 69))
        return 27;
    Buffer invalidLeft{};
    Buffer invalidRight{};
    if (render(invalidEngine, invalidEvents, invalidLeft, invalidRight)) {
        std::cerr << "out-of-range EXPRESSION was accepted\n";
        return 28;
    }

    return 0;
}
