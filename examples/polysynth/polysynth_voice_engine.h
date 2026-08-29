#pragma once

#include "polysynth_voice_lifecycle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace webview_gui::examples::polysynth {

class VoiceEngine {
public:
    bool configure(std::size_t requestedVoices,
                   double sampleRate,
                   std::uint32_t releaseSamples) noexcept {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || releaseSamples == 0)
            return false;
        if (!lifecycle_.configure(requestedVoices))
            return false;

        sampleRate_ = sampleRate;
        releaseSamples_ = releaseSamples;
        configured_ = true;
        clearVoices();
        return true;
    }

    void reset() noexcept {
        lifecycle_.reset();
        clearVoices();
    }

    [[nodiscard]] VoiceAllocator::VoiceIndex capacity() const noexcept {
        return lifecycle_.capacity();
    }

    [[nodiscard]] VoiceAllocator::VoiceIndex activeCount() const noexcept {
        return lifecycle_.activeCount();
    }

    [[nodiscard]] bool voiceIdentity(VoiceAllocator::VoiceIndex index,
                                     VoiceIdentity &identity) const noexcept {
        return lifecycle_.voiceIdentity(index, identity);
    }

    template <typename NoteEndSink>
    bool process(const clap_input_events_t *events,
                 std::uint32_t framesCount,
                 float *left,
                 float *right,
                 NoteEndSink &noteEndSink) noexcept {
        static_assert(std::is_nothrow_invocable_v<NoteEndSink &, const clap_event_note_t &>,
                      "PolySynth voice engine NOTE_END sink must be noexcept");

        if (!configured_ || !left || !right)
            return false;

        for (std::uint32_t frame = 0; frame < framesCount; ++frame) {
            left[frame] = 0.0f;
            right[frame] = 0.0f;
        }

        std::uint32_t cursor = 0;
        bool renderOk = true;
        auto boundarySink = [this, &cursor, left, right, &noteEndSink, &renderOk](
                                std::uint32_t time) noexcept {
            if (time < cursor) {
                renderOk = false;
                return;
            }
            if (!renderRange(cursor, time, left, right, noteEndSink))
                renderOk = false;
            cursor = time;
        };
        auto voiceSink = [this](const VoiceLifecycleEvent &event) noexcept {
            applyVoiceEvent(event);
        };

        if (!lifecycle_.processWithBoundaries(
                events, framesCount, boundarySink, voiceSink, noteEndSink))
            return false;
        if (!renderOk)
            return false;
        return renderRange(cursor, framesCount, left, right, noteEndSink);
    }

private:
    struct Voice {
        VoiceIdentity identity{};
        std::uint64_t generation = 0;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        float level = 0.0f;
        float releaseStep = 0.0f;
        std::uint32_t releaseRemaining = 0;
        bool active = false;
        bool releasing = false;
    };

    static constexpr double kReferenceFrequency = 440.0;
    static constexpr double kTwoPi = 6.283185307179586476925286766559;
    static constexpr double kMaximumPhaseIncrement = 0.49;

    void clearVoices() noexcept {
        for (auto &voice : voices_)
            voice = {};
    }

    [[nodiscard]] double phaseIncrementForKey(std::int16_t key) const noexcept {
        const double semitones = static_cast<double>(key - 69) / 12.0;
        const double frequency = kReferenceFrequency * std::exp2(semitones);
        return std::min(frequency / sampleRate_, kMaximumPhaseIncrement);
    }

    void applyVoiceEvent(const VoiceLifecycleEvent &event) noexcept {
        const auto index = event.note.voiceIndex;
        if (index >= lifecycle_.capacity())
            return;

        auto &voice = voices_[index];
        switch (event.note.kind) {
            case ScheduledNoteKind::NoteOn:
                voice = {};
                voice.identity = event.note.identity;
                voice.generation = event.generation;
                // Starting at a quarter cycle gives the sample-boundary tests a
                // deterministic non-zero first sample without special casing the
                // oscillator itself.
                voice.phase = 0.25;
                voice.phaseIncrement = phaseIncrementForKey(event.note.identity.key);
                voice.level = static_cast<float>(event.note.velocity);
                voice.active = true;
                return;

            case ScheduledNoteKind::NoteOff:
                if (!voice.active || voice.generation != event.generation || voice.releasing)
                    return;
                voice.releasing = true;
                voice.releaseRemaining = releaseSamples_;
                voice.releaseStep = voice.level / static_cast<float>(releaseSamples_);
                return;

            case ScheduledNoteKind::NoteChoke:
                if (voice.active && voice.generation == event.generation)
                    voice = {};
                return;
        }
    }

    template <typename NoteEndSink>
    bool renderRange(std::uint32_t begin,
                     std::uint32_t end,
                     float *left,
                     float *right,
                     NoteEndSink &noteEndSink) noexcept {
        if (end < begin)
            return false;

        bool ok = true;
        for (std::uint32_t frame = begin; frame < end; ++frame) {
            float mix = 0.0f;

            for (VoiceAllocator::VoiceIndex index = 0;
                 index < lifecycle_.capacity(); ++index) {
                auto &voice = voices_[index];
                if (!voice.active)
                    continue;

                const auto sample = static_cast<float>(
                    std::sin(voice.phase * kTwoPi) * static_cast<double>(voice.level));
                mix += sample;

                voice.phase += voice.phaseIncrement;
                if (voice.phase >= 1.0)
                    voice.phase -= 1.0;

                if (!voice.releasing)
                    continue;

                if (voice.releaseRemaining == 0) {
                    ok = false;
                    voice = {};
                    continue;
                }

                --voice.releaseRemaining;
                if (voice.releaseRemaining == 0) {
                    const auto generation = voice.generation;
                    if (!lifecycle_.completeRelease(index, generation, frame + 1u, noteEndSink))
                        ok = false;
                    voice = {};
                } else {
                    voice.level = std::max(0.0f, voice.level - voice.releaseStep);
                }
            }

            left[frame] = mix;
            right[frame] = mix;
        }

        return ok;
    }

    VoiceLifecycle lifecycle_{};
    std::array<Voice, VoiceAllocator::kMaximumVoices> voices_{};
    double sampleRate_ = 0.0;
    std::uint32_t releaseSamples_ = 0;
    bool configured_ = false;
};

} // namespace webview_gui::examples::polysynth
