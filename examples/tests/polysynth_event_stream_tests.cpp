#include "polysynth_parameter_voice_engine.h"
#include "polysynth_voice_lifecycle.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
using webview_gui::examples::polysynth::NoteEventScheduler;
using webview_gui::examples::polysynth::ParameterVoiceEngine;
using webview_gui::examples::polysynth::ScheduledNoteEvent;
using webview_gui::examples::polysynth::ScheduledNoteKind;
using webview_gui::examples::polysynth::VoiceLifecycle;
using webview_gui::examples::polysynth::VoiceLifecycleEvent;

struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool note(std::uint32_t time, std::uint16_t type, std::int32_t noteId) noexcept {
        auto &event = notes[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 2;
        event.key = 60;
        event.velocity = 1.0;
        headers[count++] = &event.header;
        return true;
    }

    bool midi(std::uint32_t time,
              std::uint8_t status,
              std::uint8_t data1,
              std::uint8_t data2) noexcept {
        auto &event = midis[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0;
        event.data[0] = status;
        event.data[1] = data1;
        event.data[2] = data2;
        headers[count++] = &event.header;
        return true;
    }

    bool mod(std::uint32_t time, std::int32_t noteId) noexcept {
        auto &event = mods[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_MOD;
        event.param_id = 100;
        event.note_id = noteId;
        event.port_index = noteId < 0 ? -1 : 0;
        event.channel = noteId < 0 ? -1 : 2;
        event.key = noteId < 0 ? -1 : 60;
        event.amount = 0.25;
        headers[count++] = &event.header;
        return true;
    }

    bool expression(std::uint32_t time, std::int32_t noteId) noexcept {
        auto &event = expressions[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
        event.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 2;
        event.key = 60;
        event.value = 0.5;
        headers[count++] = &event.header;
        return true;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *list) noexcept {
        return static_cast<const InputEvents *>(list->ctx)->count;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *list,
                                                    std::uint32_t index) noexcept {
        const auto &self = *static_cast<const InputEvents *>(list->ctx);
        return index < self.count ? self.headers[index] : nullptr;
    }

    std::array<clap_event_note_t, 16> notes{};
    std::array<clap_event_midi_t, 16> midis{};
    std::array<clap_event_param_mod_t, 16> mods{};
    std::array<clap_event_note_expression_t, 16> expressions{};
    std::array<const clap_event_header_t *, 16> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

struct Entry {
    std::uint32_t type = 0;
    std::uint32_t time = 0;
    std::int32_t noteId = -1;
};

struct ExpressionEntry {
    std::uint32_t time = 0;
    std::int32_t expressionId = -1;
    std::int16_t channel = -1;
    std::int16_t key = -1;
    double value = 0.0;
};

struct NullNoteEndSink {
    void operator()(const clap_event_note_t &) noexcept {}
};

bool near(double a, double b, double tolerance = 1.0e-12) noexcept {
    return std::fabs(a - b) <= tolerance;
}

template <std::size_t Frames>
bool render(ParameterVoiceEngine &engine,
            InputEvents &events,
            std::array<float, Frames> &left,
            std::array<float, Frames> &right) noexcept {
    NullNoteEndSink noteEnd;
    return engine.process(&events.input,
                          static_cast<std::uint32_t>(Frames),
                          left.data(),
                          right.data(),
                          noteEnd);
}

template <std::size_t Frames>
double blockDelta(const std::array<float, Frames> &aLeft,
                  const std::array<float, Frames> &aRight,
                  const std::array<float, Frames> &bLeft,
                  const std::array<float, Frames> &bRight) noexcept {
    double delta = 0.0;
    for (std::size_t i = 0; i < Frames; ++i)
        delta += std::fabs(static_cast<double>(aLeft[i] - bLeft[i])) +
                 std::fabs(static_cast<double>(aRight[i] - bRight[i]));
    return delta;
}

template <std::size_t Frames>
float peak(const std::array<float, Frames> &samples) noexcept {
    float value = 0.0f;
    for (const auto sample : samples)
        value = std::max(value, std::fabs(sample));
    return value;
}

bool prepareEnginePair(ParameterVoiceEngine &subject,
                       ParameterVoiceEngine &reference) noexcept {
    return subject.configure(4u, 48000.0, 64u) &&
           reference.configure(4u, 48000.0, 64u);
}

bool primeMidiVoice(ParameterVoiceEngine &subject,
                    ParameterVoiceEngine &reference) noexcept {
    InputEvents subjectOn;
    InputEvents referenceOn;
    subjectOn.midi(0u, 0x92u, 60u, 127u);
    referenceOn.midi(0u, 0x92u, 60u, 127u);
    std::array<float, 64> subjectLeft{};
    std::array<float, 64> subjectRight{};
    std::array<float, 64> referenceLeft{};
    std::array<float, 64> referenceRight{};
    return render(subject, subjectOn, subjectLeft, subjectRight) &&
           render(reference, referenceOn, referenceLeft, referenceRight) &&
           blockDelta(subjectLeft, subjectRight, referenceLeft, referenceRight) < 1.0e-7;
}
}

int main() {
    NoteEventScheduler scheduler;
    if (!scheduler.configure(2))
        return 1;

    InputEvents input;
    input.mod(1, -1);
    input.note(1, CLAP_EVENT_NOTE_ON, 10);
    input.expression(1, 10);
    input.note(1, CLAP_EVENT_NOTE_ON, 11);
    input.note(3, CLAP_EVENT_NOTE_OFF, 10);

    std::array<Entry, 8> sequence{};
    std::size_t sequenceCount = 0;
    std::array<std::uint32_t, 4> boundaries{};
    std::size_t boundaryCount = 0;

    auto boundarySink = [&](std::uint32_t time) noexcept {
        boundaries[boundaryCount++] = time;
    };
    auto coreSink = [&](const clap_event_header_t &header) noexcept -> bool {
        std::int32_t noteId = -1;
        if (header.type == CLAP_EVENT_PARAM_MOD)
            noteId = reinterpret_cast<const clap_event_param_mod_t &>(header).note_id;
        else if (header.type == CLAP_EVENT_NOTE_EXPRESSION)
            noteId = reinterpret_cast<const clap_event_note_expression_t &>(header).note_id;
        sequence[sequenceCount++] = {header.type, header.time, noteId};
        return true;
    };
    auto noteSink = [&](const ScheduledNoteEvent &event) noexcept {
        const auto type = event.kind == ScheduledNoteKind::NoteOn
                              ? CLAP_EVENT_NOTE_ON
                              : CLAP_EVENT_NOTE_OFF;
        sequence[sequenceCount++] = {static_cast<std::uint32_t>(type), event.time,
                                     event.identity.noteId};
    };

    if (!scheduler.processWithBoundariesAndEvents(
            &input.input, 4, boundarySink, coreSink, noteSink)) {
        std::cerr << "ordered stream rejected valid input\n";
        return 2;
    }

    const std::array<Entry, 5> expected{{
        {CLAP_EVENT_PARAM_MOD, 1, -1},
        {CLAP_EVENT_NOTE_ON, 1, 10},
        {CLAP_EVENT_NOTE_EXPRESSION, 1, 10},
        {CLAP_EVENT_NOTE_ON, 1, 11},
        {CLAP_EVENT_NOTE_OFF, 3, 10},
    }};

    if (boundaryCount != 2 || boundaries[0] != 1 || boundaries[1] != 3 ||
        sequenceCount != expected.size()) {
        std::cerr << "timestamp boundary contract failed\n";
        return 3;
    }

    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (sequence[i].type != expected[i].type ||
            sequence[i].time != expected[i].time ||
            sequence[i].noteId != expected[i].noteId) {
            std::cerr << "same-sample event ordering changed\n";
            return 4;
        }
    }

    NoteEventScheduler midiScheduler;
    if (!midiScheduler.configure(2))
        return 8;

    InputEvents midiInput;
    midiInput.midi(0, 0x92u, 60u, 100u);
    midiInput.midi(2, 0x82u, 60u, 64u);

    std::array<ScheduledNoteEvent, 2> midiSequence{};
    std::size_t midiSequenceCount = 0;
    std::size_t forwardedMidiCount = 0;
    auto midiBoundary = [](std::uint32_t) noexcept {};
    auto midiCore = [&](const clap_event_header_t &header) noexcept -> bool {
        if (header.type == CLAP_EVENT_MIDI)
            ++forwardedMidiCount;
        return true;
    };
    auto midiNote = [&](const ScheduledNoteEvent &event) noexcept {
        if (midiSequenceCount < midiSequence.size())
            midiSequence[midiSequenceCount++] = event;
    };

    if (!midiScheduler.processWithBoundariesAndEvents(
            &midiInput.input, 4, midiBoundary, midiCore, midiNote)) {
        std::cerr << "standalone MIDI note stream rejected\n";
        return 9;
    }

    const auto expectedOnVelocity = 100.0 / 127.0;
    const auto expectedOffVelocity = 64.0 / 127.0;
    if (forwardedMidiCount != 0 || midiSequenceCount != 2 ||
        midiSequence[0].kind != ScheduledNoteKind::NoteOn ||
        midiSequence[0].time != 0 || midiSequence[0].identity.noteId != -1 ||
        midiSequence[0].identity.portIndex != 0 || midiSequence[0].identity.channel != 2 ||
        midiSequence[0].identity.key != 60 ||
        std::fabs(midiSequence[0].velocity - expectedOnVelocity) > 1.0e-12 ||
        midiSequence[1].kind != ScheduledNoteKind::NoteOff ||
        midiSequence[1].time != 2 || midiSequence[1].identity.noteId != -1 ||
        midiSequence[1].identity.portIndex != 0 || midiSequence[1].identity.channel != 2 ||
        midiSequence[1].identity.key != 60 ||
        std::fabs(midiSequence[1].velocity - expectedOffVelocity) > 1.0e-12) {
        std::cerr << "standalone MIDI note translation failed\n";
        return 10;
    }

    // Validate the expressive MIDI 1.0 channel messages emitted by the standalone.
    NoteEventScheduler expressiveScheduler;
    if (!expressiveScheduler.configure(4))
        return 11;
    InputEvents expressiveInput;
    expressiveInput.midi(0, 0xe2u, 0x7fu, 0x7fu); // pitch bend max
    expressiveInput.midi(1, 0xa2u, 60u, 96u);     // poly aftertouch
    expressiveInput.midi(2, 0xd2u, 80u, 0u);      // channel pressure
    expressiveInput.midi(3, 0xb2u, 10u, 0u);      // pan hard left
    expressiveInput.midi(4, 0xb2u, 11u, 64u);     // expression
    expressiveInput.midi(5, 0xb2u, 74u, 127u);    // brightness
    expressiveInput.midi(6, 0xc2u, 5u, 0u);       // program change: safe raw no-op
    expressiveInput.midi(7, 0xb2u, 1u, 100u);     // unmapped CC: safe raw no-op

    std::array<ExpressionEntry, 8> expressiveEvents{};
    std::size_t expressiveCount = 0;
    std::size_t expressiveRawMidiCount = 0;
    auto expressiveCore = [&](const clap_event_header_t &header) noexcept -> bool {
        if (header.type == CLAP_EVENT_MIDI) {
            ++expressiveRawMidiCount;
            return true;
        }
        if (header.type != CLAP_EVENT_NOTE_EXPRESSION ||
            header.size < sizeof(clap_event_note_expression_t))
            return true;
        const auto &event = reinterpret_cast<const clap_event_note_expression_t &>(header);
        expressiveEvents[expressiveCount++] = {
            event.header.time,
            event.expression_id,
            event.channel,
            event.key,
            event.value,
        };
        return true;
    };
    auto ignoredNote = [](const ScheduledNoteEvent &) noexcept {};
    if (!expressiveScheduler.processWithBoundariesAndEvents(
            &expressiveInput.input, 8, midiBoundary, expressiveCore, ignoredNote)) {
        std::cerr << "expressive MIDI stream rejected\n";
        return 12;
    }

    if (expressiveCount != 6 || expressiveRawMidiCount != 2 ||
        expressiveEvents[0].expressionId != CLAP_NOTE_EXPRESSION_TUNING ||
        expressiveEvents[0].channel != 2 || expressiveEvents[0].key != -1 ||
        !near(expressiveEvents[0].value, 2.0) ||
        expressiveEvents[1].expressionId != CLAP_NOTE_EXPRESSION_PRESSURE ||
        expressiveEvents[1].key != 60 ||
        !near(expressiveEvents[1].value, 96.0 / 127.0) ||
        expressiveEvents[2].expressionId != CLAP_NOTE_EXPRESSION_PRESSURE ||
        expressiveEvents[2].key != -1 ||
        !near(expressiveEvents[2].value, 80.0 / 127.0) ||
        expressiveEvents[3].expressionId != CLAP_NOTE_EXPRESSION_PAN ||
        !near(expressiveEvents[3].value, 0.0) ||
        expressiveEvents[4].expressionId != CLAP_NOTE_EXPRESSION_EXPRESSION ||
        !near(expressiveEvents[4].value, 64.0 / 127.0) ||
        expressiveEvents[5].expressionId != CLAP_NOTE_EXPRESSION_BRIGHTNESS ||
        !near(expressiveEvents[5].value, 1.0)) {
        std::cerr << "expressive MIDI translation failed\n";
        return 13;
    }

    // MIDI controller state must be replayed before a later MIDI NOTE_ON so the
    // first rendered sample already sees the current bend/expression/pan state.
    NoteEventScheduler stateScheduler;
    if (!stateScheduler.configure(2))
        return 14;
    InputEvents stateInput;
    stateInput.midi(0, 0xe2u, 0u, 0u);       // -2 semitones
    stateInput.midi(1, 0xb2u, 7u, 100u);     // channel volume
    stateInput.midi(1, 0xb2u, 11u, 64u);     // expression
    stateInput.midi(2, 0x92u, 62u, 100u);    // later note-on

    std::array<ExpressionEntry, 16> replayEvents{};
    std::size_t replayCount = 0;
    std::size_t replayNoteCount = 0;
    auto replayCore = [&](const clap_event_header_t &header) noexcept -> bool {
        if (header.type != CLAP_EVENT_NOTE_EXPRESSION ||
            header.size < sizeof(clap_event_note_expression_t))
            return true;
        const auto &event = reinterpret_cast<const clap_event_note_expression_t &>(header);
        replayEvents[replayCount++] = {
            event.header.time,
            event.expression_id,
            event.channel,
            event.key,
            event.value,
        };
        return true;
    };
    auto replayNote = [&](const ScheduledNoteEvent &event) noexcept {
        if (event.kind == ScheduledNoteKind::NoteOn)
            ++replayNoteCount;
    };
    if (!stateScheduler.processWithBoundariesAndEvents(
            &stateInput.input, 4, midiBoundary, replayCore, replayNote)) {
        std::cerr << "MIDI channel-state replay rejected\n";
        return 15;
    }

    bool replayedBend = false;
    bool replayedExpression = false;
    const auto expectedExpression = (100.0 / 127.0) * (64.0 / 127.0);
    for (std::size_t i = 0; i < replayCount; ++i) {
        const auto &event = replayEvents[i];
        if (event.time != 2 || event.channel != 2 || event.key != 62)
            continue;
        if (event.expressionId == CLAP_NOTE_EXPRESSION_TUNING && near(event.value, -2.0))
            replayedBend = true;
        if (event.expressionId == CLAP_NOTE_EXPRESSION_EXPRESSION &&
            near(event.value, expectedExpression))
            replayedExpression = true;
    }
    if (replayNoteCount != 1 || !replayedBend || !replayedExpression) {
        std::cerr << "MIDI channel state was not replayed before note-on\n";
        return 16;
    }

    // RPN 0 pitch-bend sensitivity must update the conversion range sample-accurately.
    NoteEventScheduler rpnScheduler;
    if (!rpnScheduler.configure(1))
        return 17;
    InputEvents rpnInput;
    rpnInput.midi(0, 0xb2u, 101u, 0u);
    rpnInput.midi(0, 0xb2u, 100u, 0u);
    rpnInput.midi(1, 0xb2u, 6u, 12u);
    rpnInput.midi(1, 0xb2u, 38u, 50u);
    rpnInput.midi(2, 0xe2u, 0x7fu, 0x7fu);

    double finalRpnBend = 0.0;
    auto rpnCore = [&](const clap_event_header_t &header) noexcept -> bool {
        if (header.type == CLAP_EVENT_NOTE_EXPRESSION &&
            header.size >= sizeof(clap_event_note_expression_t)) {
            const auto &event = reinterpret_cast<const clap_event_note_expression_t &>(header);
            if (event.expression_id == CLAP_NOTE_EXPRESSION_TUNING)
                finalRpnBend = event.value;
        }
        return true;
    };
    if (!rpnScheduler.processWithBoundariesAndEvents(
            &rpnInput.input, 4, midiBoundary, rpnCore, ignoredNote) ||
        !near(finalRpnBend, 12.5)) {
        std::cerr << "RPN pitch-bend sensitivity translation failed\n";
        return 18;
    }

    // Sustain must defer note-off until pedal-up at the exact controller timestamp.
    NoteEventScheduler sustainScheduler;
    if (!sustainScheduler.configure(2))
        return 19;
    InputEvents sustainInput;
    sustainInput.midi(0, 0x92u, 60u, 100u);
    sustainInput.midi(1, 0xb2u, 64u, 127u);
    sustainInput.midi(2, 0x82u, 60u, 64u);
    sustainInput.midi(3, 0xb2u, 64u, 0u);

    std::array<ScheduledNoteEvent, 4> sustainNotes{};
    std::size_t sustainNoteCount = 0;
    auto sustainCore = [](const clap_event_header_t &) noexcept -> bool { return true; };
    auto sustainNote = [&](const ScheduledNoteEvent &event) noexcept {
        sustainNotes[sustainNoteCount++] = event;
    };
    if (!sustainScheduler.processWithBoundariesAndEvents(
            &sustainInput.input, 4, midiBoundary, sustainCore, sustainNote) ||
        sustainNoteCount != 2 ||
        sustainNotes[0].kind != ScheduledNoteKind::NoteOn || sustainNotes[0].time != 0 ||
        sustainNotes[1].kind != ScheduledNoteKind::NoteOff || sustainNotes[1].time != 3) {
        std::cerr << "MIDI sustain lifecycle failed\n";
        return 20;
    }

    // CC123 is sustain-aware Note Off; CC120 is immediate Note Choke.
    NoteEventScheduler panicScheduler;
    if (!panicScheduler.configure(4))
        return 21;
    InputEvents panicInput;
    panicInput.midi(0, 0x92u, 60u, 100u);
    panicInput.midi(0, 0x92u, 64u, 100u);
    panicInput.midi(1, 0xb2u, 123u, 0u);
    panicInput.midi(2, 0x92u, 67u, 100u);
    panicInput.midi(3, 0xb2u, 120u, 0u);

    std::array<ScheduledNoteEvent, 8> panicNotes{};
    std::size_t panicNoteCount = 0;
    auto panicNote = [&](const ScheduledNoteEvent &event) noexcept {
        panicNotes[panicNoteCount++] = event;
    };
    if (!panicScheduler.processWithBoundariesAndEvents(
            &panicInput.input, 4, midiBoundary, sustainCore, panicNote)) {
        std::cerr << "MIDI all-notes/all-sound-off stream rejected\n";
        return 22;
    }
    std::size_t noteOffAtOne = 0;
    std::size_t chokeAtThree = 0;
    for (std::size_t i = 0; i < panicNoteCount; ++i) {
        noteOffAtOne += panicNotes[i].kind == ScheduledNoteKind::NoteOff &&
                        panicNotes[i].time == 1 ? 1u : 0u;
        chokeAtThree += panicNotes[i].kind == ScheduledNoteKind::NoteChoke &&
                        panicNotes[i].time == 3 ? 1u : 0u;
    }
    if (noteOffAtOne != 2 || chokeAtThree != 3) {
        std::cerr << "MIDI panic-controller lifecycle failed\n";
        return 23;
    }

    // End-to-end DSP validation: raw MIDI must not merely translate structurally;
    // expressive messages have to change a sounding voice in the production engine.
    {
        ParameterVoiceEngine subject;
        ParameterVoiceEngine reference;
        if (!prepareEnginePair(subject, reference) || !primeMidiVoice(subject, reference))
            return 24;
        InputEvents bend;
        InputEvents none;
        bend.midi(0u, 0xe2u, 0x7fu, 0x7fu);
        std::array<float, 64> sl{}, sr{}, rl{}, rr{};
        if (!render(subject, bend, sl, sr) || !render(reference, none, rl, rr) ||
            blockDelta(sl, sr, rl, rr) <= 1.0e-3) {
            std::cerr << "pitch bend did not affect DSP output\n";
            return 25;
        }
    }

    {
        ParameterVoiceEngine subject;
        ParameterVoiceEngine reference;
        if (!prepareEnginePair(subject, reference) || !primeMidiVoice(subject, reference))
            return 26;
        InputEvents pressure;
        InputEvents none;
        pressure.midi(0u, 0xa2u, 60u, 127u);
        std::array<float, 64> sl{}, sr{}, rl{}, rr{};
        if (!render(subject, pressure, sl, sr) || !render(reference, none, rl, rr) ||
            blockDelta(sl, sr, rl, rr) <= 1.0e-3) {
            std::cerr << "poly aftertouch did not affect DSP output\n";
            return 27;
        }
    }

    {
        ParameterVoiceEngine subject;
        ParameterVoiceEngine reference;
        if (!prepareEnginePair(subject, reference) || !primeMidiVoice(subject, reference))
            return 28;
        InputEvents pan;
        InputEvents none;
        pan.midi(0u, 0xb2u, 10u, 0u);
        std::array<float, 64> sl{}, sr{}, rl{}, rr{};
        if (!render(subject, pan, sl, sr) || !render(reference, none, rl, rr) ||
            peak(sr) > 1.0e-6f || peak(sl) <= 1.0e-4f) {
            std::cerr << "CC10 pan did not reach DSP\n";
            return 29;
        }
    }

    {
        ParameterVoiceEngine subject;
        ParameterVoiceEngine reference;
        if (!prepareEnginePair(subject, reference) || !primeMidiVoice(subject, reference))
            return 30;
        InputEvents expression;
        InputEvents none;
        expression.midi(0u, 0xb2u, 11u, 0u);
        std::array<float, 64> sl{}, sr{}, rl{}, rr{};
        if (!render(subject, expression, sl, sr) || !render(reference, none, rl, rr) ||
            peak(sl) > 1.0e-6f || peak(sr) > 1.0e-6f) {
            std::cerr << "CC11 expression did not reach DSP\n";
            return 31;
        }
    }

    {
        ParameterVoiceEngine subject;
        ParameterVoiceEngine reference;
        if (!prepareEnginePair(subject, reference) || !primeMidiVoice(subject, reference))
            return 32;
        InputEvents harmless;
        InputEvents none;
        harmless.midi(0u, 0xc2u, 9u, 0u);
        harmless.midi(0u, 0xb2u, 1u, 100u);
        std::array<float, 64> sl{}, sr{}, rl{}, rr{};
        if (!render(subject, harmless, sl, sr) || !render(reference, none, rl, rr) ||
            blockDelta(sl, sr, rl, rr) > 1.0e-7) {
            std::cerr << "Program Change/unmapped CC changed DSP state\n";
            return 33;
        }
    }

    VoiceLifecycle lifecycle;
    if (!lifecycle.configure(1))
        return 5;

    InputEvents lifecycleInput;
    lifecycleInput.mod(0, -1);
    lifecycleInput.note(0, CLAP_EVENT_NOTE_ON, 20);
    lifecycleInput.expression(0, 20);

    std::array<Entry, 4> lifecycleSequence{};
    std::size_t lifecycleCount = 0;
    auto lifecycleBoundary = [](std::uint32_t) noexcept {};
    auto lifecycleCore = [&](const clap_event_header_t &header) noexcept -> bool {
        std::int32_t noteId = -1;
        if (header.type == CLAP_EVENT_NOTE_EXPRESSION)
            noteId = reinterpret_cast<const clap_event_note_expression_t &>(header).note_id;
        lifecycleSequence[lifecycleCount++] = {header.type, header.time, noteId};
        return true;
    };
    auto lifecycleVoice = [&](const VoiceLifecycleEvent &event) noexcept {
        lifecycleSequence[lifecycleCount++] = {
            CLAP_EVENT_NOTE_ON, event.note.time, event.note.identity.noteId};
    };
    auto noteEnd = [](const clap_event_note_t &) noexcept {};

    if (!lifecycle.processWithBoundariesAndEvents(
            &lifecycleInput.input,
            1,
            lifecycleBoundary,
            lifecycleCore,
            lifecycleVoice,
            noteEnd)) {
        std::cerr << "lifecycle rejected ordered non-note events\n";
        return 6;
    }

    if (lifecycleCount != 3 ||
        lifecycleSequence[0].type != CLAP_EVENT_PARAM_MOD ||
        lifecycleSequence[1].type != CLAP_EVENT_NOTE_ON ||
        lifecycleSequence[1].noteId != 20 ||
        lifecycleSequence[2].type != CLAP_EVENT_NOTE_EXPRESSION ||
        lifecycleSequence[2].noteId != 20 ||
        lifecycle.generation(0) == 0) {
        std::cerr << "lifecycle changed core/note ordering or generation\n";
        return 7;
    }

    return 0;
}
