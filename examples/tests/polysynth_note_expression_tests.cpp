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
                    std::int16_t key = 60) noexcept {
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
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.value = semitones;
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

    return 0;
}
