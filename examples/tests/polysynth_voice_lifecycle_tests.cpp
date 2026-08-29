#include "polysynth_voice_lifecycle.h"

#include <clap/clap.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using webview_gui::examples::polysynth::ScheduledNoteEvent;
using webview_gui::examples::polysynth::VoiceIdentity;
using webview_gui::examples::polysynth::VoiceLifecycle;
using webview_gui::examples::polysynth::VoiceStage;

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
    std::array<const clap_event_header_t *, kCapacity> headers{};
    uint32_t count = 0;
    clap_input_events_t input{};
};

struct VoiceCapture {
    void operator()(const ScheduledNoteEvent &event) noexcept {
        if (count < events.size())
            events[count++] = event;
    }

    std::array<ScheduledNoteEvent, 16> events{};
    std::size_t count = 0;
};

struct NoteEndCapture {
    void operator()(const clap_event_note_t &event) noexcept {
        if (count < events.size())
            events[count++] = event;
    }

    std::array<clap_event_note_t, 16> events{};
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

bool expectNoteEnd(const NoteEndCapture &capture,
                   std::size_t index,
                   uint32_t time,
                   int32_t noteId,
                   int16_t port,
                   int16_t channel,
                   int16_t key,
                   const char *label) {
    if (index >= capture.count) {
        std::cerr << label << ": missing NOTE_END\n";
        return false;
    }

    const auto &event = capture.events[index];
    if (event.header.size != sizeof(clap_event_note_t) ||
        event.header.time != time ||
        event.header.space_id != CLAP_CORE_EVENT_SPACE_ID ||
        event.header.type != CLAP_EVENT_NOTE_END ||
        event.header.flags != 0 ||
        event.note_id != noteId || event.port_index != port ||
        event.channel != channel || event.key != key || event.velocity != 0.0) {
        std::cerr << label << ": NOTE_END did not preserve the concrete NOTE_ON identity\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    VoiceLifecycle lifecycle;
    if (!lifecycle.configure(2) || lifecycle.capacity() != 2 || lifecycle.activeCount() != 0) {
        std::cerr << "voice lifecycle rejected valid fixed polyphony\n";
        return 1;
    }

    VoiceCapture voices;
    NoteEndCapture noteEnds;
    InputEvents starts;
    if (!starts.pushNote(1, CLAP_EVENT_NOTE_ON, 100, 0, 1, 60, 0.8) ||
        !starts.pushNote(2, CLAP_EVENT_NOTE_ON, 101, 0, 1, 64, 0.7) ||
        !lifecycle.process(&starts.input, 8, voices, noteEnds)) {
        std::cerr << "voice lifecycle rejected valid NOTE_ON events\n";
        return 2;
    }

    if (voices.count != 2 || noteEnds.count != 0 || lifecycle.activeCount() != 2 ||
        lifecycle.stage(0) != VoiceStage::Held || lifecycle.stage(1) != VoiceStage::Held) {
        std::cerr << "NOTE_ON did not establish two held voices without premature NOTE_END\n";
        return 3;
    }

    InputEvents release;
    if (!release.pushNote(4, CLAP_EVENT_NOTE_OFF, 100, -1, -1, -1, 0.25) ||
        !lifecycle.process(&release.input, 8, voices, noteEnds)) {
        return 4;
    }

    if (lifecycle.stage(0) != VoiceStage::Releasing || lifecycle.activeCount() != 2 ||
        noteEnds.count != 0) {
        std::cerr << "NOTE_OFF retired the voice or emitted NOTE_END before envelope completion\n";
        return 5;
    }

    if (!lifecycle.completeRelease(0, 6, noteEnds) || lifecycle.activeCount() != 1 ||
        lifecycle.stage(0) != VoiceStage::Inactive ||
        !expectNoteEnd(noteEnds, 0, 6, 100, 0, 1, 60, "release completion")) {
        std::cerr << "release completion did not retire exactly one concrete voice\n";
        return 6;
    }

    InputEvents choke;
    if (!choke.pushNote(7, CLAP_EVENT_NOTE_CHOKE, 101, 0, 1, 64) ||
        !lifecycle.process(&choke.input, 8, voices, noteEnds) ||
        lifecycle.activeCount() != 0 || lifecycle.stage(1) != VoiceStage::Inactive ||
        !expectNoteEnd(noteEnds, 1, 7, 101, 0, 1, 64, "NOTE_CHOKE")) {
        std::cerr << "NOTE_CHOKE did not terminate and report the concrete voice immediately\n";
        return 7;
    }

    // Hosts without note IDs may create multiple identical concrete identities.
    // Completing one release must retire the requested slot rather than whichever
    // duplicate identity happens to be found first.
    lifecycle.reset();
    if (!lifecycle.configure(2))
        return 8;
    InputEvents duplicates;
    if (!duplicates.pushNote(0, CLAP_EVENT_NOTE_ON, -1, 0, 2, 67) ||
        !duplicates.pushNote(1, CLAP_EVENT_NOTE_ON, -1, 0, 2, 67) ||
        !duplicates.pushNote(2, CLAP_EVENT_NOTE_OFF, -1, 0, 2, 67) ||
        !lifecycle.process(&duplicates.input, 4, voices, noteEnds)) {
        return 9;
    }
    if (lifecycle.stage(0) != VoiceStage::Releasing ||
        lifecycle.stage(1) != VoiceStage::Releasing || lifecycle.activeCount() != 2) {
        std::cerr << "wildcard NOTE_OFF did not put both duplicate identities into release\n";
        return 10;
    }
    if (!lifecycle.completeRelease(1, 3, noteEnds) || lifecycle.activeCount() != 1 ||
        lifecycle.stage(0) != VoiceStage::Releasing ||
        lifecycle.stage(1) != VoiceStage::Inactive) {
        std::cerr << "slot-specific release retired the wrong duplicate identity\n";
        return 11;
    }
    if (!lifecycle.completeRelease(0, 3, noteEnds) || lifecycle.activeCount() != 0)
        return 12;

    // Deterministic stealing is an immediate termination of the replaced host
    // voice. The old NOTE_ON identity must be reported as NOTE_END at the steal
    // timestamp before the slot continues with the new identity.
    lifecycle.reset();
    if (!lifecycle.configure(1))
        return 13;
    VoiceCapture stealingVoices;
    NoteEndCapture stealingEnds;
    InputEvents stealing;
    if (!stealing.pushNote(0, CLAP_EVENT_NOTE_ON, 300, 0, 3, 60) ||
        !stealing.pushNote(3, CLAP_EVENT_NOTE_ON, 301, 0, 3, 72) ||
        !lifecycle.process(&stealing.input, 4, stealingVoices, stealingEnds)) {
        return 14;
    }

    VoiceIdentity remaining{};
    if (stealingVoices.count != 2 || stealingEnds.count != 1 ||
        !expectNoteEnd(stealingEnds, 0, 3, 300, 0, 3, 60, "voice steal") ||
        lifecycle.activeCount() != 1 || lifecycle.stage(0) != VoiceStage::Held ||
        !lifecycle.voiceIdentity(0, remaining) || !sameIdentity(remaining, 301, 0, 3, 72)) {
        std::cerr << "voice stealing did not terminate the old host identity and retain the new one\n";
        return 15;
    }

    lifecycle.reset();
    if (lifecycle.activeCount() != 0 || lifecycle.stage(0) != VoiceStage::Inactive) {
        std::cerr << "voice lifecycle reset leaked active or releasing state\n";
        return 16;
    }

    return 0;
}
