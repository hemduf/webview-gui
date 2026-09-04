#include "polysynth_note_scheduler.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using webview_gui::examples::polysynth::ScheduledNoteEvent;
using webview_gui::examples::polysynth::ScheduledNoteKind;
using webview_gui::examples::polysynth::VoiceIdentity;
using webview_gui::examples::polysynth::VoiceAllocator;
using webview_gui::examples::polysynth::NoteEventScheduler;

struct InputEvents {
    static constexpr std::size_t kCapacity = 16;

    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(uint32_t time,
                  uint16_t type,
                  int32_t noteId,
                  int16_t port,
                  int16_t channel,
                  int16_t key,
                  double velocity = 1.0) noexcept {
        if (count >= kCapacity)
            return false;

        auto &event = notes[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = noteId;
        event.port_index = port;
        event.channel = channel;
        event.key = key;
        event.velocity = velocity;
        headers[count] = &event.header;
        ++count;
        return true;
    }

    bool pushMidi(uint32_t time,
                  uint16_t port,
                  uint8_t status,
                  uint8_t data1,
                  uint8_t data2) noexcept {
        if (count >= kCapacity)
            return false;

        auto &event = midis[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = port;
        event.data[0] = status;
        event.data[1] = data1;
        event.data[2] = data2;
        headers[count] = &event.header;
        ++count;
        return true;
    }

    bool pushForeign(uint32_t time) noexcept {
        if (count >= kCapacity)
            return false;
        auto &event = foreign[count];
        event = {};
        event.size = sizeof(event);
        event.time = time;
        event.space_id = 42;
        event.type = 1;
        headers[count] = &event;
        ++count;
        return true;
    }

    static uint32_t CLAP_ABI size(const clap_input_events_t *list) noexcept {
        if (!list || !list->ctx)
            return 0;
        return static_cast<const InputEvents *>(list->ctx)->count;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *list,
                                                    uint32_t index) noexcept {
        if (!list || !list->ctx)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(list->ctx);
        return index < self.count ? self.headers[index] : nullptr;
    }

    std::array<clap_event_note_t, kCapacity> notes{};
    std::array<clap_event_midi_t, kCapacity> midis{};
    std::array<clap_event_header_t, kCapacity> foreign{};
    std::array<const clap_event_header_t *, kCapacity> headers{};
    uint32_t count = 0;
    clap_input_events_t input{};
};

struct Capture {
    void operator()(const ScheduledNoteEvent &event) noexcept {
        if (count < events.size())
            events[count++] = event;
    }

    std::array<ScheduledNoteEvent, 16> events{};
    std::size_t count = 0;
};

bool sameIdentity(const VoiceIdentity &identity,
                  int32_t noteId,
                  int16_t port,
                  int16_t channel,
                  int16_t key) noexcept {
    return identity.noteId == noteId && identity.portIndex == port &&
           identity.channel == channel && identity.key == key;
}

bool expectEvent(const Capture &capture,
                 std::size_t index,
                 uint32_t time,
                 ScheduledNoteKind kind,
                 VoiceAllocator::VoiceIndex voice,
                 int32_t noteId,
                 int16_t port,
                 int16_t channel,
                 int16_t key,
                 const char *label) {
    if (index >= capture.count) {
        std::cerr << label << ": missing scheduled event\n";
        return false;
    }

    const auto &event = capture.events[index];
    if (event.time != time || event.kind != kind || event.voiceIndex != voice ||
        !sameIdentity(event.identity, noteId, port, channel, key)) {
        std::cerr << label << ": scheduled event did not preserve time/order/identity\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    NoteEventScheduler scheduler;
    if (!scheduler.configure(3) || scheduler.capacity() != 3 || scheduler.activeCount() != 0) {
        std::cerr << "note scheduler rejected valid fixed polyphony\n";
        return 1;
    }

    InputEvents input;
    if (!input.pushForeign(1) ||
        !input.pushNote(2, CLAP_EVENT_NOTE_ON, 100, 0, 1, 60, 0.75) ||
        !input.pushNote(2, CLAP_EVENT_NOTE_ON, 101, 0, 1, 60, 0.50) ||
        !input.pushNote(5, CLAP_EVENT_NOTE_OFF, 100, -1, -1, -1, 0.25) ||
        !input.pushNote(7, CLAP_EVENT_NOTE_CHOKE, -1, 0, 1, 60, 0.0)) {
        return 2;
    }

    Capture capture;
    if (!scheduler.process(&input.input, 8, capture)) {
        std::cerr << "sample-accurate note scheduler rejected valid sorted CLAP events\n";
        return 3;
    }

    if (!expectEvent(capture, 0, 2, ScheduledNoteKind::NoteOn, 0, 100, 0, 1, 60,
                     "first same-time NOTE_ON") ||
        !expectEvent(capture, 1, 2, ScheduledNoteKind::NoteOn, 1, 101, 0, 1, 60,
                     "second same-time NOTE_ON")) {
        return 4;
    }

    if (!expectEvent(capture, 2, 5, ScheduledNoteKind::NoteOff, 0, 100, 0, 1, 60,
                     "note-id wildcard NOTE_OFF")) {
        return 5;
    }

    if (!expectEvent(capture, 3, 7, ScheduledNoteKind::NoteChoke, 0, 100, 0, 1, 60,
                     "first wildcard NOTE_CHOKE") ||
        !expectEvent(capture, 4, 7, ScheduledNoteKind::NoteChoke, 1, 101, 0, 1, 60,
                     "second wildcard NOTE_CHOKE") || capture.count != 5) {
        return 6;
    }

    if (scheduler.activeCount() != 2) {
        std::cerr << "scheduler retired a voice before the future voice engine can emit NOTE_END\n";
        return 7;
    }

    VoiceIdentity concrete{};
    if (!scheduler.voiceIdentity(0, concrete) || !sameIdentity(concrete, 100, 0, 1, 60) ||
        !scheduler.voiceIdentity(1, concrete) || !sameIdentity(concrete, 101, 0, 1, 60)) {
        std::cerr << "scheduler lost concrete voice identity after wildcard dispatch\n";
        return 8;
    }

    InputEvents malformed;
    if (!malformed.pushNote(1, CLAP_EVENT_NOTE_ON, 200, -1, 1, 64))
        return 9;
    Capture malformedCapture;
    if (!scheduler.process(&malformed.input, 4, malformedCapture) ||
        malformedCapture.count != 0 || scheduler.activeCount() != 2) {
        std::cerr << "scheduler accepted a NOTE_ON with a wildcard port\n";
        return 10;
    }

    scheduler.reset();
    if (!scheduler.configure(1))
        return 11;
    InputEvents stealing;
    if (!stealing.pushNote(0, CLAP_EVENT_NOTE_ON, 300, 0, 2, 64) ||
        !stealing.pushNote(3, CLAP_EVENT_NOTE_ON, 301, 0, 2, 67)) {
        return 12;
    }
    Capture stealingCapture;
    if (!scheduler.process(&stealing.input, 4, stealingCapture) || stealingCapture.count != 2) {
        std::cerr << "voice-stealing scheduler fixture did not produce two note-ons\n";
        return 13;
    }
    if (stealingCapture.events[0].replacedVoice ||
        !stealingCapture.events[1].replacedVoice ||
        !sameIdentity(stealingCapture.events[1].replacedIdentity, 300, 0, 2, 64) ||
        !sameIdentity(stealingCapture.events[1].identity, 301, 0, 2, 67)) {
        std::cerr << "voice stealing lost the replaced CLAP note identity needed for NOTE_END\n";
        return 14;
    }

    scheduler.reset();
    if (!scheduler.configure(1))
        return 15;
    InputEvents blockEndEvent;
    if (!blockEndEvent.pushNote(4, CLAP_EVENT_NOTE_ON, 400, 0, 3, 69))
        return 16;
    Capture blockEndCapture;
    if (scheduler.process(&blockEndEvent.input, 4, blockEndCapture) ||
        blockEndCapture.count != 0 || scheduler.activeCount() != 0) {
        std::cerr << "scheduler accepted an event at the invalid frames_count offset\n";
        return 17;
    }

    scheduler.reset();
    if (scheduler.activeCount() != 0) {
        std::cerr << "scheduler reset leaked active voice identities\n";
        return 18;
    }

    NoteEventScheduler portScheduler;
    if (!portScheduler.configure(1))
        return 19;
    InputEvents wrongPort;
    if (!wrongPort.pushNote(0, CLAP_EVENT_NOTE_ON, 500, 1, 2, 60) ||
        !wrongPort.pushMidi(1, 1, 0x92u, 60u, 100u))
        return 20;
    Capture wrongPortCapture;
    std::size_t forwardedWrongPortMidi = 0;
    auto boundary = [](std::uint32_t) noexcept {};
    auto wrongPortCore = [&](const clap_event_header_t &header) noexcept -> bool {
        if (header.type == CLAP_EVENT_MIDI)
            ++forwardedWrongPortMidi;
        return true;
    };
    if (!portScheduler.processWithBoundariesAndEvents(
            &wrongPort.input, 3, boundary, wrongPortCore, wrongPortCapture) ||
        wrongPortCapture.count != 0 || portScheduler.activeCount() != 0 ||
        forwardedWrongPortMidi != 1) {
        std::cerr << "scheduler accepted an event on a nonexistent note port\n";
        return 21;
    }

    NoteEventScheduler rpnScheduler;
    if (!rpnScheduler.configure(1))
        return 22;
    InputEvents rpn;
    if (!rpn.pushMidi(0, 0, 0xb2u, 101u, 0u) ||
        !rpn.pushMidi(0, 0, 0xb2u, 100u, 0u) ||
        !rpn.pushMidi(1, 0, 0xb2u, 6u, 12u) ||
        !rpn.pushMidi(1, 0, 0xb2u, 38u, 127u) ||
        !rpn.pushMidi(2, 0, 0xb2u, 99u, 1u) ||
        !rpn.pushMidi(2, 0, 0xb2u, 98u, 2u) ||
        !rpn.pushMidi(3, 0, 0xb2u, 6u, 24u) ||
        !rpn.pushMidi(4, 0, 0xe2u, 0x7fu, 0x7fu))
        return 23;

    double finalTuning = -999.0;
    std::size_t rawNrpnMessages = 0;
    auto rpnCore = [&](const clap_event_header_t &header) noexcept -> bool {
        if (header.type == CLAP_EVENT_MIDI) {
            ++rawNrpnMessages;
            return true;
        }
        if (header.type == CLAP_EVENT_NOTE_EXPRESSION &&
            header.size >= sizeof(clap_event_note_expression_t)) {
            const auto &event = reinterpret_cast<const clap_event_note_expression_t &>(header);
            if (event.expression_id == CLAP_NOTE_EXPRESSION_TUNING)
                finalTuning = event.value;
        }
        return true;
    };
    Capture rpnCapture;
    if (!rpnScheduler.processWithBoundariesAndEvents(
            &rpn.input, 5, boundary, rpnCore, rpnCapture) ||
        rpnCapture.count != 0 || rawNrpnMessages != 3 ||
        std::fabs(finalTuning - 13.27) > 1.0e-12) {
        std::cerr << "RPN cents range or NRPN isolation changed pitch-bend sensitivity\n";
        return 24;
    }

    // RP-015 Reset All Controllers is not a power-on reset. In the subset this
    // adapter models it resets bend/pressure/expression/sustain and the RPN
    // selector while preserving Channel Volume, Pan, Brightness and the stored
    // pitch-bend sensitivity value.
    NoteEventScheduler resetScheduler;
    if (!resetScheduler.configure(1))
        return 31;
    InputEvents resetInput;
    if (!resetInput.pushMidi(0, 0, 0xb2u, 7u, 64u) ||
        !resetInput.pushMidi(0, 0, 0xb2u, 10u, 0u) ||
        !resetInput.pushMidi(0, 0, 0xb2u, 74u, 127u) ||
        !resetInput.pushMidi(0, 0, 0xb2u, 101u, 0u) ||
        !resetInput.pushMidi(0, 0, 0xb2u, 100u, 0u) ||
        !resetInput.pushMidi(0, 0, 0xb2u, 6u, 12u) ||
        !resetInput.pushMidi(0, 0, 0xb2u, 38u, 127u) ||
        !resetInput.pushMidi(1, 0, 0xe2u, 0x7fu, 0x7fu) ||
        !resetInput.pushMidi(1, 0, 0xd2u, 80u, 0u) ||
        !resetInput.pushMidi(1, 0, 0xb2u, 11u, 32u) ||
        !resetInput.pushMidi(2, 0, 0xb2u, 121u, 0u) ||
        !resetInput.pushMidi(3, 0, 0xe2u, 0x7fu, 0x7fu) ||
        !resetInput.pushMidi(4, 0, 0x92u, 60u, 100u))
        return 32;

    bool resetBendCentered = false;
    bool resetPressureCleared = false;
    bool resetExpressionUsesPreservedVolume = false;
    bool resetTouchedPanOrBrightness = false;
    bool rangePreservedAfterReset = false;
    bool replayPreservedPan = false;
    bool replayPreservedBrightness = false;
    bool replayPreservedVolume = false;
    bool replayPreservedRange = false;
    std::size_t resetNoteOns = 0;
    const auto preservedVolume = 64.0 / 127.0;
    auto resetCore = [&](const clap_event_header_t &header) noexcept -> bool {
        if (header.type != CLAP_EVENT_NOTE_EXPRESSION ||
            header.size < sizeof(clap_event_note_expression_t))
            return true;
        const auto &event = reinterpret_cast<const clap_event_note_expression_t &>(header);
        if (event.header.time == 2) {
            if (event.expression_id == CLAP_NOTE_EXPRESSION_TUNING &&
                std::fabs(event.value) <= 1.0e-12)
                resetBendCentered = true;
            if (event.expression_id == CLAP_NOTE_EXPRESSION_PRESSURE &&
                std::fabs(event.value) <= 1.0e-12)
                resetPressureCleared = true;
            if (event.expression_id == CLAP_NOTE_EXPRESSION_EXPRESSION &&
                std::fabs(event.value - preservedVolume) <= 1.0e-12)
                resetExpressionUsesPreservedVolume = true;
            if (event.expression_id == CLAP_NOTE_EXPRESSION_PAN ||
                event.expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS)
                resetTouchedPanOrBrightness = true;
        }
        if (event.header.time == 3 && event.expression_id == CLAP_NOTE_EXPRESSION_TUNING &&
            std::fabs(event.value - 13.27) <= 1.0e-12)
            rangePreservedAfterReset = true;
        if (event.header.time == 4 && event.key == 60) {
            if (event.expression_id == CLAP_NOTE_EXPRESSION_TUNING &&
                std::fabs(event.value - 13.27) <= 1.0e-12)
                replayPreservedRange = true;
            if (event.expression_id == CLAP_NOTE_EXPRESSION_EXPRESSION &&
                std::fabs(event.value - preservedVolume) <= 1.0e-12)
                replayPreservedVolume = true;
            if (event.expression_id == CLAP_NOTE_EXPRESSION_PAN &&
                std::fabs(event.value) <= 1.0e-12)
                replayPreservedPan = true;
            if (event.expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS &&
                std::fabs(event.value - 1.0) <= 1.0e-12)
                replayPreservedBrightness = true;
        }
        return true;
    };
    Capture resetCapture;
    auto resetNote = [&](const ScheduledNoteEvent &event) noexcept {
        resetCapture(event);
        if (event.kind == ScheduledNoteKind::NoteOn)
            ++resetNoteOns;
    };
    if (!resetScheduler.processWithBoundariesAndEvents(
            &resetInput.input, 5, boundary, resetCore, resetNote) ||
        resetNoteOns != 1 || !resetBendCentered || !resetPressureCleared ||
        !resetExpressionUsesPreservedVolume || resetTouchedPanOrBrightness ||
        !rangePreservedAfterReset || !replayPreservedPan ||
        !replayPreservedBrightness || !replayPreservedVolume || !replayPreservedRange) {
        std::cerr << "MIDI Reset All Controllers violated RP-015 preservation/reset rules\n";
        return 33;
    }

    NoteEventScheduler panScheduler;
    if (!panScheduler.configure(1))
        return 25;
    InputEvents panInput;
    if (!panInput.pushMidi(0, 0, 0xb2u, 10u, 64u) ||
        !panInput.pushMidi(1, 0, 0xb2u, 10u, 127u))
        return 26;
    std::array<double, 2> panValues{};
    std::size_t panCount = 0;
    auto panCore = [&](const clap_event_header_t &header) noexcept -> bool {
        if (header.type == CLAP_EVENT_NOTE_EXPRESSION &&
            header.size >= sizeof(clap_event_note_expression_t)) {
            const auto &event = reinterpret_cast<const clap_event_note_expression_t &>(header);
            if (event.expression_id == CLAP_NOTE_EXPRESSION_PAN && panCount < panValues.size())
                panValues[panCount++] = event.value;
        }
        return true;
    };
    Capture panCapture;
    if (!panScheduler.processWithBoundariesAndEvents(
            &panInput.input, 2, boundary, panCore, panCapture) ||
        panCount != 2 || std::fabs(panValues[0] - 0.5) > 1.0e-12 ||
        std::fabs(panValues[1] - 1.0) > 1.0e-12) {
        std::cerr << "MIDI CC10 center/endpoints were mapped incorrectly\n";
        return 27;
    }

    for (const auto controller : std::array<std::uint8_t, 5>{{123u, 124u, 125u, 126u, 127u}}) {
        NoteEventScheduler modeScheduler;
        if (!modeScheduler.configure(1))
            return 28;
        InputEvents modeInput;
        if (!modeInput.pushMidi(0, 0, 0x92u, 60u, 100u) ||
            !modeInput.pushMidi(1, 0, 0xb2u, controller, 0u))
            return 29;
        Capture modeCapture;
        auto modeCore = [](const clap_event_header_t &) noexcept -> bool { return true; };
        if (!modeScheduler.processWithBoundariesAndEvents(
                &modeInput.input, 2, boundary, modeCore, modeCapture) ||
            modeCapture.count != 2 ||
            modeCapture.events[0].kind != ScheduledNoteKind::NoteOn ||
            modeCapture.events[1].kind != ScheduledNoteKind::NoteOff ||
            modeCapture.events[1].time != 1) {
            std::cerr << "MIDI Channel Mode message omitted its All Notes Off side effect\n";
            return 30;
        }
    }

    return 0;
}
