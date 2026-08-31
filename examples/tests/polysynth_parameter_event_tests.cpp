#include "polysynth_parameter_voice_engine.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
using webview_gui::examples::polysynth::ParameterSlot;
using webview_gui::examples::polysynth::ParameterVoiceEngine;

constexpr clap_id kMasterGainId =
    1000u + static_cast<unsigned>(ParameterSlot::MasterGain);
constexpr clap_id kWaveformId =
    1000u + static_cast<unsigned>(ParameterSlot::Waveform);
constexpr clap_id kFineTuneId =
    1000u + static_cast<unsigned>(ParameterSlot::FineTuning);
constexpr clap_id kFilterCutoffId =
    1000u + static_cast<unsigned>(ParameterSlot::FilterCutoff);
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kSampleRate = 48000.0;
constexpr double kHalfGainDb = -6.020599913279624;

struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(std::uint32_t time, std::int32_t noteId, std::int16_t key) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = notes[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = 1.0;
        headers[count++] = &event.header;
        return true;
    }

    bool pushMod(std::uint32_t time,
                 clap_id paramId,
                 double amount,
                 std::int32_t noteId = -1,
                 std::int16_t portIndex = -1,
                 std::int16_t channel = -1,
                 std::int16_t key = -1) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = mods[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_MOD;
        event.param_id = paramId;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.amount = amount;
        headers[count++] = &event.header;
        return true;
    }

    bool pushValue(std::uint32_t time,
                   clap_id paramId,
                   double value,
                   std::int32_t noteId = -1,
                   std::int16_t portIndex = -1,
                   std::int16_t channel = -1,
                   std::int16_t key = -1) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = values[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = paramId;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.value = value;
        headers[count++] = &event.header;
        return true;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        return events && events->ctx
                   ? static_cast<const InputEvents *>(events->ctx)->count
                   : 0;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(events->ctx);
        return index < self.count ? self.headers[index] : nullptr;
    }

    std::array<clap_event_note_t, 16> notes{};
    std::array<clap_event_param_mod_t, 16> mods{};
    std::array<clap_event_param_value_t, 16> values{};
    std::array<const clap_event_header_t *, 16> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

struct RenderResult {
    std::array<float, 32> left{};
    std::array<float, 32> right{};
};

bool render(ParameterVoiceEngine &engine, InputEvents &events, RenderResult &result) noexcept {
    auto noteEnd = [](const clap_event_note_t &) noexcept {};
    return engine.process(&events.input,
                          static_cast<std::uint32_t>(result.left.size()),
                          result.left.data(),
                          result.right.data(),
                          noteEnd);
}

bool sameAudio(const RenderResult &a, const RenderResult &b) noexcept {
    for (std::size_t i = 0; i < a.left.size(); ++i) {
        if (std::fabs(a.left[i] - b.left[i]) > 1.0e-5f ||
            std::fabs(a.right[i] - b.right[i]) > 1.0e-5f)
            return false;
    }
    return true;
}

bool configure(ParameterVoiceEngine &engine) noexcept {
    return engine.configure(4, kSampleRate, 16) &&
           engine.setAmpEnvelope(0, 0, 1.0f, 16);
}

double phaseIncrement(std::int16_t key) noexcept {
    const double semitones = static_cast<double>(key - 69);
    return (440.0 * std::exp2(semitones / 12.0)) / kSampleRate;
}

double wrappedPhase(double phase) noexcept {
    phase -= std::floor(phase);
    return phase;
}

bool matchesSingleVoiceRetune(const RenderResult &audio,
                              std::uint32_t retuneFrame,
                              std::int16_t initialKey,
                              std::int16_t retunedKey) noexcept {
    const double initialIncrement = phaseIncrement(initialKey);
    const double retunedIncrement = phaseIncrement(retunedKey);
    for (std::uint32_t frame = 0; frame < audio.left.size(); ++frame) {
        const double phase = frame < retuneFrame
                                 ? 0.25 + static_cast<double>(frame) * initialIncrement
                                 : 0.25 + static_cast<double>(retuneFrame) * initialIncrement +
                                       static_cast<double>(frame - retuneFrame) * retunedIncrement;
        const auto expected = static_cast<float>(std::sin(wrappedPhase(phase) * kTwoPi));
        if (std::fabs(audio.left[frame] - expected) > 1.0e-5f ||
            std::fabs(audio.right[frame] - expected) > 1.0e-5f)
            return false;
    }
    return true;
}

bool matchesTargetedRetune(const RenderResult &audio,
                           std::uint32_t retuneFrame,
                           std::int16_t initialKey,
                           std::int16_t retunedKey) noexcept {
    const double initialIncrement = phaseIncrement(initialKey);
    const double retunedIncrement = phaseIncrement(retunedKey);
    for (std::uint32_t frame = 0; frame < audio.left.size(); ++frame) {
        const double plainPhase = 0.25 + static_cast<double>(frame) * initialIncrement;
        const double targetedPhase = frame < retuneFrame
                                         ? plainPhase
                                         : 0.25 + static_cast<double>(retuneFrame) * initialIncrement +
                                               static_cast<double>(frame - retuneFrame) * retunedIncrement;
        const auto expected = static_cast<float>(
            std::sin(wrappedPhase(plainPhase) * kTwoPi) +
            std::sin(wrappedPhase(targetedPhase) * kTwoPi));
        if (std::fabs(audio.left[frame] - expected) > 1.0e-5f ||
            std::fabs(audio.right[frame] - expected) > 1.0e-5f)
            return false;
    }
    return true;
}

bool matchesMasterGainStep(const RenderResult &audio,
                           std::uint32_t gainFrame,
                           float beforeGain,
                           float afterGain,
                           std::int16_t key) noexcept {
    const double increment = phaseIncrement(key);
    for (std::uint32_t frame = 0; frame < audio.left.size(); ++frame) {
        const auto gain = frame < gainFrame ? beforeGain : afterGain;
        const auto sample = static_cast<float>(std::sin(
            wrappedPhase(0.25 + static_cast<double>(frame) * increment) * kTwoPi));
        const auto expected = sample * gain;
        if (std::fabs(audio.left[frame] - expected) > 1.0e-5f ||
            std::fabs(audio.right[frame] - expected) > 1.0e-5f)
            return false;
    }
    return true;
}
}

int main() {
    ParameterVoiceEngine reference;
    ParameterVoiceEngine modulated;
    ParameterVoiceEngine valued;
    if (!configure(reference) || !configure(modulated) || !configure(valued))
        return 1;

    InputEvents referenceEvents;
    referenceEvents.pushNote(0, 1, 61);
    RenderResult referenceAudio;
    if (!render(reference, referenceEvents, referenceAudio))
        return 2;

    InputEvents modulationEvents;
    modulationEvents.pushMod(0, kFineTuneId, 100.0);
    modulationEvents.pushNote(0, 2, 60);
    RenderResult modulationAudio;
    if (!render(modulated, modulationEvents, modulationAudio)) {
        std::cerr << "ParameterVoiceEngine rejected a valid PARAM_MOD event stream\n";
        return 3;
    }

    double baseValue = -1.0;
    if (!modulated.parameterBaseValue(kFineTuneId, baseValue) ||
        std::fabs(baseValue) > 1.0e-12) {
        std::cerr << "PARAM_MOD overwrote the host-visible base value\n";
        return 4;
    }
    if (!sameAudio(referenceAudio, modulationAudio)) {
        std::cerr << "+100 cent modulation did not produce the next semitone\n";
        return 5;
    }

    InputEvents valueEvents;
    valueEvents.pushValue(0, kFineTuneId, 100.0);
    valueEvents.pushNote(0, 3, 60);
    RenderResult valueAudio;
    if (!render(valued, valueEvents, valueAudio) ||
        !valued.parameterBaseValue(kFineTuneId, baseValue) ||
        std::fabs(baseValue - 100.0) > 1.0e-12) {
        std::cerr << "PARAM_VALUE was not retained as the host-visible base\n";
        return 6;
    }
    if (!sameAudio(referenceAudio, valueAudio)) {
        std::cerr << "+100 cent base value did not produce the next semitone\n";
        return 7;
    }

    modulated.reset();
    ParameterVoiceEngine plain;
    if (!configure(plain))
        return 8;
    InputEvents plainEvents;
    plainEvents.pushNote(0, 4, 60);
    InputEvents resetEvents;
    resetEvents.pushNote(0, 5, 60);
    RenderResult plainAudio;
    RenderResult resetAudio;
    if (!render(plain, plainEvents, plainAudio) ||
        !render(modulated, resetEvents, resetAudio) ||
        !sameAudio(plainAudio, resetAudio)) {
        std::cerr << "reset retained ephemeral modulation\n";
        return 9;
    }

    ParameterVoiceEngine midBlock;
    if (!configure(midBlock))
        return 10;
    InputEvents midBlockEvents;
    midBlockEvents.pushNote(0, 10, 60);
    midBlockEvents.pushMod(8, kFineTuneId, 100.0);
    RenderResult midBlockAudio;
    if (!render(midBlock, midBlockEvents, midBlockAudio) ||
        !matchesSingleVoiceRetune(midBlockAudio, 8, 60, 61)) {
        std::cerr << "mid-block PARAM_MOD was not applied at its exact sample boundary\n";
        return 11;
    }

    ParameterVoiceEngine targeted;
    if (!configure(targeted))
        return 12;
    InputEvents targetedEvents;
    targetedEvents.pushNote(0, 11, 60);
    targetedEvents.pushNote(0, 12, 60);
    targetedEvents.pushMod(8, kFineTuneId, 100.0, 11, 0, 0, 60);
    RenderResult targetedAudio;
    if (!render(targeted, targetedEvents, targetedAudio) ||
        !matchesTargetedRetune(targetedAudio, 8, 60, 61)) {
        std::cerr << "targeted PARAM_MOD leaked across overlapping voices or was quantized\n";
        return 13;
    }

    ParameterVoiceEngine sameSample;
    if (!configure(sameSample))
        return 14;
    InputEvents sameSampleEvents;
    sameSampleEvents.pushNote(0, 21, 60);
    sameSampleEvents.pushMod(0, kFineTuneId, 100.0, 21, 0, 0, 60);
    RenderResult sameSampleAudio;
    if (!render(sameSample, sameSampleEvents, sameSampleAudio) ||
        !matchesSingleVoiceRetune(sameSampleAudio, 0, 60, 61)) {
        std::cerr << "same-sample NOTE_ON then targeted PARAM_MOD did not affect the first sample\n";
        return 15;
    }

    ParameterVoiceEngine gainValue;
    if (!configure(gainValue))
        return 16;
    InputEvents gainValueEvents;
    gainValueEvents.pushNote(0, 31, 60);
    gainValueEvents.pushValue(8, kMasterGainId, kHalfGainDb);
    RenderResult gainValueAudio;
    if (!render(gainValue, gainValueEvents, gainValueAudio) ||
        !matchesMasterGainStep(gainValueAudio, 8, 1.0f, 0.5f, 60)) {
        std::cerr << "Master Gain PARAM_VALUE was not applied at its exact sample boundary\n";
        return 16;
    }
    if (!gainValue.parameterBaseValue(kMasterGainId, baseValue) ||
        std::fabs(baseValue - kHalfGainDb) > 1.0e-9) {
        std::cerr << "Master Gain PARAM_VALUE was not retained as the host-visible base\n";
        return 17;
    }

    ParameterVoiceEngine gainMod;
    if (!configure(gainMod))
        return 18;
    InputEvents gainModEvents;
    gainModEvents.pushValue(0, kMasterGainId, kHalfGainDb);
    gainModEvents.pushNote(0, 32, 60);
    gainModEvents.pushMod(8, kMasterGainId, -kHalfGainDb);
    RenderResult gainModAudio;
    if (!render(gainMod, gainModEvents, gainModAudio) ||
        !matchesMasterGainStep(gainModAudio, 8, 0.5f, 1.0f, 60)) {
        std::cerr << "Master Gain PARAM_MOD was not composed sample-accurately with its base\n";
        return 19;
    }
    if (!gainMod.parameterBaseValue(kMasterGainId, baseValue) ||
        std::fabs(baseValue - kHalfGainDb) > 1.0e-9) {
        std::cerr << "Master Gain PARAM_MOD overwrote the host-visible base value\n";
        return 20;
    }

    gainMod.reset();
    InputEvents gainResetEvents;
    gainResetEvents.pushNote(0, 33, 60);
    RenderResult gainResetAudio;
    if (!render(gainMod, gainResetEvents, gainResetAudio) ||
        !matchesMasterGainStep(gainResetAudio, 0, 0.5f, 0.5f, 60)) {
        std::cerr << "reset did not clear Master Gain modulation while retaining its base\n";
        return 21;
    }

    ParameterVoiceEngine invalidMasterGain;
    if (!configure(invalidMasterGain))
        return 22;
    InputEvents invalidMasterGainEvents;
    invalidMasterGainEvents.pushValue(0, kMasterGainId, kHalfGainDb);
    invalidMasterGainEvents.pushNote(0, 34, 60);
    invalidMasterGainEvents.pushValue(8, kMasterGainId, 24.0);
    RenderResult invalidMasterGainAudio;
    if (!render(invalidMasterGain, invalidMasterGainEvents, invalidMasterGainAudio)) {
        std::cerr << "finite out-of-range Master Gain PARAM_VALUE failed the process block\n";
        return 23;
    }
    if (!matchesMasterGainStep(invalidMasterGainAudio, 0, 0.5f, 0.5f, 60)) {
        std::cerr << "finite out-of-range Master Gain PARAM_VALUE changed the DSP state\n";
        return 24;
    }
    if (!invalidMasterGain.parameterBaseValue(kMasterGainId, baseValue) ||
        std::fabs(baseValue - kHalfGainDb) > 1.0e-9) {
        std::cerr << "finite out-of-range Master Gain PARAM_VALUE changed the base value\n";
        return 25;
    }

    ParameterVoiceEngine waveform;
    if (!configure(waveform))
        return 26;
    InputEvents waveformEvents;
    waveformEvents.pushValue(8, kWaveformId, 1.0);
    waveformEvents.pushNote(8, 35, 60);
    RenderResult waveformAudio;
    if (!render(waveform, waveformEvents, waveformAudio)) {
        std::cerr << "Waveform PARAM_VALUE failed the process block\n";
        return 27;
    }
    if (std::fabs(waveformAudio.left[8] + 0.5f) > 1.0e-5f ||
        std::fabs(waveformAudio.right[8] + 0.5f) > 1.0e-5f) {
        std::cerr << "Waveform PARAM_VALUE was not ordered before same-sample NOTE_ON\n";
        return 28;
    }
    if (!waveform.parameterBaseValue(kWaveformId, baseValue) ||
        std::fabs(baseValue - 1.0) > 1.0e-12) {
        std::cerr << "Waveform PARAM_VALUE was not retained as the host-visible base\n";
        return 29;
    }

    ParameterVoiceEngine cutoff;
    if (!configure(cutoff))
        return 30;
    InputEvents cutoffEvents;
    cutoffEvents.pushValue(0, kFilterCutoffId, 1200.0);
    cutoffEvents.pushNote(0, 41, 60);
    cutoffEvents.pushValue(8, kFilterCutoffId, 2400.0, 41, 0, 0, 60);
    cutoffEvents.pushMod(16, kFilterCutoffId, 300.0);
    RenderResult cutoffAudio;
    if (!render(cutoff, cutoffEvents, cutoffAudio)) {
        std::cerr << "Filter Cutoff parameter state routing failed the process block\n";
        return 31;
    }
    if (!cutoff.parameterBaseValue(kFilterCutoffId, baseValue) ||
        std::fabs(baseValue - 1200.0) > 1.0e-9) {
        std::cerr << "Filter Cutoff global base was not retained independently of targeted value/modulation\n";
        return 32;
    }

    return 0;
}
