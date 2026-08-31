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

    bool pushBrightness(std::uint32_t time,
                        double brightness,
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
        event.expression_id = CLAP_NOTE_EXPRESSION_BRIGHTNESS;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.value = brightness;
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

    std::array<clap_event_note_t, 8> notes{};
    std::array<clap_event_note_expression_t, 8> expressions{};
    std::array<const clap_event_header_t *, 8> headers{};
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
           engine.setFilter(120.0f, 0.0f);
}

double energy(const Buffer &buffer, std::uint32_t begin) noexcept {
    double result = 0.0;
    for (std::uint32_t frame = begin; frame < buffer.size(); ++frame)
        result += std::fabs(static_cast<double>(buffer[frame]));
    return result;
}

bool nearlyEqual(float a, float b, float tolerance = 2.0e-5f) noexcept {
    return std::fabs(a - b) <= tolerance;
}
}

int main() {
    ParameterVoiceEngine baselineEngine;
    ParameterVoiceEngine brightEngine;
    ParameterVoiceEngine targetedEngine;
    if (!configure(baselineEngine, 2) ||
        !configure(brightEngine, 2) ||
        !configure(targetedEngine, 4))
        return 1;

    InputEvents baselineEvents;
    InputEvents brightEvents;
    InputEvents targetedEvents;
    if (!baselineEvents.pushNote(0, 301, 84) ||
        !brightEvents.pushNote(0, 301, 84) ||
        !brightEvents.pushBrightness(16, 1.0, 301, 84) ||
        !targetedEvents.pushNote(0, 301, 84) ||
        !targetedEvents.pushNote(0, 302, 84) ||
        !targetedEvents.pushBrightness(16, 1.0, 301, 84))
        return 2;

    Buffer baselineLeft{};
    Buffer baselineRight{};
    Buffer brightLeft{};
    Buffer brightRight{};
    Buffer targetedLeft{};
    Buffer targetedRight{};
    if (!render(baselineEngine, baselineEvents, baselineLeft, baselineRight) ||
        !render(brightEngine, brightEvents, brightLeft, brightRight) ||
        !render(targetedEngine, targetedEvents, targetedLeft, targetedRight))
        return 3;

    for (std::uint32_t frame = 0; frame < 16; ++frame) {
        if (!nearlyEqual(brightLeft[frame], baselineLeft[frame])) {
            std::cerr << "BRIGHTNESS changed output before its sample timestamp\n";
            return 4;
        }
    }

    if (!(energy(brightLeft, 24) > energy(baselineLeft, 24) * 2.0)) {
        std::cerr << "BRIGHTNESS did not increase the audible high-frequency content\n";
        return 5;
    }

    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        const float expected = baselineLeft[frame] + brightLeft[frame];
        if (!nearlyEqual(targetedLeft[frame], expected)) {
            std::cerr << "targeted BRIGHTNESS leaked across same-key voices or missed its target\n";
            return 6;
        }
    }

    ParameterVoiceEngine beforeNoteEngine;
    ParameterVoiceEngine afterNoteEngine;
    if (!configure(beforeNoteEngine, 2) || !configure(afterNoteEngine, 2))
        return 7;

    InputEvents beforeNoteEvents;
    InputEvents afterNoteEvents;
    if (!beforeNoteEvents.pushBrightness(0, 1.0, 401, 84) ||
        !beforeNoteEvents.pushNote(0, 401, 84) ||
        !afterNoteEvents.pushNote(0, 401, 84) ||
        !afterNoteEvents.pushBrightness(0, 1.0, 401, 84))
        return 8;

    Buffer beforeLeft{};
    Buffer beforeRight{};
    Buffer afterLeft{};
    Buffer afterRight{};
    if (!render(beforeNoteEngine, beforeNoteEvents, beforeLeft, beforeRight) ||
        !render(afterNoteEngine, afterNoteEvents, afterLeft, afterRight))
        return 9;

    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        if (!nearlyEqual(beforeLeft[frame], afterLeft[frame])) {
            std::cerr << "same-sample BRIGHTNESS before NOTE_ON was not applied to the new voice\n";
            return 10;
        }
    }

    ParameterVoiceEngine reusedEngine;
    ParameterVoiceEngine freshEngine;
    if (!configure(reusedEngine, 1) || !configure(freshEngine, 1))
        return 11;

    InputEvents reusedEvents;
    InputEvents freshEvents;
    if (!reusedEvents.pushNote(0, 501, 84) ||
        !reusedEvents.pushBrightness(0, 1.0, 501, 84) ||
        !reusedEvents.pushNote(8, 501, 84, CLAP_EVENT_NOTE_CHOKE) ||
        !reusedEvents.pushNote(8, 502, 84) ||
        !freshEvents.pushNote(8, 502, 84))
        return 12;

    Buffer reusedLeft{};
    Buffer reusedRight{};
    Buffer freshLeft{};
    Buffer freshRight{};
    if (!render(reusedEngine, reusedEvents, reusedLeft, reusedRight) ||
        !render(freshEngine, freshEvents, freshLeft, freshRight))
        return 13;

    for (std::uint32_t frame = 8; frame < kFrames; ++frame) {
        if (!nearlyEqual(reusedLeft[frame], freshLeft[frame])) {
            std::cerr << "BRIGHTNESS survived voice-generation reuse\n";
            return 14;
        }
    }

    return 0;
}
