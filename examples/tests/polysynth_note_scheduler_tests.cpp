#include "polysynth_note_scheduler.h"

#include <clap/clap.h>

#include <array>
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

    // Same-time events must preserve host list order. These two note-ons also
    // prove that identical key/channel values remain isolated by note_id.
    if (!expectEvent(capture, 0, 2, ScheduledNoteKind::NoteOn, 0, 100, 0, 1, 60,
                     "first same-time NOTE_ON") ||
        !expectEvent(capture, 1, 2, ScheduledNoteKind::NoteOn, 1, 101, 0, 1, 60,
                     "second same-time NOTE_ON")) {
        return 4;
    }

    // NOTE_OFF may wildcard any address field. A note_id-only release must
    // target the concrete active identity without leaking to the overlapping
    // same-key voice.
    if (!expectEvent(capture, 2, 5, ScheduledNoteKind::NoteOff, 0, 100, 0, 1, 60,
                     "note-id wildcard NOTE_OFF")) {
        return 5;
    }

    // NOTE_OFF begins release but does not retire a voice. A later key-scoped
    // NOTE_CHOKE therefore still sees both active identities. Fan-out order is
    // deterministic ascending voice-slot order.
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

    // Malformed NOTE_ON addresses are ignored rather than allocating a voice:
    // CLAP requires port/channel/key to be specified for NOTE_ON.
    InputEvents malformed;
    if (!malformed.pushNote(1, CLAP_EVENT_NOTE_ON, 200, -1, 1, 64))
        return 9;
    Capture malformedCapture;
    if (!scheduler.process(&malformed.input, 4, malformedCapture) ||
        malformedCapture.count != 0 || scheduler.activeCount() != 2) {
        std::cerr << "scheduler accepted a NOTE_ON with a wildcard port\n";
        return 10;
    }

    // Review regression: when deterministic stealing occurs, the old concrete
    // identity must survive in the scheduled NOTE_ON result. The future voice
    // engine needs it to emit the correct CLAP NOTE_END for the host voice that
    // was terminated by stealing.
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

    // CLAP process events use offsets inside the current block. An event at
    // frames_count has no corresponding sample and must not be scheduled or
    // mutate the allocator; accepting it could later produce an invalid output
    // NOTE_END at the same out-of-range offset.
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

    return 0;
}
