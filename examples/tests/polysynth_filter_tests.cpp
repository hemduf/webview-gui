#include "polysynth_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using webview_gui::examples::polysynth::VoiceEngine;

struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool noteOn(std::uint32_t time,
                std::int32_t noteId,
                std::int16_t key,
                double velocity = 1.0) noexcept {
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = velocity;
        present = true;
        return true;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *list) noexcept {
        if (!list || !list->ctx)
            return 0;
        return static_cast<const InputEvents *>(list->ctx)->present ? 1u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *list,
                                                    std::uint32_t index) noexcept {
        if (!list || !list->ctx || index != 0)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(list->ctx);
        return self.present ? &self.event.header : nullptr;
    }

    clap_event_note_t event{};
    bool present = false;
    clap_input_events_t input{};
};

struct IgnoreNoteEnd {
    void operator()(const clap_event_note_t &) noexcept {}
};

template <std::size_t Frames>
bool renderNote(VoiceEngine &engine,
                std::int32_t noteId,
                std::int16_t key,
                std::array<float, Frames> &left,
                std::array<float, Frames> &right) noexcept {
    InputEvents events;
    events.noteOn(0, noteId, key);
    IgnoreNoteEnd noteEnd;
    return engine.process(&events.input,
                          static_cast<std::uint32_t>(Frames),
                          left.data(),
                          right.data(),
                          noteEnd);
}

template <std::size_t Frames>
double tailEnergy(const std::array<float, Frames> &samples,
                  std::size_t begin) noexcept {
    double energy = 0.0;
    for (std::size_t i = begin; i < Frames; ++i)
        energy += std::fabs(static_cast<double>(samples[i]));
    return energy;
}

template <std::size_t Frames>
bool finiteBlock(const std::array<float, Frames> &left,
                 const std::array<float, Frames> &right) noexcept {
    for (std::size_t i = 0; i < Frames; ++i) {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
            return false;
    }
    return true;
}

template <std::size_t Frames>
bool sameBlock(const std::array<float, Frames> &a,
               const std::array<float, Frames> &b,
               float tolerance = 1.0e-6f) noexcept {
    for (std::size_t i = 0; i < Frames; ++i) {
        if (std::fabs(a[i] - b[i]) > tolerance)
            return false;
    }
    return true;
}

} // namespace

int main() {
    constexpr double sampleRate = 48000.0;

    VoiceEngine validation;
    if (!validation.configure(1, sampleRate, 64))
        return 1;
    if (validation.setFilter(19.0f, 0.0f) ||
        validation.setFilter(21601.0f, 0.0f) ||
        validation.setFilter(1000.0f, -0.01f) ||
        validation.setFilter(1000.0f, 1.0f)) {
        std::cerr << "voice filter accepted an out-of-range cutoff/resonance configuration\n";
        return 2;
    }
    if (!validation.setFilter(12000.0f, 0.25f)) {
        std::cerr << "voice filter rejected a legal cutoff/resonance configuration\n";
        return 3;
    }

    // The filter is a per-voice low-pass. With the same 440 Hz oscillator and
    // envelope, a very low cutoff must substantially attenuate the settled tail
    // compared with a mostly-open cutoff. This is deliberately an energy-ratio
    // contract rather than an implementation-specific coefficient assertion.
    VoiceEngine lowCutoff;
    VoiceEngine highCutoff;
    if (!lowCutoff.configure(1, sampleRate, 64) ||
        !highCutoff.configure(1, sampleRate, 64) ||
        !lowCutoff.setFilter(80.0f, 0.15f) ||
        !highCutoff.setFilter(12000.0f, 0.15f))
        return 4;

    std::array<float, 512> lowLeft{};
    std::array<float, 512> lowRight{};
    std::array<float, 512> highLeft{};
    std::array<float, 512> highRight{};
    if (!renderNote(lowCutoff, 100, 69, lowLeft, lowRight) ||
        !renderNote(highCutoff, 101, 69, highLeft, highRight))
        return 5;

    const auto lowEnergy = tailEnergy(lowLeft, 256);
    const auto highEnergy = tailEnergy(highLeft, 256);
    if (!(highEnergy > 1.0) || !(lowEnergy < highEnergy * 0.25)) {
        std::cerr << "per-voice cutoff did not produce the expected low-pass attenuation\n";
        return 6;
    }

    // Filter defaults are configuration for future NOTE_ON generations. Updating
    // them between process calls must not splice new coefficients into an active
    // voice, which would create a discontinuity and make lifecycle behavior depend
    // on main-thread timing.
    VoiceEngine stableDefaults;
    VoiceEngine changedDefaults;
    if (!stableDefaults.configure(1, sampleRate, 64) ||
        !changedDefaults.configure(1, sampleRate, 64) ||
        !stableDefaults.setFilter(12000.0f, 0.25f) ||
        !changedDefaults.setFilter(12000.0f, 0.25f))
        return 7;

    std::array<float, 64> stableFirstLeft{};
    std::array<float, 64> stableFirstRight{};
    std::array<float, 64> changedFirstLeft{};
    std::array<float, 64> changedFirstRight{};
    if (!renderNote(stableDefaults, 200, 69, stableFirstLeft, stableFirstRight) ||
        !renderNote(changedDefaults, 200, 69, changedFirstLeft, changedFirstRight) ||
        !sameBlock(stableFirstLeft, changedFirstLeft))
        return 8;

    if (!changedDefaults.setFilter(80.0f, 0.95f))
        return 9;

    std::array<float, 64> stableSecondLeft{};
    std::array<float, 64> stableSecondRight{};
    std::array<float, 64> changedSecondLeft{};
    std::array<float, 64> changedSecondRight{};
    IgnoreNoteEnd noteEnd;
    if (!stableDefaults.process(nullptr, 64, stableSecondLeft.data(), stableSecondRight.data(), noteEnd) ||
        !changedDefaults.process(nullptr, 64, changedSecondLeft.data(), changedSecondRight.data(), noteEnd) ||
        !sameBlock(stableSecondLeft, changedSecondLeft) ||
        !sameBlock(stableSecondRight, changedSecondRight)) {
        std::cerr << "active voice filter changed when future-voice defaults were updated\n";
        return 10;
    }

    // Reset/reuse must clear the TPT integrator state. A contaminated voice slot
    // reused after reset must render identically to a fresh engine with the same
    // note and filter snapshot.
    VoiceEngine reused;
    if (!reused.configure(1, sampleRate, 64) || !reused.setFilter(80.0f, 0.99f))
        return 11;
    std::array<float, 256> dirtyLeft{};
    std::array<float, 256> dirtyRight{};
    if (!renderNote(reused, 300, 60, dirtyLeft, dirtyRight))
        return 12;
    reused.reset();
    if (!reused.setFilter(12000.0f, 0.1f))
        return 13;

    VoiceEngine fresh;
    if (!fresh.configure(1, sampleRate, 64) || !fresh.setFilter(12000.0f, 0.1f))
        return 14;
    std::array<float, 64> reusedLeft{};
    std::array<float, 64> reusedRight{};
    std::array<float, 64> freshLeft{};
    std::array<float, 64> freshRight{};
    if (!renderNote(reused, 301, 69, reusedLeft, reusedRight) ||
        !renderNote(fresh, 301, 69, freshLeft, freshRight) ||
        !sameBlock(reusedLeft, freshLeft) || !sameBlock(reusedRight, freshRight)) {
        std::cerr << "voice reuse leaked resonant-filter integrator state\n";
        return 15;
    }

    // The highest legal resonance must remain numerically finite under sustained
    // excitation. The test intentionally exercises the resonant frequency rather
    // than relying only on low-resonance attenuation behavior.
    VoiceEngine resonant;
    if (!resonant.configure(1, sampleRate, 64) ||
        !resonant.setFilter(440.0f, 0.99f))
        return 16;
    std::array<float, 2048> resonantLeft{};
    std::array<float, 2048> resonantRight{};
    if (!renderNote(resonant, 400, 69, resonantLeft, resonantRight) ||
        !finiteBlock(resonantLeft, resonantRight)) {
        std::cerr << "legal high-resonance filter configuration produced NaN/Inf\n";
        return 17;
    }

    return 0;
}
