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

constexpr clap_id kFineTuneId =
    1000u + static_cast<unsigned>(ParameterSlot::FineTuning);
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kSampleRate = 48000.0;

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

    bool pushTuning(std::uint32_t time,
                    double semitones,
                    std::int32_t noteId,
                    std::int16_t key = 60,
                    std::int16_t portIndex = 0,
                    std::int16_t channel = 0) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = expressions[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
        event.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.value = semitones;
        headers[count++] = &event.header;
        return true;
    }

    bool pushVolume(std::uint32_t time,
                    double gain,
                    std::int32_t noteId,
                    std::int16_t key = 60,
                    std::int16_t portIndex = 0,
                    std::int16_t channel = 0) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = expressions[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
        event.expression_id = CLAP_NOTE_EXPRESSION_VOLUME;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.value = gain;
        headers[count++] = &event.header;
        return true;
    }

    bool pushFineMod(std::uint32_t time, double cents) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = mods[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_MOD;
        event.param_id = kFineTuneId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.amount = cents;
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

    std::array<clap_event_note_t, 12> notes{};
    std::array<clap_event_note_expression_t, 12> expressions{};
    std::array<clap_event_param_mod_t, 12> mods{};
    std::array<const clap_event_header_t *, 12> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

struct RenderResult {
    std::array<float, 32> left{};
    std::array<float, 32> right{};
};

bool configure(ParameterVoiceEngine &engine) noexcept {
    return engine.configure(4, kSampleRate, 16) &&
           engine.setAmpEnvelope(0, 0, 1.0f, 16);
}

bool render(ParameterVoiceEngine &engine, InputEvents &events, RenderResult &result) noexcept {
    auto noteEnd = [](const clap_event_note_t &) noexcept {};
    return engine.process(&events.input,
                          static_cast<std::uint32_t>(result.left.size()),
                          result.left.data(),
                          result.right.data(),
                          noteEnd);
}

bool sameAudio(const RenderResult &a, const RenderResult &b) noexcept {
    for (std::size_t frame = 0; frame < a.left.size(); ++frame) {
        if (std::fabs(a.left[frame] - b.left[frame]) > 1.0e-5f ||
            std::fabs(a.right[frame] - b.right[frame]) > 1.0e-5f)
            return false;
    }
    return true;
}

double incrementForKey(double key) noexcept {
    return (440.0 * std::exp2((key - 69.0) / 12.0)) / kSampleRate;
}

double wrappedPhase(double phase) noexcept {
    return phase - std::floor(phase);
}

float sineAt(double phase) noexcept {
    return static_cast<float>(std::sin(wrappedPhase(phase) * kTwoPi));
}

bool matchesRetune(const RenderResult &audio,
                   std::uint32_t retuneFrame,
                   double initialKey,
                   double retunedKey,
                   unsigned voiceCount = 1) noexcept {
    const double before = incrementForKey(initialKey);
    const double after = incrementForKey(retunedKey);
    for (std::uint32_t frame = 0; frame < audio.left.size(); ++frame) {
        const double phase = frame < retuneFrame
                                 ? 0.25 + static_cast<double>(frame) * before
                                 : 0.25 + static_cast<double>(retuneFrame) * before +
                                       static_cast<double>(frame - retuneFrame) * after;
        const float expected = sineAt(phase) * static_cast<float>(voiceCount);
        if (std::fabs(audio.left[frame] - expected) > 1.0e-5f ||
            std::fabs(audio.right[frame] - expected) > 1.0e-5f)
            return false;
    }
    return true;
}

bool matchesOneOfTwoRetuned(const RenderResult &audio,
                            std::uint32_t retuneFrame,
                            double initialKey,
                            double retunedKey) noexcept {
    const double before = incrementForKey(initialKey);
    const double after = incrementForKey(retunedKey);
    for (std::uint32_t frame = 0; frame < audio.left.size(); ++frame) {
        const double plainPhase = 0.25 + static_cast<double>(frame) * before;
        const double tunedPhase = frame < retuneFrame
                                      ? plainPhase
                                      : 0.25 + static_cast<double>(retuneFrame) * before +
                                            static_cast<double>(frame - retuneFrame) * after;
        const float expected = sineAt(plainPhase) + sineAt(tunedPhase);
        if (std::fabs(audio.left[frame] - expected) > 1.0e-5f ||
            std::fabs(audio.right[frame] - expected) > 1.0e-5f)
            return false;
    }
    return true;
}

bool matchesRestart(const RenderResult &audio,
                    std::uint32_t restartFrame,
                    double key) noexcept {
    const double increment = incrementForKey(key);
    for (std::uint32_t frame = restartFrame; frame < audio.left.size(); ++frame) {
        const double phase = 0.25 +
                             static_cast<double>(frame - restartFrame) * increment;
        const float expected = sineAt(phase);
        if (std::fabs(audio.left[frame] - expected) > 1.0e-5f ||
            std::fabs(audio.right[frame] - expected) > 1.0e-5f)
            return false;
    }
    return true;
}

bool matchesOneOfTwoVolumeExpression(const RenderResult &audio,
                                     std::uint32_t expressionFrame,
                                     double key,
                                     double targetGain) noexcept {
    const double increment = incrementForKey(key);
    for (std::uint32_t frame = 0; frame < audio.left.size(); ++frame) {
        const float sample = sineAt(0.25 + static_cast<double>(frame) * increment);
        const float expected = sample * static_cast<float>(
            frame < expressionFrame ? 2.0 : 1.0 + targetGain);
        if (std::fabs(audio.left[frame] - expected) > 1.0e-5f ||
            std::fabs(audio.right[frame] - expected) > 1.0e-5f)
            return false;
    }
    return true;
}

bool matchesPostFilterVolumeExpression(const RenderResult &reference,
                                       const RenderResult &expressed,
                                       std::uint32_t expressionFrame,
                                       float targetGain) noexcept {
    for (std::uint32_t frame = 0; frame < reference.left.size(); ++frame) {
        const float gain = frame < expressionFrame ? 1.0f : targetGain;
        if (std::fabs(expressed.left[frame] - reference.left[frame] * gain) > 1.0e-5f ||
            std::fabs(expressed.right[frame] - reference.right[frame] * gain) > 1.0e-5f)
            return false;
    }
    return true;
}
}

int main() {
    ParameterVoiceEngine midBlock;
    if (!configure(midBlock))
        return 1;
    InputEvents midBlockEvents;
    midBlockEvents.pushNote(0, 31, 60);
    midBlockEvents.pushTuning(8, 1.0, 31);
    RenderResult midBlockAudio;
    if (!render(midBlock, midBlockEvents, midBlockAudio) ||
        !matchesRetune(midBlockAudio, 8, 60.0, 61.0)) {
        std::cerr << "NOTE_EXPRESSION_TUNING was not applied at sample 8\n";
        return 2;
    }

    ParameterVoiceEngine isolated;
    if (!configure(isolated))
        return 3;
    InputEvents isolatedEvents;
    isolatedEvents.pushNote(0, 41, 60);
    isolatedEvents.pushNote(0, 42, 60);
    isolatedEvents.pushTuning(8, 1.0, 41);
    RenderResult isolatedAudio;
    if (!render(isolated, isolatedEvents, isolatedAudio) ||
        !matchesOneOfTwoRetuned(isolatedAudio, 8, 60.0, 61.0)) {
        std::cerr << "targeted tuning expression leaked across overlapping voices\n";
        return 4;
    }

    ParameterVoiceEngine composed;
    if (!configure(composed))
        return 5;
    InputEvents composedEvents;
    composedEvents.pushFineMod(0, 50.0);
    composedEvents.pushNote(0, 51, 60);
    composedEvents.pushTuning(0, 0.5, 51);
    RenderResult composedAudio;
    if (!render(composed, composedEvents, composedAudio) ||
        !matchesRetune(composedAudio, 0, 60.0, 61.0)) {
        std::cerr << "parameter modulation and note-expression tuning did not compose\n";
        return 6;
    }

    ParameterVoiceEngine expressionBeforeNote;
    if (!configure(expressionBeforeNote))
        return 7;
    InputEvents expressionBeforeNoteEvents;
    expressionBeforeNoteEvents.pushTuning(0, 1.0, 61);
    expressionBeforeNoteEvents.pushNote(0, 61, 60);
    RenderResult expressionBeforeNoteAudio;
    if (!render(expressionBeforeNote,
                expressionBeforeNoteEvents,
                expressionBeforeNoteAudio) ||
        !matchesRetune(expressionBeforeNoteAudio, 0, 60.0, 61.0)) {
        std::cerr << "same-sample expression before NOTE_ON missed the generated voice\n";
        return 8;
    }

    ParameterVoiceEngine wildcardBeforeNote;
    ParameterVoiceEngine wildcardAfterNote;
    if (!configure(wildcardBeforeNote) || !configure(wildcardAfterNote))
        return 9;

    InputEvents wildcardBeforeEvents;
    wildcardBeforeEvents.pushNote(0, 71, 60);
    wildcardBeforeEvents.pushTuning(8, 1.0, -1);
    wildcardBeforeEvents.pushNote(8, 72, 60);
    RenderResult wildcardBeforeAudio;

    InputEvents wildcardAfterEvents;
    wildcardAfterEvents.pushNote(0, 71, 60);
    wildcardAfterEvents.pushNote(8, 72, 60);
    wildcardAfterEvents.pushTuning(8, 1.0, -1);
    RenderResult wildcardAfterAudio;

    if (!render(wildcardBeforeNote, wildcardBeforeEvents, wildcardBeforeAudio) ||
        !render(wildcardAfterNote, wildcardAfterEvents, wildcardAfterAudio) ||
        !sameAudio(wildcardBeforeAudio, wildcardAfterAudio)) {
        std::cerr << "same-sample wildcard expression missed a later NOTE_ON after matching an existing voice\n";
        return 10;
    }

    ParameterVoiceEngine invalidAddress;
    if (!configure(invalidAddress))
        return 11;
    InputEvents invalidAddressEvents;
    invalidAddressEvents.pushTuning(0, 1.0, -1, 60, 0, 16);
    RenderResult invalidAddressAudio;
    if (render(invalidAddress, invalidAddressEvents, invalidAddressAudio)) {
        std::cerr << "invalid NOTE_EXPRESSION channel was accepted\n";
        return 12;
    }

    ParameterVoiceEngine sameIdentityReuse;
    if (!sameIdentityReuse.configure(1, kSampleRate, 16) ||
        !sameIdentityReuse.setAmpEnvelope(0, 0, 1.0f, 16))
        return 13;
    InputEvents sameIdentityReuseEvents;
    sameIdentityReuseEvents.pushNote(0, -1, 60);
    sameIdentityReuseEvents.pushTuning(4, 1.0, -1);
    sameIdentityReuseEvents.pushNote(8, -1, 60);
    RenderResult sameIdentityReuseAudio;
    if (!render(sameIdentityReuse, sameIdentityReuseEvents, sameIdentityReuseAudio) ||
        !matchesRestart(sameIdentityReuseAudio, 8, 60.0)) {
        std::cerr << "same-identity voice reuse retained the previous generation expression\n";
        return 14;
    }

    ParameterVoiceEngine octaveExpression;
    if (!configure(octaveExpression))
        return 15;
    InputEvents octaveEvents;
    octaveEvents.pushNote(0, 81, 60);
    octaveEvents.pushTuning(8, 12.0, 81);
    RenderResult octaveAudio;
    if (!render(octaveExpression, octaveEvents, octaveAudio) ||
        !matchesRetune(octaveAudio, 8, 60.0, 72.0)) {
        std::cerr << "NOTE_EXPRESSION_TUNING was incorrectly clipped to the Fine Tune parameter range\n";
        return 16;
    }

    ParameterVoiceEngine volumeExpression;
    if (!configure(volumeExpression))
        return 17;
    InputEvents volumeEvents;
    volumeEvents.pushNote(0, 91, 60);
    volumeEvents.pushNote(0, 92, 60);
    volumeEvents.pushVolume(8, 0.5, 91);
    RenderResult volumeAudio;
    if (!render(volumeExpression, volumeEvents, volumeAudio) ||
        !matchesOneOfTwoVolumeExpression(volumeAudio, 8, 60.0, 0.5)) {
        std::cerr << "targeted VOLUME note expression was not sample-accurate or leaked across voices\n";
        return 18;
    }

    ParameterVoiceEngine filteredReference;
    ParameterVoiceEngine filteredExpression;
    if (!configure(filteredReference) || !configure(filteredExpression) ||
        !filteredReference.setFilter(1200.0f, 0.5f) ||
        !filteredExpression.setFilter(1200.0f, 0.5f))
        return 19;

    InputEvents filteredReferenceEvents;
    filteredReferenceEvents.pushNote(0, 101, 60);
    RenderResult filteredReferenceAudio;
    InputEvents filteredExpressionEvents;
    filteredExpressionEvents.pushNote(0, 101, 60);
    filteredExpressionEvents.pushVolume(8, 0.5, 101);
    RenderResult filteredExpressionAudio;
    if (!render(filteredReference, filteredReferenceEvents, filteredReferenceAudio) ||
        !render(filteredExpression, filteredExpressionEvents, filteredExpressionAudio) ||
        !matchesPostFilterVolumeExpression(filteredReferenceAudio,
                                           filteredExpressionAudio,
                                           8,
                                           0.5f)) {
        std::cerr << "VOLUME note expression was applied inside the voice filter instead of at the sample-accurate voice output\n";
        return 20;
    }

    return 0;
}
