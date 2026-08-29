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
        filterEnabled_ = false;
        filterA1_ = 0.0;
        filterA2_ = 0.0;
        filterA3_ = 0.0;
        filterCutoffHz_ = 0.0f;
        filterResonance_ = 0.0f;
        filterEnvelopeAmount_ = 0.0f;
        filterBaseG_ = 0.0;
        filterEnvelopeGDelta_ = 0.0;
        filterDamping_ = 2.0;
        pan_ = 0.0f;
        masterGainCurrent_ = 1.0f;
        masterGainTarget_ = 1.0f;
        masterGainStep_ = 0.0f;
        masterGainRemaining_ = 0;
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

    // Configures a stable two-integrator TPT low-pass snapshot for future
    // NOTE_ON allocations. The resonance control maps [0, 0.99] to damping
    // [2.0, 0.02]. Coefficients are calculated outside process() and copied into
    // each new voice, so active voices cannot acquire zipper/discontinuous
    // coefficient changes from a control update. Future CLAP modulation support
    // can add an explicit audio-thread coefficient handoff without weakening this
    // baseline contract.
    bool setFilter(float cutoffHz, float resonance) noexcept {
        if (!configured_ || !std::isfinite(cutoffHz) || !std::isfinite(resonance) ||
            cutoffHz < kMinimumFilterCutoffHz ||
            static_cast<double>(cutoffHz) > sampleRate_ * kMaximumFilterCutoffFraction ||
            resonance < 0.0f || resonance > kMaximumFilterResonance)
            return false;

        double a1 = 0.0;
        double a2 = 0.0;
        double a3 = 0.0;
        double baseG = 0.0;
        double targetG = 0.0;
        double damping = 0.0;
        if (!prepareFilter(cutoffHz,
                           resonance,
                           filterEnvelopeAmount_,
                           a1,
                           a2,
                           a3,
                           baseG,
                           targetG,
                           damping))
            return false;

        filterA1_ = a1;
        filterA2_ = a2;
        filterA3_ = a3;
        filterCutoffHz_ = cutoffHz;
        filterResonance_ = resonance;
        filterBaseG_ = baseG;
        filterEnvelopeGDelta_ = targetG - baseG;
        filterDamping_ = damping;
        filterEnabled_ = true;
        return true;
    }

    // The baseline core intentionally reuses the normalized amplitude-envelope
    // shape as the filter-envelope source because #31 only requires an amount
    // slot, not a second ADSR. Amount is normalized to [-1, 1] and maps to a
    // +/-4-octave cutoff excursion. Endpoint cutoff and TPT g values are prepared
    // outside process(); voices snapshot them at NOTE_ON, so later default updates
    // cannot alter an active generation.
    bool setFilterEnvelopeAmount(float amount) noexcept {
        if (!configured_ || !std::isfinite(amount) || amount < -1.0f || amount > 1.0f)
            return false;

        if (!filterEnabled_) {
            filterEnvelopeAmount_ = amount;
            return true;
        }

        double a1 = 0.0;
        double a2 = 0.0;
        double a3 = 0.0;
        double baseG = 0.0;
        double targetG = 0.0;
        double damping = 0.0;
        if (!prepareFilter(filterCutoffHz_,
                           filterResonance_,
                           amount,
                           a1,
                           a2,
                           a3,
                           baseG,
                           targetG,
                           damping))
            return false;

        filterEnvelopeAmount_ = amount;
        filterA1_ = a1;
        filterA2_ = a2;
        filterA3_ = a3;
        filterBaseG_ = baseG;
        filterEnvelopeGDelta_ = targetG - baseG;
        filterDamping_ = damping;
        return true;
    }

    // Pan is a NOTE_ON default rather than shared mutable voice state. The
    // center-preserving linear law keeps the existing mono-at-center reference
    // level unchanged while providing deterministic hard-left/right endpoints.
    bool setPan(float pan) noexcept {
        if (!configured_ || !std::isfinite(pan) || pan < -1.0f || pan > 1.0f)
            return false;
        pan_ = pan;
        return true;
    }

    // Master gain is global and can transition while voices are active. The
    // target conversion and ramp step are prepared outside process(); process()
    // performs one bounded add per sample. Future CLAP parameter integration must
    // call this from its audio-thread event handoff rather than concurrently from
    // another thread.
    bool setMasterGainDb(float gainDb, std::uint32_t rampSamples) noexcept {
        if (!configured_ || !std::isfinite(gainDb) ||
            gainDb < kMinimumMasterGainDb || gainDb > kMaximumMasterGainDb ||
            rampSamples == 0)
            return false;

        const double target = std::pow(10.0, static_cast<double>(gainDb) / 20.0);
        if (!std::isfinite(target))
            return false;

        masterGainTarget_ = static_cast<float>(target);
        masterGainStep_ =
            (masterGainTarget_ - masterGainCurrent_) / static_cast<float>(rampSamples);
        masterGainRemaining_ = rampSamples;
        return true;
    }

    void reset() noexcept {
        lifecycle_.reset();
        clearVoices();
        // Reset removes process-history state but retains control values. A
        // partially completed gain ramp is therefore collapsed to its target so
        // the next activation does not inherit an old block-partition history.
        masterGainCurrent_ = masterGainTarget_;
        masterGainStep_ = 0.0f;
        masterGainRemaining_ = 0;
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
        double filterA1 = 0.0;
        double filterA2 = 0.0;
        double filterA3 = 0.0;
        double filterBaseG = 0.0;
        double filterEnvelopeGDelta = 0.0;
        double filterDamping = 2.0;
        double filterIc1Eq = 0.0;
        double filterIc2Eq = 0.0;
        float peakLevel = 0.0f;
        float sustainTarget = 0.0f;
        float level = 0.0f;
        float stageStep = 0.0f;
        float panLeftGain = 1.0f;
        float panRightGain = 1.0f;
        std::uint32_t stageRemaining = 0;
        std::uint32_t decaySamples = 0;
        std::uint32_t releaseSamples = 0;
        AmpStage ampStage = AmpStage::Sustain;
        bool active = false;
        bool deferredReleaseCompletion = false;
        bool filterEnabled = false;
        bool filterEnvelopeEnabled = false;
    };

    static constexpr double kReferenceFrequency = 440.0;
    static constexpr double kPi = 3.1415926535897932384626433832795;
    static constexpr double kTwoPi = 6.283185307179586476925286766559;
    static constexpr double kMaximumPhaseIncrement = 0.49;
    static constexpr float kEnvelopeDenormalFloor = 1.0e-20f;
    static constexpr double kFilterDenormalFloor = 1.0e-30;
    static constexpr float kMinimumFilterCutoffHz = 20.0f;
    static constexpr double kMaximumFilterCutoffFraction = 0.45;
    static constexpr float kMaximumFilterResonance = 0.99f;
    static constexpr double kMaximumFilterEnvelopeOctaves = 4.0;
    static constexpr float kMinimumMasterGainDb = -60.0f;
    static constexpr float kMaximumMasterGainDb = 12.0f;

    static float flushEnvelopeDenormal(float value) noexcept {
        return std::fabs(value) < kEnvelopeDenormalFloor ? 0.0f : value;
    }

    static double flushFilterDenormal(double value) noexcept {
        return std::fabs(value) < kFilterDenormalFloor ? 0.0 : value;
    }

    bool prepareFilter(float cutoffHz,
                       float resonance,
                       float envelopeAmount,
                       double &a1,
                       double &a2,
                       double &a3,
                       double &baseG,
                       double &targetG,
                       double &damping) const noexcept {
        baseG = std::tan(kPi * static_cast<double>(cutoffHz) / sampleRate_);
        damping = 2.0 * (1.0 - static_cast<double>(resonance));
        const double denominator = 1.0 + baseG * (baseG + damping);
        if (!std::isfinite(baseG) || !std::isfinite(damping) ||
            !std::isfinite(denominator) || denominator <= 0.0)
            return false;

        a1 = 1.0 / denominator;
        a2 = baseG * a1;
        a3 = baseG * a2;
        if (!std::isfinite(a1) || !std::isfinite(a2) || !std::isfinite(a3))
            return false;

        const double cutoffMultiplier =
            std::exp2(kMaximumFilterEnvelopeOctaves * static_cast<double>(envelopeAmount));
        const double maximumCutoff = sampleRate_ * kMaximumFilterCutoffFraction;
        const double targetCutoff = std::clamp(
            static_cast<double>(cutoffHz) * cutoffMultiplier,
            static_cast<double>(kMinimumFilterCutoffHz),
            maximumCutoff);
        targetG = std::tan(kPi * targetCutoff / sampleRate_);
        return std::isfinite(cutoffMultiplier) && std::isfinite(targetCutoff) &&
               std::isfinite(targetG) && targetG >= 0.0;
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
        voice.filterEnabled = filterEnabled_;
        voice.filterA1 = filterA1_;
        voice.filterA2 = filterA2_;
        voice.filterA3 = filterA3_;
        voice.filterBaseG = filterBaseG_;
        voice.filterEnvelopeGDelta = filterEnvelopeGDelta_;
        voice.filterDamping = filterDamping_;
        voice.filterEnvelopeEnabled = filterEnabled_ && filterEnvelopeAmount_ != 0.0f;
        if (pan_ <= 0.0f) {
            voice.panLeftGain = 1.0f;
            voice.panRightGain = 1.0f + pan_;
        } else {
            voice.panLeftGain = 1.0f - pan_;
            voice.panRightGain = 1.0f;
        }
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

    static double processFilter(Voice &voice, double input) noexcept {
        if (!voice.filterEnabled)
            return input;

        double a1 = voice.filterA1;
        double a2 = voice.filterA2;
        double a3 = voice.filterA3;
        if (voice.filterEnvelopeEnabled) {
            double normalizedEnvelope = 0.0;
            if (voice.peakLevel > kEnvelopeDenormalFloor) {
                normalizedEnvelope = std::clamp(
                    static_cast<double>(voice.level / voice.peakLevel), 0.0, 1.0);
            }
            const double g = voice.filterBaseG +
                             voice.filterEnvelopeGDelta * normalizedEnvelope;
            const double denominator = 1.0 + g * (g + voice.filterDamping);
            a1 = 1.0 / denominator;
            a2 = g * a1;
            a3 = g * a2;
        }

        const double v3 = input - voice.filterIc2Eq;
        const double v1 = a1 * voice.filterIc1Eq + a2 * v3;
        const double v2 = voice.filterIc2Eq + a2 * voice.filterIc1Eq + a3 * v3;
        voice.filterIc1Eq = flushFilterDenormal(2.0 * v1 - voice.filterIc1Eq);
        voice.filterIc2Eq = flushFilterDenormal(2.0 * v2 - voice.filterIc2Eq);
        return flushFilterDenormal(v2);
    }

    float advanceMasterGain() noexcept {
        if (masterGainRemaining_ == 0)
            return masterGainCurrent_;

        masterGainCurrent_ += masterGainStep_;
        --masterGainRemaining_;
        if (masterGainRemaining_ == 0) {
            masterGainCurrent_ = masterGainTarget_;
            masterGainStep_ = 0.0f;
        }
        return masterGainCurrent_;
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
            float leftMix = 0.0f;
            float rightMix = 0.0f;

            for (VoiceAllocator::VoiceIndex index = 0;
                 index < lifecycle_.capacity(); ++index) {
                auto &voice = voices_[index];
                if (!voice.active || voice.deferredReleaseCompletion)
                    continue;

                const double oscillatorSample =
                    std::sin(voice.phase * kTwoPi) * static_cast<double>(voice.level);
                const auto filteredSample = static_cast<float>(processFilter(voice, oscillatorSample));
                leftMix += filteredSample * voice.panLeftGain;
                rightMix += filteredSample * voice.panRightGain;

                voice.phase += voice.phaseIncrement;
                if (voice.phase >= 1.0)
                    voice.phase -= 1.0;

                advanceEnvelope(index, voice, frame, blockFrames, noteEndSink, ok);
            }

            const float masterGain = advanceMasterGain();
            left[frame] = leftMix * masterGain;
            right[frame] = rightMix * masterGain;
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
    double filterA1_ = 0.0;
    double filterA2_ = 0.0;
    double filterA3_ = 0.0;
    float filterCutoffHz_ = 0.0f;
    float filterResonance_ = 0.0f;
    float filterEnvelopeAmount_ = 0.0f;
    double filterBaseG_ = 0.0;
    double filterEnvelopeGDelta_ = 0.0;
    double filterDamping_ = 2.0;
    float pan_ = 0.0f;
    float masterGainCurrent_ = 1.0f;
    float masterGainTarget_ = 1.0f;
    float masterGainStep_ = 0.0f;
    std::uint32_t masterGainRemaining_ = 0;
    bool filterEnabled_ = false;
    bool configured_ = false;
};

} // namespace webview_gui::examples::polysynth
