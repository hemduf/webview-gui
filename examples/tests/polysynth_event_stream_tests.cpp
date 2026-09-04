#include "polysynth_voice_lifecycle.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
using webview_gui::examples::polysynth::NoteEventScheduler;
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
              std::uint8_t key,
              std::uint8_t velocity) noexcept {
        auto &event = midis[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0;
        event.data[0] = status;
        event.data[1] = key;
        event.data[2] = velocity;
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

    std::array<clap_event_note_t, 8> notes{};
    std::array<clap_event_midi_t, 8> midis{};
    std::array<clap_event_param_mod_t, 8> mods{};
    std::array<clap_event_note_expression_t, 8> expressions{};
    std::array<const clap_event_header_t *, 8> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

struct Entry {
    std::uint32_t type = 0;
    std::uint32_t time = 0;
    std::int32_t noteId = -1;
};
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
