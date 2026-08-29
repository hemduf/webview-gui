#include "polysynth_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using webview_gui::examples::polysynth::VoiceIdentity;
using webview_gui::examples::polysynth::VoiceEngine;

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

struct NoteEndCapture {
    void operator()(const clap_event_note_t &event) noexcept {
        if (count < events.size())
            events[count++] = event;
    }

    std::array<clap_event_note_t, 16> events{};
    std::size_t count = 0;
};

bool nearZero(float value) noexcept {
    return std::fabs(value) <= 1.0e-7f;
}

bool finiteStereo(const std::array<float, 16> &left,
                  const std::array<float, 16> &right,
                  std::size_t frames) noexcept {
    for (std::size_t i = 0; i < frames; ++i) {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
            return false;
    }
    return true;
}

bool sameIdentity(const VoiceIdentity &identity,
                  int32_t noteId,
                  int16_t port,
                  int16_t channel,
                  int16_t key) noexcept {
    return identity.noteId == noteId && identity.portIndex == port &&
           identity.channel == channel && identity.key == key;
}

} // namespace

int main() {
    VoiceEngine engine;
    if (engine.configure(0, 48000.0, 4) ||
        engine.configure(2, 0.0, 4) ||
        engine.configure(2, 48000.0, 0)) {
        std::cerr << "voice engine accepted an invalid fixed RT configuration\n";
        return 1;
    }
    if (!engine.configure(2, 48000.0, 4) || engine.capacity() != 2 ||
        engine.activeCount() != 0) {
        std::cerr << "voice engine rejected a valid fixed RT configuration\n";
        return 2;
    }

    // The first bounded voice kernel uses a sine oscillator and a deterministic
    // linear release. NOTE_ON at sample 2 must not leak into samples 0..1;
    // NOTE_OFF at sample 5 starts a four-sample release and therefore retires
    // the voice at the sample-9 boundary, where NOTE_END must be reported.
    InputEvents lifecycleEvents;
    if (!lifecycleEvents.pushNote(2, CLAP_EVENT_NOTE_ON, 100, 0, 1, 69, 1.0) ||
        !lifecycleEvents.pushNote(5, CLAP_EVENT_NOTE_OFF, 100, 0, 1, 69, 0.25))
        return 3;

    std::array<float, 16> left{};
    std::array<float, 16> right{};
    NoteEndCapture noteEnds;
    if (!engine.process(&lifecycleEvents.input, 12, left.data(), right.data(), noteEnds)) {
        std::cerr << "voice engine rejected valid sample-accurate note events\n";
        return 4;
    }

    if (!nearZero(left[0]) || !nearZero(left[1]) ||
        !nearZero(right[0]) || !nearZero(right[1]) ||
        nearZero(left[2]) || nearZero(right[2])) {
        std::cerr << "NOTE_ON did not begin audio exactly at its sample timestamp\n";
        return 5;
    }
    if (noteEnds.count != 1 || noteEnds.events[0].header.type != CLAP_EVENT_NOTE_END ||
        noteEnds.events[0].header.time != 9 || noteEnds.events[0].note_id != 100 ||
        noteEnds.events[0].port_index != 0 || noteEnds.events[0].channel != 1 ||
        noteEnds.events[0].key != 69 || engine.activeCount() != 0) {
        std::cerr << "release completion did not retire the voice at the exact sample boundary\n";
        return 6;
    }
    for (std::size_t frame = 9; frame < 12; ++frame) {
        if (!nearZero(left[frame]) || !nearZero(right[frame])) {
            std::cerr << "released voice leaked audio after NOTE_END\n";
            return 7;
        }
    }
    if (!finiteStereo(left, right, 12)) {
        std::cerr << "voice engine produced NaN/Inf for legal note data\n";
        return 8;
    }

    // A release that completes exactly where a new NOTE_ON arrives must retire
    // before scheduler allocation at that timestamp. The new note therefore
    // starts on the same sample without a block-boundary quantization gap.
    engine.reset();
    if (!engine.configure(1, 48000.0, 4))
        return 9;
    InputEvents reuseEvents;
    if (!reuseEvents.pushNote(0, CLAP_EVENT_NOTE_ON, 200, 0, 2, 60, 0.8) ||
        !reuseEvents.pushNote(1, CLAP_EVENT_NOTE_OFF, 200, 0, 2, 60) ||
        !reuseEvents.pushNote(5, CLAP_EVENT_NOTE_ON, 201, 0, 2, 64, 0.9))
        return 10;

    left.fill(0.0f);
    right.fill(0.0f);
    NoteEndCapture reuseEnds;
    if (!engine.process(&reuseEvents.input, 8, left.data(), right.data(), reuseEnds))
        return 11;

    VoiceIdentity remaining{};
    if (reuseEnds.count != 1 || reuseEnds.events[0].header.time != 5 ||
        reuseEnds.events[0].note_id != 200 || engine.activeCount() != 1 ||
        !engine.voiceIdentity(0, remaining) || !sameIdentity(remaining, 201, 0, 2, 64) ||
        nearZero(left[5]) || nearZero(right[5])) {
        std::cerr << "release completion was not resolved before same-sample NOTE_ON allocation\n";
        return 12;
    }

    // NOTE_CHOKE is immediate: it must stop the current generation at the
    // event timestamp and leave the rest of the block silent.
    InputEvents choke;
    if (!choke.pushNote(3, CLAP_EVENT_NOTE_CHOKE, 201, 0, 2, 64))
        return 13;
    left.fill(0.0f);
    right.fill(0.0f);
    if (!engine.process(&choke.input, 7, left.data(), right.data(), reuseEnds))
        return 14;
    if (reuseEnds.count != 2 || reuseEnds.events[1].header.time != 3 ||
        reuseEnds.events[1].note_id != 201 || engine.activeCount() != 0) {
        std::cerr << "NOTE_CHOKE did not terminate the active voice immediately\n";
        return 15;
    }
    for (std::size_t frame = 3; frame < 7; ++frame) {
        if (!nearZero(left[frame]) || !nearZero(right[frame])) {
            std::cerr << "choked voice leaked audio after its event timestamp\n";
            return 16;
        }
    }

    engine.reset();
    left.fill(1.0f);
    right.fill(1.0f);
    NoteEndCapture resetEnds;
    if (!engine.process(nullptr, 8, left.data(), right.data(), resetEnds) ||
        engine.activeCount() != 0 || resetEnds.count != 0) {
        std::cerr << "reset voice engine rejected an empty block or leaked lifecycle state\n";
        return 17;
    }
    for (std::size_t frame = 0; frame < 8; ++frame) {
        if (!nearZero(left[frame]) || !nearZero(right[frame])) {
            std::cerr << "voice engine was not silent after reset\n";
            return 18;
        }
    }

    return 0;
}
