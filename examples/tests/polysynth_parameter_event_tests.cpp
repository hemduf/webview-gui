#include "polysynth_parameters.h"
#include "polysynth_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
using webview_gui::examples::polysynth::ParameterSlot;
using webview_gui::examples::polysynth::VoiceEngine;

constexpr clap_id kFineTuneId =
    1000u + static_cast<unsigned>(ParameterSlot::FineTuning);

struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(std::uint32_t time, std::int32_t noteId, std::int16_t key) noexcept {
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

    bool pushMod(std::uint32_t time, clap_id paramId, double amount) noexcept {
        auto &event = mods[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_MOD;
        event.param_id = paramId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.amount = amount;
        headers[count++] = &event.header;
        return true;
    }

    bool pushValue(std::uint32_t time, clap_id paramId, double value) noexcept {
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

    std::array<clap_event_note_t, 8> notes{};
    std::array<clap_event_param_mod_t, 8> mods{};
    std::array<clap_event_param_value_t, 8> values{};
    std::array<const clap_event_header_t *, 8> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

struct RenderResult {
    std::array<float, 32> left{};
    std::array<float, 32> right{};
};

bool render(VoiceEngine &engine, InputEvents &events, RenderResult &result) noexcept {
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

bool configure(VoiceEngine &engine) noexcept {
    return engine.configure(4, 48000.0, 16) &&
           engine.setAmpEnvelope(0, 0, 1.0f, 16);
}
}

int main() {
    VoiceEngine reference;
    VoiceEngine modulated;
    VoiceEngine valued;
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
        std::cerr << "VoiceEngine rejected a valid PARAM_MOD event stream\n";
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
    VoiceEngine plain;
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

    return 0;
}
