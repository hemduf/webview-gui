#pragma once

#include "polysynth_voice_lifecycle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace webview_gui::examples::polysynth {

enum class OscillatorWaveform : std::uint8_t {
    Sine,
    Saw,
    Square,
};

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
        if (!prepareRealtimeFilterGTable())
            return false;
        oscillatorWaveform_ = OscillatorWaveform::Sine;
        coarseTuningSemitones_ = 0;
        fineTuningCents_ = 0.0f;
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
        filterEnvelopeCutoffMultiplier_ = 1.0;
        filterBaseG_ = 0.0;
        filterEnvelopeGDelta_ = 0.0;
        filterBrightnessGDelta_ = 0.0;
        filterMaximumG_ = 0.0;
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

    // Oscillator and tuning controls are NOTE_ON defaults. Active generations
    // retain their waveform and phase increment, preventing a non-event-thread
    // default update from causing a discontinuity in a sounding voice. #32 can
    // layer sample-accurate CLAP modulation on the voice-local pitch state.
    bool setOscillatorWaveform(OscillatorWaveform waveform) noexcept {
        if (!configured_)
            return false;

        switch (waveform) {
            case OscillatorWaveform::Sine:
            case OscillatorWaveform::Saw:
            case OscillatorWaveform::Square:
                oscillatorWaveform_ = waveform;
                return true;
        }
        return false;
    }

    bool setCoarseTuningSemitones(int semitones) noexcept {
        if (!configured_ || semitones < -48 || semitones > 48)
            return false;
        coarseTuningSemitones_ = semitones;
        return true;
    }

    bool setFineTuningCents(float cents) noexcept {
        if (!configured_ || !std::isfinite(cents) || cents < -100.0f || cents > 100.0f)
            return false;
        fineTuningCents_ = cents;
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

        const double maximumG = maximumFilterG();
        const double envelopeCutoffMultiplier =
            std::exp2(kMaximumFilterEnvelopeOctaves *
                      static_cast<double>(filterEnvelopeAmount_));
        if (!std::isfinite(maximumG) || maximumG < baseG ||
            !std::isfinite(envelopeCutoffMultiplier))
            return false;

        filterA1_ = a1;
        filterA2_ = a2;
        filterA3_ = a3;
        filterCutoffHz_ = cutoffHz;
        filterResonance_ = resonance;
        filterEnvelopeCutoffMultiplier_ = envelopeCutoffMultiplier;
        filterBaseG_ = baseG;
        filterEnvelopeGDelta_ = targetG - baseG;
        filterMaximumG_ = maximumG;
        filterBrightnessGDelta_ = maximumG - baseG;
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

        const double envelopeCutoffMultiplier =
            std::exp2(kMaximumFilterEnvelopeOctaves * static_cast<double>(amount));
        if (!std::isfinite(envelopeCutoffMultiplier))
            return false;

        if (!filterEnabled_) {
            filterEnvelopeAmount_ = amount;
            filterEnvelopeCutoffMultiplier_ = envelopeCutoffMultiplier;
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

        const double maximumG = maximumFilterG();
        if (!std::isfinite(maximumG) || maximumG < baseG)
            return false;

        filterEnvelopeAmount_ = amount;
        filterEnvelopeCutoffMultiplier_ = envelopeCutoffMultiplier;
        filterA1_ = a1;
        filterA2_ = a2;
        filterA3_ = a3;
        filterBaseG_ = baseG;
        filterEnvelopeGDelta_ = targetG - baseG;
        filterMaximumG_ = maximumG;
        filterBrightnessGDelta_ = maximumG - baseG;
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

    // Audio-thread pitch update for an already allocated generation. Unlike the
    // +/-100-cent host Fine Tune default, the voice-local effective value may
    // include CLAP's +/-120-semitone TUNING note expression. It can also include
    // up to 96 semitones of compensation when an active voice preserves its
    // NOTE_ON Coarse Tune snapshot while the global default moves between -48
    // and +48. Only the phase increment changes: oscillator phase, envelope,
    // filter state and lifecycle generation remain continuous across the event.
    bool setVoiceFineTuningCents(VoiceAllocator::VoiceIndex index,
                                 float cents) noexcept {
        if (!configured_ || index >= lifecycle_.capacity() || !voices_[index].active ||
            !std::isfinite(cents) ||
            cents < -kMaximumVoicePitchOffsetCents ||
            cents > kMaximumVoicePitchOffsetCents)
            return false;
        voices_[index].phaseIncrement =
            phaseIncrementForKeyAndFine(voices_[index].identity.key, cents);
        return true;
    }

    // Stores the effective post-filter note-expression gain. CLAP VOLUME itself
    // remains validated as (0, 4] by the host-facing adapter. EXPRESSION may
    // produce exact silence, and the reference PRESSURE mapping contributes up
    // to another 2x, so the legal composed internal range is [0, 8].
    bool setVoiceVolumeExpression(VoiceAllocator::VoiceIndex index,
                                  float gain) noexcept {
        if (!configured_ || index >= lifecycle_.capacity() || !voices_[index].active ||
            !std::isfinite(gain) || gain < 0.0f || gain > 8.0f)
            return false;
        voices_[index].noteExpressionGain = gain;
        return true;
    }

    // CLAP PAN is an absolute per-note position in [0, 1]. Map it onto the
    // example's existing center-preserving linear law without touching phase,
    // envelope, filter or lifecycle state.
    bool setVoicePanExpression(VoiceAllocator::VoiceIndex index,
                               float pan) noexcept {
        if (!configured_ || index >= lifecycle_.capacity() || !voices_[index].active ||
            !std::isfinite(pan) || pan < 0.0f || pan > 1.0f)
            return false;

        auto &voice = voices_[index];
        const float signedPan = pan * 2.0f - 1.0f;
        if (signedPan <= 0.0f) {
            voice.panLeftGain = 1.0f;
            voice.panRightGain = 1.0f + signedPan;
        } else {
            voice.panLeftGain = 1.0f - signedPan;
            voice.panRightGain = 1.0f;
        }
        return true;
    }

    // CLAP BRIGHTNESS is a normalized per-note timbral offset in [0, 1]. The
    // example maps 0 to the configured filter cutoff and 1 to the highest legal
    // cutoff. Both endpoints are prepared as TPT g values outside process(), so
    // a sample-accurate event only updates one bounded voice-local scalar.
    bool setVoiceBrightnessExpression(VoiceAllocator::VoiceIndex index,
                                      float brightness) noexcept {
        if (!configured_ || index >= lifecycle_.capacity() || !voices_[index].active ||
            !std::isfinite(brightness) || brightness < 0.0f || brightness > 1.0f)
            return false;
        voices_[index].noteExpressionBrightness = brightness;
        return true;
    }

    // Audio-thread cutoff handoff for an already allocated voice. configure()
    // prepares a fixed lookup from cutoff Hz to the TPT g domain; this path uses
    // only bounded indexing, interpolation and scalar coefficient algebra. It
    // intentionally performs no tan/exp2, allocation, locking, logging or I/O.
    // The filter integrator state is preserved so a sample-accurate event changes
    // the coefficient target without restarting the voice generation.
    bool setVoiceFilterCutoffHz(VoiceAllocator::VoiceIndex index,
                                float cutoffHz) noexcept {
        if (!configured_ || index >= lifecycle_.capacity() || !voices_[index].active ||
            !std::isfinite(cutoffHz))
            return false;

        auto &voice = voices_[index];
        if (!voice.filterEnabled)
            return true;

        const double maximumCutoff = sampleRate_ * kMaximumFilterCutoffFraction;
        const double effectiveCutoff = std::clamp(
            static_cast<double>(cutoffHz),
            static_cast<double>(kMinimumFilterCutoffHz),
            maximumCutoff);
        double baseG = 0.0;
        if (!realtimeFilterG(effectiveCutoff, baseG))
            return false;

        const double targetCutoff = std::clamp(
            effectiveCutoff * voice.filterEnvelopeCutoffMultiplier,
            static_cast<double>(kMinimumFilterCutoffHz),
            maximumCutoff);
        double targetG = 0.0;
        if (!realtimeFilterG(targetCutoff, targetG))
            return false;

        double a1 = 0.0;
        double a2 = 0.0;
        double a3 = 0.0;
        filterCoefficientsForG(baseG, voice.filterDamping, a1, a2, a3);
        if (!std::isfinite(a1) || !std::isfinite(a2) || !std::isfinite(a3))
            return false;

        voice.filterCutoffHz = static_cast<float>(effectiveCutoff);
        voice.filterA1 = a1;
        voice.filterA2 = a2;
        voice.filterA3 = a3;
        voice.filterBaseG = baseG;
        voice.filterEnvelopeGDelta = targetG - baseG;
        voice.filterBrightnessGDelta = voice.filterMaximumG - baseG;
        return true;
    }

    template <typename NoteEndSink>
    bool process(const clap_input_events_t *events,
                 std::uint32_t framesCount,
                 float *left,
                 float *right,
                 NoteEndSink &noteEndSink) noexcept {
        auto ignoredCoreEvent = [](const clap_event_header_t &) noexcept -> bool {
            return true;
        };
        return processWithEvents(events,
                                 framesCount,
                                 left,
                                 right,
                                 ignoredCoreEvent,
                                 noteEndSink);
    }

    template <typename CoreEventSink, typename NoteEndSink>
    bool processWithEvents(const clap_input_events_t *events,
                           std::uint32_t framesCount,
                           float *left,
                           float *right,
                           CoreEventSink &coreEventSink,
                           NoteEndSink &noteEndSink) noexcept {
        static_assert(
            std::is_nothrow_invocable_r_v<bool,
                                          CoreEventSink &,
                                          const clap_event_header_t &>,
            "PolySynth voice engine core-event sink must be noexcept and return bool");
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

        if (!lifecycle_.processWithBoundariesAndEvents(events,
                                                       framesCount,
                                                       boundarySink,
                                                       coreEventSink,
                                                       voiceSink,
                                                       noteEndSink))
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
        double filterBrightnessGDelta = 0.0;
        double filterMaximumG = 0.0;
        double filterDamping = 2.0;
        double filterEnvelopeCutoffMultiplier = 1.0;
        double filterIc1Eq = 0.0;
        double filterIc2Eq = 0.0;
        float filterCutoffHz = 0.0f;
        float peakLevel = 0.0f;
        float sustainTarget = 0.0f;
        float level = 0.0f;
        float stageStep = 0.0f;
        float noteExpressionGain = 1.0f;
        float noteExpressionBrightness = 0.0f;
        float panLeftGain = 1.0f;
        float panRightGain = 1.0f;
        std::uint32_t stageRemaining = 0;
        std::uint32_t decaySamples = 0;
        std::uint32_t releaseSamples = 0;
        AmpStage ampStage = AmpStage::Sustain;
        OscillatorWaveform waveform = OscillatorWaveform::Sine;
        bool active = false;
        bool deferredReleaseCompletion = false;
        bool filterEnabled = false;
        bool filterEnvelopeEnabled = false;
    };

    static constexpr double kReferenceFrequency = 440.0;
    static constexpr double kPi = 3.1415926535897932384626433832795;
    static constexpr double kTwoPi = 6.283185307179586476925286766559;
    static constexpr double kMaximumPhaseIncrement = 0.49;
    // Worst-case active-generation pitch handoff: 96 semitones of Coarse Tune
    // snapshot compensation + 100 cents Fine Tune + 120 semitones CLAP TUNING.
    static constexpr float kMaximumVoicePitchOffsetCents = 21700.0f;
    static constexpr float kEnvelopeDenormalFloor = 1.0e-20f;
    static constexpr double kFilterDenormalFloor = 1.0e-30;
    static constexpr float kMinimumFilterCutoffHz = 20.0f;
    static constexpr double kMaximumFilterCutoffFraction = 0.45;
    static constexpr float kMaximumFilterResonance = 0.99f;
    static constexpr double kMaximumFilterEnvelopeOctaves = 4.0;
    static constexpr std::size_t kRealtimeFilterGTableSize = 2049;
    static constexpr float kMinimumMasterGainDb = -60.0f;
    static constexpr float kMaximumMasterGainDb = 12.0f;

    static float flushEnvelopeDenormal(float value) noexcept {
        return std::fabs(value) < kEnvelopeDenormalFloor ? 0.0f : value;
    }

    static double flushFilterDenormal(double value) noexcept {
        return std::fabs(value) < kFilterDenormalFloor ? 0.0 : value;
    }

    static double maximumFilterG() noexcept {
        return std::tan(kPi * kMaximumFilterCutoffFraction);
    }

    bool prepareRealtimeFilterGTable() noexcept {
        const double minimumCutoff = static_cast<double>(kMinimumFilterCutoffHz);
        const double maximumCutoff = sampleRate_ * kMaximumFilterCutoffFraction;
        const double span = maximumCutoff - minimumCutoff;
        if (!std::isfinite(maximumCutoff) || !(span > 0.0))
            return false;

        for (std::size_t index = 0; index < realtimeFilterGTable_.size(); ++index) {
            const double normalized = static_cast<double>(index) /
                                      static_cast<double>(realtimeFilterGTable_.size() - 1u);
            const double cutoff = minimumCutoff + span * normalized;
            const double g = std::tan(kPi * cutoff / sampleRate_);
            if (!std::isfinite(g) || g < 0.0)
                return false;
            realtimeFilterGTable_[index] = g;
        }
        return true;
    }

    bool realtimeFilterG(double cutoffHz, double &g) const noexcept {
        const double minimumCutoff = static_cast<double>(kMinimumFilterCutoffHz);
        const double maximumCutoff = sampleRate_ * kMaximumFilterCutoffFraction;
        if (!std::isfinite(cutoffHz) || cutoffHz < minimumCutoff ||
            cutoffHz > maximumCutoff || !(maximumCutoff > minimumCutoff))
            return false;

        const double normalized =
            (cutoffHz - minimumCutoff) / (maximumCutoff - minimumCutoff);
        const double tablePosition =
            normalized * static_cast<double>(realtimeFilterGTable_.size() - 1u);
        std::size_t lower = static_cast<std::size_t>(tablePosition);
        if (lower >= realtimeFilterGTable_.size() - 1u) {
            g = realtimeFilterGTable_.back();
            return std::isfinite(g);
        }
        const double fraction = tablePosition - static_cast<double>(lower);
        const double lowerG = realtimeFilterGTable_[lower];
        const double upperG = realtimeFilterGTable_[lower + 1u];
        g = lowerG + (upperG - lowerG) * fraction;
        return std::isfinite(g) && g >= 0.0;
    }

    // Compact polyBLEP correction keeps the discontinuities of the educational
    // saw/square modes bounded without allocating tables or performing any
    // unbounded work on the audio thread. The sine path remains unchanged.
    static double polyBlep(double phase, double phaseIncrement) noexcept {
        if (!(phaseIncrement > 0.0) || phaseIncrement >= 1.0)
            return 0.0;

        if (phase < phaseIncrement) {
            const double t = phase / phaseIncrement;
            return t + t - t * t - 1.0;
        }
        if (phase > 1.0 - phaseIncrement) {
            const double t = (phase - 1.0) / phaseIncrement;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }

    static double oscillatorSample(const Voice &voice) noexcept {
        switch (voice.waveform) {
            case OscillatorWaveform::Sine:
                return std::sin(voice.phase * kTwoPi);

            case OscillatorWaveform::Saw:
                return 2.0 * voice.phase - 1.0 -
                       polyBlep(voice.phase, voice.phaseIncrement);

            case OscillatorWaveform::Square: {
                double sample = voice.phase < 0.5 ? 1.0 : -1.0;
                sample += polyBlep(voice.phase, voice.phaseIncrement);
                double shifted = voice.phase + 0.5;
                if (shifted >= 1.0)
                    shifted -= 1.0;
                sample -= polyBlep(shifted, voice.phaseIncrement);
                return sample;
            }
        }
        return 0.0;
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

    [[nodiscard]] double phaseIncrementForKeyAndFine(std::int16_t key,
                                                     float fineCents) const noexcept {
        const double semitones = static_cast<double>(key - 69) +
                                 static_cast<double>(coarseTuningSemitones_) +
                                 static_cast<double>(fineCents) / 100.0;
        const double frequency = kReferenceFrequency * std::exp2(semitones / 12.0);
        return std::min(frequency / sampleRate_, kMaximumPhaseIncrement);
    }

    [[nodiscard]] double phaseIncrementForKey(std::int16_t key) const noexcept {
        return phaseIncrementForKeyAndFine(key, fineTuningCents_);
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
        voice.waveform = oscillatorWaveform_;
        voice.filterEnabled = filterEnabled_;
        voice.filterA1 = filterA1_;
        voice.filterA2 = filterA2_;
        voice.filterA3 = filterA3_;
        voice.filterCutoffHz = filterCutoffHz_;
        voice.filterBaseG = filterBaseG_;
        voice.filterEnvelopeGDelta = filterEnvelopeGDelta_;
        voice.filterBrightnessGDelta = filterBrightnessGDelta_;
        voice.filterMaximumG = filterMaximumG_;
        voice.filterDamping = filterDamping_;
        voice.filterEnvelopeCutoffMultiplier = filterEnvelopeCutoffMultiplier_;
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

    static void filterCoefficientsForG(double g,
                                       double damping,
                                       double &a1,
                                       double &a2,
                                       double &a3) noexcept {
        const double denominator = 1.0 + g * (g + damping);
        a1 = 1.0 / denominator;
        a2 = g * a1;
        a3 = g * a2;
    }

    static double processFilter(Voice &voice, double input) noexcept {
        if (!voice.filterEnabled)
            return input;

        double a1 = voice.filterA1;
        double a2 = voice.filterA2;
        double a3 = voice.filterA3;
        if (voice.filterEnvelopeEnabled || voice.noteExpressionBrightness > 0.0f) {
            double normalizedEnvelope = 0.0;
            if (voice.filterEnvelopeEnabled &&
                voice.peakLevel > kEnvelopeDenormalFloor) {
                normalizedEnvelope = std::clamp(
                    static_cast<double>(voice.level / voice.peakLevel), 0.0, 1.0);
            }
            const double requestedG =
                voice.filterBaseG +
                voice.filterEnvelopeGDelta * normalizedEnvelope +
                voice.filterBrightnessGDelta *
                    static_cast<double>(voice.noteExpressionBrightness);
            const double g = std::min(voice.filterMaximumG, requestedG);
            filterCoefficientsForG(g, voice.filterDamping, a1, a2, a3);
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

                const double oscillator =
                    oscillatorSample(voice) * static_cast<double>(voice.level);
                const auto filteredSample = static_cast<float>(processFilter(voice, oscillator));
                const auto expressedSample = filteredSample * voice.noteExpressionGain;
                leftMix += expressedSample * voice.panLeftGain;
                rightMix += expressedSample * voice.panRightGain;
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
    std::array<double, kRealtimeFilterGTableSize> realtimeFilterGTable_{};
    double sampleRate_ = 0.0;
    OscillatorWaveform oscillatorWaveform_ = OscillatorWaveform::Sine;
    int coarseTuningSemitones_ = 0;
    float fineTuningCents_ = 0.0f;
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
    double filterEnvelopeCutoffMultiplier_ = 1.0;
    double filterBaseG_ = 0.0;
    double filterEnvelopeGDelta_ = 0.0;
    double filterBrightnessGDelta_ = 0.0;
    double filterMaximumG_ = 0.0;
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