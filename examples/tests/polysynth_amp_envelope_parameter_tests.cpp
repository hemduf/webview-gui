#include "polysynth_parameter_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;
using webview_gui::examples::polysynth::ParameterVoiceEngine;
using webview_gui::examples::polysynth::kFirstParameterId;

constexpr clap_id parameterId(ParameterSlot slot) noexcept {
    return kFirstParameterId + static_cast<clap_id>(slot);
}

constexpr clap_id kAmpAttackId = parameterId(ParameterSlot::AmpAttack);
constexpr clap_id kAmpDecayId = parameterId(ParameterSlot::AmpDecay);
constexpr clap_id kAmpSustainId = parameterId(ParameterSlot::AmpSustain);
constexpr clap_id kAmpReleaseId = parameterId(ParameterSlot::AmpRelease);

bool applyValue(ParameterVoiceEngine &engine,
                clap_id paramId,
                double value,
                std::int32_t noteId = -1,
                std::int16_t port = -1,
                std::int16_t channel = -1,
                std::int16_t key = -1) noexcept {
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = paramId;
    event.note_id = noteId;
    event.port_index = port;
    event.channel = channel;
    event.key = key;
    event.value = value;
    return engine.applyParameterFlushEvent(event.header);
}

struct NoteInput {
    NoteInput(std::uint16_t type, std::int32_t noteId, std::int16_t key) noexcept {
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = 1.0;
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        return events && events->ctx ? 1u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx || index != 0)
            return nullptr;
        return &static_cast<const NoteInput *>(events->ctx)->event.header;
    }

    clap_event_note_t event{};
    clap_input_events_t input{};
};

struct NoteEndCapture {
    void operator()(const clap_event_note_t &event) noexcept {
        if (count < events.size())
            events[count++] = event;
    }

    std::array<clap_event_note_t, 4> events{};
    std::size_t count = 0;
};

bool approximately(double actual, double expected, double tolerance = 1.0e-9) noexcept {
    return std::fabs(actual - expected) <= tolerance;
}

template <std::size_t Frames>
bool render(ParameterVoiceEngine &engine,
            const clap_input_events_t *events,
            NoteEndCapture &noteEnds) {
    std::array<float, Frames> left{};
    std::array<float, Frames> right{};
    return engine.process(events,
                          static_cast<std::uint32_t>(Frames),
                          left.data(),
                          right.data(),
                          noteEnds);
}

} // namespace

int main() {
    ParameterVoiceEngine engine;
    if (!engine.configure(1, 48000.0, 64u))
        return 1;

    // RED contract: the four existing stable Amp Envelope IDs must be routed by
    // the audio-thread parameter adapter before they are published by clap.params.
    // Their public unit is seconds (except normalized sustain), while the DSP core
    // intentionally stores bounded sample counts.
    if (!applyValue(engine, kAmpAttackId, 0.001) ||
        !applyValue(engine, kAmpDecayId, 0.001) ||
        !applyValue(engine, kAmpSustainId, 0.5) ||
        !applyValue(engine, kAmpReleaseId, 0.002)) {
        std::cerr << "valid global Amp Envelope values were rejected\n";
        return 2;
    }

    double value = -1.0;
    if (!engine.parameterBaseValue(kAmpAttackId, value) || !approximately(value, 0.001) ||
        !engine.parameterBaseValue(kAmpDecayId, value) || !approximately(value, 0.001) ||
        !engine.parameterBaseValue(kAmpSustainId, value) || !approximately(value, 0.5) ||
        !engine.parameterBaseValue(kAmpReleaseId, value) || !approximately(value, 0.002)) {
        std::cerr << "Amp Envelope base values are not retained by stable parameter ID\n";
        return 3;
    }

    // These parameters are global-only. A per-note statement is structurally
    // valid CLAP input but must be ignored rather than mutating the default or
    // poisoning the real-time block.
    if (!applyValue(engine, kAmpReleaseId, 0.004, 700, 0, 0, 69) ||
        !engine.parameterBaseValue(kAmpReleaseId, value) || !approximately(value, 0.002)) {
        std::cerr << "per-note Amp Release unexpectedly mutated a global-only parameter\n";
        return 4;
    }

    // Finite out-of-range fuzz statements follow the existing parameter-adapter
    // robustness policy: ignore them without changing retained state.
    if (!applyValue(engine, kAmpReleaseId, 0.0) ||
        !engine.parameterBaseValue(kAmpReleaseId, value) || !approximately(value, 0.002)) {
        std::cerr << "invalid Amp Release changed retained state or failed the block\n";
        return 5;
    }

    NoteInput firstOn(CLAP_EVENT_NOTE_ON, 700, 69);
    NoteEndCapture firstEnds;
    if (!render<48>(engine, &firstOn.input, firstEnds))
        return 6;

    float level = -1.0f;
    if (!engine.voiceEnvelopeLevel(0, level) || !approximately(level, 1.0, 1.0e-6)) {
        std::cerr << "0.001 s attack did not convert to exactly 48 samples at 48 kHz\n";
        return 7;
    }

    if (!render<48>(engine, nullptr, firstEnds) ||
        !engine.voiceEnvelopeLevel(0, level) || !approximately(level, 0.5, 1.0e-6)) {
        std::cerr << "0.001 s decay did not reach the configured sustain at 48 samples\n";
        return 8;
    }

    // Updating defaults while a generation is active must not splice a new ADSR
    // into that voice. Future NOTE_ON generations receive the new snapshot.
    if (!applyValue(engine, kAmpAttackId, 0.0) ||
        !applyValue(engine, kAmpDecayId, 0.0) ||
        !applyValue(engine, kAmpSustainId, 1.0) ||
        !applyValue(engine, kAmpReleaseId, 0.001) ||
        !render<1>(engine, nullptr, firstEnds) ||
        !engine.voiceEnvelopeLevel(0, level) || !approximately(level, 0.5, 1.0e-6)) {
        std::cerr << "active voice envelope changed when only future defaults were automated\n";
        return 9;
    }

    NoteInput firstOff(CLAP_EVENT_NOTE_OFF, 700, 69);
    if (!render<95>(engine, &firstOff.input, firstEnds) || firstEnds.count != 0 ||
        engine.activeCount() != 1u) {
        std::cerr << "active voice did not preserve its original 96-sample release snapshot\n";
        return 10;
    }
    if (!render<2>(engine, nullptr, firstEnds) || firstEnds.count != 1 ||
        firstEnds.events[0].header.time != 1u || engine.activeCount() != 0u) {
        std::cerr << "original 96-sample release did not retire at the exact boundary\n";
        return 11;
    }

    NoteInput secondOn(CLAP_EVENT_NOTE_ON, 701, 69);
    NoteEndCapture secondEnds;
    if (!render<1>(engine, &secondOn.input, secondEnds) ||
        !engine.voiceEnvelopeLevel(0, level) || !approximately(level, 1.0, 1.0e-6)) {
        std::cerr << "future voice did not receive the updated zero-time attack/decay snapshot\n";
        return 12;
    }

    NoteInput secondOff(CLAP_EVENT_NOTE_OFF, 701, 69);
    if (!render<49>(engine, &secondOff.input, secondEnds) || secondEnds.count != 1 ||
        secondEnds.events[0].header.time != 48u || engine.activeCount() != 0u) {
        std::cerr << "updated 0.001 s release did not convert to 48 samples\n";
        return 13;
    }

    return 0;
}
