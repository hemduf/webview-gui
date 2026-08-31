#include "polysynth_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using webview_gui::examples::polysynth::VoiceEngine;

struct InputEvents {
    static constexpr std::size_t kCapacity = 4;

    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(std::uint32_t time,
                  std::uint16_t type,
                  std::int32_t noteId,
                  std::int16_t port,
                  std::int16_t channel,
                  std::int16_t key,
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

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *list) noexcept {
        return list && list->ctx ? static_cast<const InputEvents *>(list->ctx)->count : 0;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *list,
                                                    std::uint32_t index) noexcept {
        if (!list || !list->ctx)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(list->ctx);
        return index < self.count ? self.headers[index] : nullptr;
    }

    std::array<clap_event_note_t, kCapacity> notes{};
    std::array<const clap_event_header_t *, kCapacity> headers{};
    std::uint32_t count = 0;
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

bool approximately(float actual, float expected) noexcept {
    return std::fabs(actual - expected) <= 1.0e-6f;
}

} // namespace

int main() {
    VoiceEngine engine;
    if (!engine.configure(1, 48000.0, 4))
        return 1;

    if (engine.maximumActiveReleaseSamples() != 0u) {
        std::cerr << "inactive engine unexpectedly reported an active release snapshot\n";
        return 2;
    }

    if (engine.setAmpEnvelope(4, 4, -0.01f, 4) ||
        engine.setAmpEnvelope(4, 4, 1.01f, 4) ||
        engine.setAmpEnvelope(4, 4, std::numeric_limits<float>::quiet_NaN(), 4) ||
        engine.setAmpEnvelope(4, 4, 0.5f, 0)) {
        std::cerr << "amp envelope accepted an invalid bounded configuration\n";
        return 3;
    }

    if (!engine.setAmpEnvelope(4, 4, 0.5f, 4)) {
        std::cerr << "amp envelope rejected a valid bounded configuration\n";
        return 4;
    }

    InputEvents noteOn;
    if (!noteOn.pushNote(0, CLAP_EVENT_NOTE_ON, 700, 0, 0, 69, 1.0))
        return 5;

    std::array<float, 8> left{};
    std::array<float, 8> right{};
    NoteEndCapture noteEnds;

    // Four attack samples start at exact zero and reach full velocity at the
    // boundary after sample 3. This makes the stage length independent of the
    // host block size while keeping NOTE_ON sample-accurate.
    if (!engine.process(&noteOn.input, 4, left.data(), right.data(), noteEnds))
        return 6;
    if (!approximately(left[0], 0.0f) || !approximately(right[0], 0.0f)) {
        std::cerr << "amp attack did not start from zero at the NOTE_ON sample\n";
        return 7;
    }

    float level = -1.0f;
    if (!engine.voiceEnvelopeLevel(0, level) || !approximately(level, 1.0f)) {
        std::cerr << "four-sample amp attack did not reach peak at the exact boundary\n";
        return 8;
    }
    if (engine.maximumActiveReleaseSamples() != 4u) {
        std::cerr << "active voice did not expose its NOTE_ON release snapshot\n";
        return 9;
    }

    // Four decay samples must reach the configured sustain level exactly,
    // without depending on process block boundaries.
    left.fill(0.0f);
    right.fill(0.0f);
    if (!engine.process(nullptr, 4, left.data(), right.data(), noteEnds) ||
        !engine.voiceEnvelopeLevel(0, level) || !approximately(level, 0.5f)) {
        std::cerr << "four-sample amp decay did not reach sustain exactly\n";
        return 10;
    }

    // Release starts from the current envelope level, not from velocity/peak.
    // After three of four release samples from sustain 0.5 the remaining level
    // must be 0.125; NOTE_END is emitted one sample into the following block.
    InputEvents noteOff;
    if (!noteOff.pushNote(0, CLAP_EVENT_NOTE_OFF, 700, 0, 0, 69))
        return 11;
    left.fill(0.0f);
    right.fill(0.0f);
    if (!engine.process(&noteOff.input, 3, left.data(), right.data(), noteEnds) ||
        !engine.voiceEnvelopeLevel(0, level) || !approximately(level, 0.125f) ||
        noteEnds.count != 0 || engine.maximumActiveReleaseSamples() != 4u) {
        std::cerr << "amp release did not preserve its active release snapshot\n";
        return 12;
    }

    left.fill(0.0f);
    right.fill(0.0f);
    if (!engine.process(nullptr, 2, left.data(), right.data(), noteEnds) ||
        noteEnds.count != 1 || noteEnds.events[0].header.type != CLAP_EVENT_NOTE_END ||
        noteEnds.events[0].header.time != 1 || noteEnds.events[0].note_id != 700 ||
        engine.activeCount() != 0 || engine.maximumActiveReleaseSamples() != 0u) {
        std::cerr << "amp release completion did not retire the release snapshot\n";
        return 13;
    }

    // Zero attack/decay are explicit instantaneous stages. Sustain is applied
    // immediately and the existing fixed release contract remains available.
    engine.reset();
    if (!engine.setAmpEnvelope(0, 0, 0.25f, 4))
        return 14;
    InputEvents immediate;
    if (!immediate.pushNote(0, CLAP_EVENT_NOTE_ON, 701, 0, 0, 60, 0.8))
        return 15;
    left.fill(0.0f);
    right.fill(0.0f);
    NoteEndCapture immediateEnds;
    if (!engine.process(&immediate.input, 2, left.data(), right.data(), immediateEnds) ||
        !engine.voiceEnvelopeLevel(0, level) || !approximately(level, 0.2f) ||
        approximately(left[0], 0.0f) || approximately(right[0], 0.0f) ||
        engine.maximumActiveReleaseSamples() != 4u) {
        std::cerr << "zero-time attack/decay did not enter sustain immediately\n";
        return 16;
    }

    // Specialist review regression: changing the default envelope while a
    // voice is active must not splice new stage lengths into the middle of that
    // voice's current lifecycle. Each NOTE_ON snapshots one coherent ADSR
    // configuration; subsequent configuration changes apply to future voices.
    engine.reset();
    if (!engine.setAmpEnvelope(4, 4, 0.5f, 4))
        return 17;
    InputEvents snapshotOn;
    if (!snapshotOn.pushNote(0, CLAP_EVENT_NOTE_ON, 702, 0, 0, 69, 1.0))
        return 18;
    left.fill(0.0f);
    right.fill(0.0f);
    NoteEndCapture snapshotEnds;
    if (!engine.process(&snapshotOn.input, 2, left.data(), right.data(), snapshotEnds) ||
        !engine.voiceEnvelopeLevel(0, level) || !approximately(level, 0.5f) ||
        engine.maximumActiveReleaseSamples() != 4u)
        return 19;

    if (!engine.setAmpEnvelope(0, 0, 1.0f, 1) ||
        engine.maximumActiveReleaseSamples() != 4u)
        return 20;

    left.fill(0.0f);
    right.fill(0.0f);
    if (!engine.process(nullptr, 2, left.data(), right.data(), snapshotEnds) ||
        !engine.voiceEnvelopeLevel(0, level) || !approximately(level, 1.0f) ||
        engine.maximumActiveReleaseSamples() != 4u) {
        std::cerr << "active voice attack/decay changed when defaults were updated\n";
        return 21;
    }

    left.fill(0.0f);
    right.fill(0.0f);
    if (!engine.process(nullptr, 4, left.data(), right.data(), snapshotEnds) ||
        !engine.voiceEnvelopeLevel(0, level) || !approximately(level, 0.5f) ||
        engine.maximumActiveReleaseSamples() != 4u) {
        std::cerr << "active voice did not preserve its NOTE_ON decay/sustain snapshot\n";
        return 22;
    }

    InputEvents snapshotOff;
    if (!snapshotOff.pushNote(0, CLAP_EVENT_NOTE_OFF, 702, 0, 0, 69))
        return 23;
    left.fill(0.0f);
    right.fill(0.0f);
    if (!engine.process(&snapshotOff.input, 2, left.data(), right.data(), snapshotEnds) ||
        snapshotEnds.count != 0 || !engine.voiceEnvelopeLevel(0, level) ||
        !approximately(level, 0.25f) || engine.maximumActiveReleaseSamples() != 4u) {
        std::cerr << "active voice release length changed after default envelope update\n";
        return 24;
    }

    left.fill(0.0f);
    right.fill(0.0f);
    if (!engine.process(nullptr, 4, left.data(), right.data(), snapshotEnds) ||
        engine.activeCount() != 0 || engine.maximumActiveReleaseSamples() != 0u) {
        std::cerr << "retired generation kept a stale release snapshot\n";
        return 25;
    }

    // The new NOTE_ON generation receives the current one-sample Release default.
    InputEvents shortReleaseOn;
    if (!shortReleaseOn.pushNote(0, CLAP_EVENT_NOTE_ON, 703, 0, 0, 69, 1.0))
        return 26;
    left.fill(0.0f);
    right.fill(0.0f);
    NoteEndCapture shortReleaseEnds;
    if (!engine.process(&shortReleaseOn.input, 1, left.data(), right.data(), shortReleaseEnds) ||
        engine.maximumActiveReleaseSamples() != 1u) {
        std::cerr << "future generation did not expose the updated release snapshot\n";
        return 27;
    }

    return 0;
}
