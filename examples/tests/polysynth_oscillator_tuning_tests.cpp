#include "polysynth_parameter_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using webview_gui::examples::polysynth::OscillatorWaveform;
using webview_gui::examples::polysynth::ParameterSlot;
using webview_gui::examples::polysynth::ParameterVoiceEngine;
using webview_gui::examples::polysynth::VoiceEngine;

constexpr clap_id kCoarseTuneId =
    1000u + static_cast<unsigned>(ParameterSlot::CoarseTuning);
constexpr clap_id kFineTuneId =
    1000u + static_cast<unsigned>(ParameterSlot::FineTuning);

template <std::size_t Capacity = 8>
struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(uint32_t time,
                  uint16_t type,
                  int32_t noteId,
                  int16_t key,
                  double velocity = 1.0) noexcept {
        if (count >= Capacity)
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
        event.velocity = velocity;
        headers[count] = &event.header;
        ++count;
        return true;
    }

    bool pushValue(uint32_t time, clap_id paramId, double value) noexcept {
        if (count >= Capacity)
            return false;
        auto &event = values[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = paramId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        headers[count] = &event.header;
        ++count;
        return true;
    }

    static uint32_t CLAP_ABI size(const clap_input_events_t *list) noexcept {
        return list && list->ctx ? static_cast<const InputEvents *>(list->ctx)->count : 0;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *list,
                                                    uint32_t index) noexcept {
        if (!list || !list->ctx)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(list->ctx);
        return index < self.count ? self.headers[index] : nullptr;
    }

    std::array<clap_event_note_t, Capacity> notes{};
    std::array<clap_event_param_value_t, Capacity> values{};
    std::array<const clap_event_header_t *, Capacity> headers{};
    uint32_t count = 0;
    clap_input_events_t input{};
};

struct NoteEndSink {
    void operator()(const clap_event_note_t &) noexcept {}
};

constexpr std::size_t kFrames = 64;
using AudioBlock = std::array<float, kFrames>;

bool renderNote(VoiceEngine &engine,
                int32_t noteId,
                int16_t key,
                AudioBlock &left,
                AudioBlock &right) noexcept {
    InputEvents<> events;
    if (!events.pushNote(0, CLAP_EVENT_NOTE_ON, noteId, key))
        return false;
    NoteEndSink sink;
    return engine.process(&events.input,
                          static_cast<uint32_t>(left.size()),
                          left.data(),
                          right.data(),
                          sink);
}

bool processEmpty(VoiceEngine &engine, AudioBlock &left, AudioBlock &right) noexcept {
    NoteEndSink sink;
    return engine.process(nullptr,
                          static_cast<uint32_t>(left.size()),
                          left.data(),
                          right.data(),
                          sink);
}

bool sameAudio(const AudioBlock &a, const AudioBlock &b, float tolerance = 1.0e-6f) noexcept {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i] - b[i]) > tolerance)
            return false;
    }
    return true;
}

bool differentAudio(const AudioBlock &a, const AudioBlock &b, float tolerance = 1.0e-4f) noexcept {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i] - b[i]) > tolerance)
            return true;
    }
    return false;
}

bool finiteBounded(const AudioBlock &block, float bound = 2.0f) noexcept {
    for (float sample : block) {
        if (!std::isfinite(sample) || std::fabs(sample) > bound)
            return false;
    }
    return true;
}

bool configureReference(VoiceEngine &engine) noexcept {
    return engine.configure(2, 48000.0, 16) &&
           engine.setAmpEnvelope(0, 0, 1.0f, 16);
}

bool configureParameterReference(ParameterVoiceEngine &engine) noexcept {
    return engine.configure(2, 48000.0, 16) &&
           engine.setAmpEnvelope(0, 0, 1.0f, 16);
}

} // namespace

int main() {
    VoiceEngine unconfigured;
    if (unconfigured.setOscillatorWaveform(OscillatorWaveform::Saw) ||
        unconfigured.setCoarseTuningSemitones(12) ||
        unconfigured.setFineTuningCents(25.0f)) {
        std::cerr << "oscillator controls accepted updates before configuration\n";
        return 1;
    }

    VoiceEngine validation;
    if (!configureReference(validation))
        return 2;
    if (!validation.setOscillatorWaveform(OscillatorWaveform::Sine) ||
        !validation.setOscillatorWaveform(OscillatorWaveform::Saw) ||
        !validation.setOscillatorWaveform(OscillatorWaveform::Square) ||
        validation.setOscillatorWaveform(static_cast<OscillatorWaveform>(255)) ||
        !validation.setCoarseTuningSemitones(-48) ||
        !validation.setCoarseTuningSemitones(48) ||
        validation.setCoarseTuningSemitones(-49) ||
        validation.setCoarseTuningSemitones(49) ||
        !validation.setFineTuningCents(-100.0f) ||
        !validation.setFineTuningCents(100.0f) ||
        validation.setFineTuningCents(-100.01f) ||
        validation.setFineTuningCents(100.01f) ||
        validation.setFineTuningCents(std::numeric_limits<float>::quiet_NaN())) {
        std::cerr << "oscillator/tuning range validation is incomplete\n";
        return 3;
    }

    // Coarse and fine defaults must map to pitch exactly. +12 semitones is
    // equivalent to moving the note up one octave; +100 cents is one semitone.
    VoiceEngine coarse;
    VoiceEngine octaveReference;
    if (!configureReference(coarse) || !configureReference(octaveReference) ||
        !coarse.setCoarseTuningSemitones(12))
        return 4;
    AudioBlock coarseLeft{};
    AudioBlock coarseRight{};
    AudioBlock octaveLeft{};
    AudioBlock octaveRight{};
    if (!renderNote(coarse, 1, 69, coarseLeft, coarseRight) ||
        !renderNote(octaveReference, 2, 81, octaveLeft, octaveRight) ||
        !sameAudio(coarseLeft, octaveLeft) || !sameAudio(coarseRight, octaveRight)) {
        std::cerr << "coarse tuning did not map exactly to semitone pitch\n";
        return 5;
    }

    VoiceEngine fine;
    VoiceEngine semitoneReference;
    if (!configureReference(fine) || !configureReference(semitoneReference) ||
        !fine.setFineTuningCents(100.0f))
        return 6;
    AudioBlock fineLeft{};
    AudioBlock fineRight{};
    AudioBlock semitoneLeft{};
    AudioBlock semitoneRight{};
    if (!renderNote(fine, 3, 69, fineLeft, fineRight) ||
        !renderNote(semitoneReference, 4, 70, semitoneLeft, semitoneRight) ||
        !sameAudio(fineLeft, semitoneLeft) || !sameAudio(fineRight, semitoneRight)) {
        std::cerr << "fine tuning did not map cents to pitch exactly\n";
        return 7;
    }

    // Non-sine shapes must be real oscillator choices, remain finite/bounded at
    // high legal pitches, and differ audibly from the sine reference.
    VoiceEngine sine;
    VoiceEngine saw;
    VoiceEngine square;
    if (!configureReference(sine) || !configureReference(saw) || !configureReference(square) ||
        !saw.setOscillatorWaveform(OscillatorWaveform::Saw) ||
        !square.setOscillatorWaveform(OscillatorWaveform::Square))
        return 8;
    AudioBlock sineLeft{};
    AudioBlock sineRight{};
    AudioBlock sawLeft{};
    AudioBlock sawRight{};
    AudioBlock squareLeft{};
    AudioBlock squareRight{};
    if (!renderNote(sine, 10, 110, sineLeft, sineRight) ||
        !renderNote(saw, 11, 110, sawLeft, sawRight) ||
        !renderNote(square, 12, 110, squareLeft, squareRight) ||
        !finiteBounded(sawLeft) || !finiteBounded(sawRight) ||
        !finiteBounded(squareLeft) || !finiteBounded(squareRight) ||
        !differentAudio(sineLeft, sawLeft) || !differentAudio(sineLeft, squareLeft) ||
        !differentAudio(sawLeft, squareLeft)) {
        std::cerr << "waveform choices are not distinct, finite and bounded\n";
        return 9;
    }

    // These setters are default-state updates, not audio-thread modulation.
    // Existing NOTE_ON generations must therefore remain bit-for-bit on their
    // original oscillator/tuning snapshot; only future NOTE_ONs observe changes.
    VoiceEngine snapshot;
    VoiceEngine unchanged;
    if (!configureReference(snapshot) || !configureReference(unchanged))
        return 10;
    AudioBlock firstA{};
    AudioBlock firstARight{};
    AudioBlock firstB{};
    AudioBlock firstBRight{};
    if (!renderNote(snapshot, 20, 69, firstA, firstARight) ||
        !renderNote(unchanged, 21, 69, firstB, firstBRight) ||
        !sameAudio(firstA, firstB))
        return 11;

    if (!snapshot.setOscillatorWaveform(OscillatorWaveform::Saw) ||
        !snapshot.setCoarseTuningSemitones(12) ||
        !snapshot.setFineTuningCents(50.0f))
        return 12;

    AudioBlock continuedA{};
    AudioBlock continuedARight{};
    AudioBlock continuedB{};
    AudioBlock continuedBRight{};
    if (!processEmpty(snapshot, continuedA, continuedARight) ||
        !processEmpty(unchanged, continuedB, continuedBRight) ||
        !sameAudio(continuedA, continuedB) || !sameAudio(continuedARight, continuedBRight)) {
        std::cerr << "default oscillator/tuning update caused a zipper on an active voice\n";
        return 13;
    }

    InputEvents<> replace;
    if (!replace.pushNote(0, CLAP_EVENT_NOTE_CHOKE, 20, 69) ||
        !replace.pushNote(0, CLAP_EVENT_NOTE_ON, 22, 69))
        return 14;
    NoteEndSink sink;
    AudioBlock futureLeft{};
    AudioBlock futureRight{};
    if (!snapshot.process(&replace.input,
                          static_cast<uint32_t>(futureLeft.size()),
                          futureLeft.data(), futureRight.data(), sink))
        return 15;

    VoiceEngine futureReference;
    if (!configureReference(futureReference) ||
        !futureReference.setOscillatorWaveform(OscillatorWaveform::Saw) ||
        !futureReference.setCoarseTuningSemitones(12) ||
        !futureReference.setFineTuningCents(50.0f))
        return 16;
    AudioBlock referenceLeft{};
    AudioBlock referenceRight{};
    if (!renderNote(futureReference, 23, 69, referenceLeft, referenceRight) ||
        !sameAudio(futureLeft, referenceLeft) || !sameAudio(futureRight, referenceRight)) {
        std::cerr << "future NOTE_ON did not snapshot the new oscillator/tuning defaults\n";
        return 17;
    }

    // #32 Coarse Tune starts as a global stepped NOTE_ON default. The CLAP
    // PARAM_VALUE must be consumed before a same-sample NOTE_ON so the new
    // generation snapshots the requested semitone offset, while the adapter
    // retains the base separately for later host-facing publication.
    ParameterVoiceEngine routedCoarse;
    ParameterVoiceEngine stableCoarse;
    VoiceEngine routedOctaveReference;
    if (!configureParameterReference(routedCoarse) ||
        !configureParameterReference(stableCoarse) ||
        !configureReference(routedOctaveReference))
        return 18;
    InputEvents<> routedEvents;
    InputEvents<> stableEvents;
    if (!routedEvents.pushValue(0, kCoarseTuneId, 12.0) ||
        !routedEvents.pushNote(0, CLAP_EVENT_NOTE_ON, 30, 69) ||
        !stableEvents.pushValue(0, kCoarseTuneId, 12.0) ||
        !stableEvents.pushNote(0, CLAP_EVENT_NOTE_ON, 32, 69))
        return 19;
    AudioBlock routedLeft{};
    AudioBlock routedRight{};
    AudioBlock stableLeft{};
    AudioBlock stableRight{};
    if (!routedCoarse.process(&routedEvents.input,
                              static_cast<uint32_t>(routedLeft.size()),
                              routedLeft.data(), routedRight.data(), sink) ||
        !stableCoarse.process(&stableEvents.input,
                              static_cast<uint32_t>(stableLeft.size()),
                              stableLeft.data(), stableRight.data(), sink))
        return 20;
    AudioBlock routedReferenceLeft{};
    AudioBlock routedReferenceRight{};
    if (!renderNote(routedOctaveReference,
                    31,
                    81,
                    routedReferenceLeft,
                    routedReferenceRight) ||
        !sameAudio(routedLeft, routedReferenceLeft) ||
        !sameAudio(routedRight, routedReferenceRight)) {
        std::cerr << "Coarse Tune PARAM_VALUE was not ordered before same-sample NOTE_ON\n";
        return 21;
    }
    double routedBase = -999.0;
    if (!routedCoarse.parameterBaseValue(kCoarseTuneId, routedBase) || routedBase != 12.0) {
        std::cerr << "Coarse Tune PARAM_VALUE was not retained as the adapter base\n";
        return 22;
    }

    // Changing the global Coarse Tune default must not alter an existing
    // generation when a later Fine Tune event recomputes its live pitch. The
    // generation keeps the coarse snapshot taken at NOTE_ON.
    InputEvents<> changedDefaultEvents;
    InputEvents<> stableFineEvents;
    if (!changedDefaultEvents.pushValue(0, kCoarseTuneId, 0.0) ||
        !changedDefaultEvents.pushValue(0, kFineTuneId, 25.0) ||
        !stableFineEvents.pushValue(0, kFineTuneId, 25.0))
        return 23;
    AudioBlock changedDefaultLeft{};
    AudioBlock changedDefaultRight{};
    AudioBlock stableFineLeft{};
    AudioBlock stableFineRight{};
    if (!routedCoarse.process(&changedDefaultEvents.input,
                              static_cast<uint32_t>(changedDefaultLeft.size()),
                              changedDefaultLeft.data(), changedDefaultRight.data(), sink) ||
        !stableCoarse.process(&stableFineEvents.input,
                              static_cast<uint32_t>(stableFineLeft.size()),
                              stableFineLeft.data(), stableFineRight.data(), sink) ||
        !sameAudio(changedDefaultLeft, stableFineLeft) ||
        !sameAudio(changedDefaultRight, stableFineRight)) {
        std::cerr << "Coarse Tune default change leaked into an active voice\n";
        return 24;
    }

    return 0;
}
