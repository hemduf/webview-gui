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
        attackSamples_ = 0;
        decaySamples_ = 0;
        sustainLevel_ = 1.0f;
        releaseSamples_ = releaseSamples;
        configured_ = true;
        clearVoices();
        return true;
    }

    // Updates the default envelope used by future NOTE_ON allocations. Active
    // voices retain a coherent NOTE_ON snapshot so a control update cannot splice
    // new timing into the middle of an existing attack/decay/release lifecycle.
    // As with the rest of this core, callers must not invoke configuration APIs
    // concurrently with process(); future CLAP parameter integration owns the
    // audio-thread event handoff.
    bool setAmpEnvelope(std::uint32_t attackSamples,
                        std::uint32_t decaySamples,
                        float sustainLevel,
                        std::uint32_t releaseSamples) noexcept {
        if (!configured_ || !std::isfinite(sustainLevel) || sustainLevel < 0.0f ||
            sustainLevel > 1.0f || releaseSamples == 0)
            return false;

        attackSamples_ = attackSamples;
        decaySamples_ = decaySamples;
        sustainLevel_ = sustainLevel;
        releaseSamples_ = releaseSamples;
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

    [[nodiscard]] bool voiceEnvelopeLevel(VoiceAllocator::VoiceIndex index,
                                          float &level) const noexcept {
        if (index >= lifecycle_.capacity() || !voices_[index].active)
            return false;
        level = voices_[index].level;
        return true;
    }

    template <typename NoteEndSink>
    bool process(const clap_input_events_t *events,
                 std::uint32_t framesCount,
                 float *left,
                 float *right,
                 NoteEndSink &noteEndSink) noexcept {
        static_assert(std::is_nothrow_invocable_v<NoteEndSink &, const clap_event_note_t &>,
                      "PolySynth voice engine NOTE_END sink must be noexcept");

        if (!configured_ || framesCount == 0 || !left || !right)
            return false;

        for (std::uint32_t frame = 0; frame < framesCount; ++frame) {
            left[frame] = 0.0f;
            right[frame] = 0.0f;
        }

        // CLAP output events must use an offset strictly smaller than
        // frames_count. A release which reached zero at the previous block's
        // end is therefore retired here at sample 0, before any new sample-zero
        // host event can allocate the same voice slot.
        if (!completeDeferredReleases(noteEndSink))
            return false;

        std::uint32_t cursor = 0;
        bool renderOk = true;
        auto boundarySink = [this, framesCount, &cursor, left, right, &noteEndSink, &renderOk](
                                std::uint32_t time) noexcept {
            if (time < cursor) {
                renderOk = false;
                return;
            }
            if (!renderRange(cursor, time, framesCount, left, right, noteEndSink))
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
        return renderRange(cursor, framesCount, framesCount, left, right, noteEndSink);
    }

private:
    enum class AmpStage : std::uint8_t {
        Attack,
        Decay,
        Sustain,
        Release,
    };

    struct Voice {
        VoiceIdentity identity{};
        std::uint64_t generation = 0;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        float peakLevel = 0.0f;
        float sustainTarget = 0.0f;
        float level = 0.0f;
        float stageStep = 0.0f;
        std::uint32_t stageRemaining = 0;
        std::uint32_t decaySamples = 0;
        std::uint32_t releaseSamples = 0;
        AmpStage ampStage = AmpStage::Sustain;
        bool active = false;
        bool deferredReleaseCompletion = false;
    };

    static constexpr double kReferenceFrequency = 440.0;
    static constexpr double kTwoPi = 6.283185307179586476925286766559;
    static constexpr double kMaximumPhaseIncrement = 0.49;
    static constexpr float kEnvelopeDenormalFloor = 1.0e-20f;

    static float flushEnvelopeDenormal(float value) noexcept {
        return std::fabs(value) < kEnvelopeDenormalFloor ? 0.0f : value;
    }

    void clearVoices() noexcept {
        for (auto &voice : voices_)
            voice = {};
    }

    [[nodiscard]] double phaseIncrementForKey(std::int16_t key) const noexcept {
        const double semitones = static_cast<double>(key - 69) / 12.0;
        const double frequency = kReferenceFrequency * std::exp2(semitones);
        return std::min(frequency / sampleRate_, kMaximumPhaseIncrement);
    }

    static void enterDecayOrSustain(Voice &voice) noexcept {
        if (voice.decaySamples == 0) {
            voice.level = voice.sustainTarget;
            voice.stageStep = 0.0f;
            voice.stageRemaining = 0;
            voice.ampStage = AmpStage::Sustain;
            return;
        }

        voice.ampStage = AmpStage::Decay;
        voice.stageRemaining = voice.decaySamples;
        voice.stageStep = flushEnvelopeDenormal(
            (voice.peakLevel - voice.sustainTarget) /
            static_cast<float>(voice.decaySamples));
    }

    void startVoice(Voice &voice, const VoiceLifecycleEvent &event) const noexcept {
        voice = {};
        voice.identity = event.note.identity;
        voice.generation = event.generation;
        // Starting at a quarter cycle gives the sample-boundary tests a
        // deterministic non-zero first sample when attack is instantaneous.
        voice.phase = 0.25;
        voice.phaseIncrement = phaseIncrementForKey(event.note.identity.key);
        voice.peakLevel = flushEnvelopeDenormal(static_cast<float>(event.note.velocity));
        voice.sustainTarget = flushEnvelopeDenormal(voice.peakLevel * sustainLevel_);
        voice.decaySamples = decaySamples_;
        voice.releaseSamples = releaseSamples_;
        voice.active = true;

        if (attackSamples_ == 0) {
            voice.level = voice.peakLevel;
            enterDecayOrSustain(voice);
            return;
        }

        voice.level = 0.0f;
        voice.ampStage = AmpStage::Attack;
        voice.stageRemaining = attackSamples_;
        voice.stageStep = flushEnvelopeDenormal(
            voice.peakLevel / static_cast<float>(attackSamples_));
    }

    static void beginRelease(Voice &voice) noexcept {
        if (voice.ampStage == AmpStage::Release)
            return;
        voice.ampStage = AmpStage::Release;
        voice.stageRemaining = voice.releaseSamples;
        voice.stageStep = flushEnvelopeDenormal(
            voice.level / static_cast<float>(voice.releaseSamples));
    }

    void applyVoiceEvent(const VoiceLifecycleEvent &event) noexcept {
        const auto index = event.note.voiceIndex;
        if (index >= lifecycle_.capacity())
            return;

        auto &voice = voices_[index];
        switch (event.note.kind) {
            case ScheduledNoteKind::NoteOn:
                startVoice(voice, event);
                return;

            case ScheduledNoteKind::NoteOff:
                if (!voice.active || voice.generation != event.generation ||
                    voice.ampStage == AmpStage::Release)
                    return;
                beginRelease(voice);
                return;

            case ScheduledNoteKind::NoteChoke:
                if (voice.active && voice.generation == event.generation)
                    voice = {};
                return;
        }
    }

    template <typename NoteEndSink>
    bool completeDeferredReleases(NoteEndSink &noteEndSink) noexcept {
        bool ok = true;
        for (VoiceAllocator::VoiceIndex index = 0;
             index < lifecycle_.capacity(); ++index) {
            auto &voice = voices_[index];
            if (!voice.active || !voice.deferredReleaseCompletion)
                continue;

            const auto generation = voice.generation;
            if (!lifecycle_.completeRelease(index, generation, 0, noteEndSink))
                ok = false;
            voice = {};
        }
        return ok;
    }

    template <typename NoteEndSink>
    void advanceEnvelope(VoiceAllocator::VoiceIndex index,
                         Voice &voice,
                         std::uint32_t frame,
                         std::uint32_t blockFrames,
                         NoteEndSink &noteEndSink,
                         bool &ok) noexcept {
        switch (voice.ampStage) {
            case AmpStage::Attack:
                if (voice.stageRemaining == 0) {
                    ok = false;
                    return;
                }
                --voice.stageRemaining;
                if (voice.stageRemaining == 0) {
                    voice.level = voice.peakLevel;
                    enterDecayOrSustain(voice);
                } else {
                    voice.level = flushEnvelopeDenormal(
                        std::min(voice.peakLevel, voice.level + voice.stageStep));
                }
                return;

            case AmpStage::Decay:
                if (voice.stageRemaining == 0) {
                    ok = false;
                    return;
                }
                --voice.stageRemaining;
                if (voice.stageRemaining == 0) {
                    voice.level = voice.sustainTarget;
                    voice.stageStep = 0.0f;
                    voice.ampStage = AmpStage::Sustain;
                } else {
                    voice.level = flushEnvelopeDenormal(
                        std::max(voice.sustainTarget, voice.level - voice.stageStep));
                }
                return;

            case AmpStage::Sustain:
                return;

            case AmpStage::Release:
                if (voice.stageRemaining == 0) {
                    ok = false;
                    voice = {};
                    return;
                }

                --voice.stageRemaining;
                if (voice.stageRemaining == 0) {
                    const auto completionTime = frame + 1u;
                    if (completionTime < blockFrames) {
                        const auto generation = voice.generation;
                        if (!lifecycle_.completeRelease(
                                index, generation, completionTime, noteEndSink))
                            ok = false;
                        voice = {};
                    } else {
                        // Keep the lifecycle slot and generation alive across the
                        // block boundary. The next process call will emit NOTE_END
                        // at offset 0 before scheduling its input events.
                        voice.level = 0.0f;
                        voice.stageStep = 0.0f;
                        voice.deferredReleaseCompletion = true;
                    }
                } else {
                    voice.level = flushEnvelopeDenormal(
                        std::max(0.0f, voice.level - voice.stageStep));
                }
                return;
        }
    }

    template <typename NoteEndSink>
    bool renderRange(std::uint32_t begin,
                     std::uint32_t end,
                     std::uint32_t blockFrames,
                     float *left,
                     float *right,
                     NoteEndSink &noteEndSink) noexcept {
        if (end < begin || end > blockFrames)
            return false;

        bool ok = true;
        for (std::uint32_t frame = begin; frame < end; ++frame) {
            float mix = 0.0f;

            for (VoiceAllocator::VoiceIndex index = 0;
                 index < lifecycle_.capacity(); ++index) {
                auto &voice = voices_[index];
                if (!voice.active || voice.deferredReleaseCompletion)
                    continue;

                const auto sample = static_cast<float>(
                    std::sin(voice.phase * kTwoPi) * static_cast<double>(voice.level));
                mix += sample;

                voice.phase += voice.phaseIncrement;
                if (voice.phase >= 1.0)
                    voice.phase -= 1.0;

                advanceEnvelope(index, voice, frame, blockFrames, noteEndSink, ok);
            }

            left[frame] = mix;
            right[frame] = mix;
        }

        return ok;
    }

    VoiceLifecycle lifecycle_{};
    std::array<Voice, VoiceAllocator::kMaximumVoices> voices_{};
    double sampleRate_ = 0.0;
    std::uint32_t attackSamples_ = 0;
    std::uint32_t decaySamples_ = 0;
    float sustainLevel_ = 1.0f;
    std::uint32_t releaseSamples_ = 0;
    bool configured_ = false;
};

} // namespace webview_gui::examples::polysynth
