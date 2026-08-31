#include "polysynth_parameter_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
using webview_gui::examples::polysynth::ParameterSlot;
using webview_gui::examples::polysynth::ParameterVoiceEngine;

constexpr clap_id kPanParameterId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::Pan);
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

    bool pushPanValue(std::uint32_t time,
                      double value,
                      std::int32_t noteId = -1,
                      std::int16_t portIndex = -1,
                      std::int16_t channel = -1,
                      std::int16_t key = -1) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = values[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kPanParameterId;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.value = value;
        headers[count++] = &event.header;
        return true;
    }

    bool pushPanMod(std::uint32_t time,
                    double amount,
                    std::int32_t noteId = -1,
                    std::int16_t portIndex = -1,
                    std::int16_t channel = -1,
                    std::int16_t key = -1) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = mods[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_MOD;
        event.param_id = kPanParameterId;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.amount = amount;
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
    std::array<clap_event_param_value_t, 8> values{};
    std::array<clap_event_param_mod_t, 8> mods{};
    std::array<const clap_event_header_t *, 8> headers{};
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

bool configure(ParameterVoiceEngine &engine) noexcept {
    return engine.configure(4, kSampleRate, 16) &&
           engine.setAmpEnvelope(0, 0, 1.0f, 16);
}

template <std::size_t Frames>
bool render(ParameterVoiceEngine &engine,
            InputEvents &events,
            std::array<float, Frames> &left,
            std::array<float, Frames> &right) noexcept {
    auto noteEnd = [](const clap_event_note_t &) noexcept {};
    return engine.process(&events.input,
                          static_cast<std::uint32_t>(Frames),
                          left.data(),
                          right.data(),
                          noteEnd);
}
}

int main() {
    ParameterVoiceEngine engine;
    if (!configure(engine))
        return 1;

    InputEvents events;
    if (!events.pushNote(0, 201, 60) ||
        !events.pushNote(0, 202, 60) ||
        !events.pushPan(8, 0.0, 201, 60))
        return 2;

    std::array<float, 32> left{};
    std::array<float, 32> right{};
    if (!render(engine, events, left, right))
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

    // Pan is also a polyphonically addressable CLAP parameter. A global base
    // change must affect an already-active voice at the event sample and remain
    // visible through the retained base-value query.
    ParameterVoiceEngine globalPan;
    if (!configure(globalPan))
        return 5;
    InputEvents globalEvents;
    if (!globalEvents.pushNote(0, 301, 60) ||
        !globalEvents.pushPanValue(8, 1.0))
        return 6;
    std::array<float, 32> globalLeft{};
    std::array<float, 32> globalRight{};
    if (!render(globalPan, globalEvents, globalLeft, globalRight))
        return 7;
    for (std::uint32_t frame = 0; frame < globalLeft.size(); ++frame) {
        const float sample = sineAt(0.25 + static_cast<double>(frame) * increment);
        const float expectedLeft = frame < 8 ? sample : 0.0f;
        const float expectedRight = sample;
        if (std::fabs(globalLeft[frame] - expectedLeft) > 1.0e-5f ||
            std::fabs(globalRight[frame] - expectedRight) > 1.0e-5f) {
            std::cerr << "global Pan PARAM_VALUE was not applied at its exact sample boundary\n";
            return 8;
        }
    }
    double panBase = -2.0;
    if (!globalPan.parameterBaseValue(kPanParameterId, panBase) || panBase != 1.0) {
        std::cerr << "global Pan PARAM_VALUE was not retained as the host-visible base\n";
        return 9;
    }

    // The pinned CLAP note-expression contract defines PAN as an offset from the
    // non-note-expression voice default. Compose the signed Pan parameter base +
    // modulation first, then add the expression's center-relative offset. The
    // polyphonic value/modulation pair must affect only the addressed voice and
    // must not overwrite the global host-visible base.
    ParameterVoiceEngine composedPan;
    if (!configure(composedPan))
        return 10;
    InputEvents composedEvents;
    if (!composedEvents.pushNote(0, 401, 60) ||
        !composedEvents.pushNote(0, 402, 60) ||
        !composedEvents.pushPanValue(8, 0.25, 401, 0, 0, 60) ||
        !composedEvents.pushPanMod(8, 0.25, 401, 0, 0, 60) ||
        !composedEvents.pushPan(8, 0.0, 401, 60))
        return 11;
    std::array<float, 32> composedLeft{};
    std::array<float, 32> composedRight{};
    if (!render(composedPan, composedEvents, composedLeft, composedRight))
        return 12;
    for (std::uint32_t frame = 0; frame < composedLeft.size(); ++frame) {
        const float sample = sineAt(0.25 + static_cast<double>(frame) * increment);
        const float expectedLeft = sample * 2.0f;
        const float expectedRight = sample * (frame < 8 ? 2.0f : 1.5f);
        if (std::fabs(composedLeft[frame] - expectedLeft) > 1.0e-5f ||
            std::fabs(composedRight[frame] - expectedRight) > 1.0e-5f) {
            std::cerr << "Pan base/modulation/note-expression composition was not isolated or sample-accurate\n";
            return 13;
        }
    }
    panBase = -2.0;
    if (!composedPan.parameterBaseValue(kPanParameterId, panBase) || panBase != 0.0) {
        std::cerr << "targeted Pan PARAM_VALUE overwrote the global host-visible base\n";
        return 14;
    }

    return 0;
}
