#include "polysynth_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using webview_gui::examples::polysynth::VoiceEngine;

struct InputEvents {
    static constexpr std::size_t kCapacity = 8;

    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(std::uint32_t time,
                  std::uint16_t type,
                  std::int32_t noteId,
                  std::int16_t port,
                  std::int16_t channel,
                  std::int16_t key,
                  double velocity = 1.0) noexcept {
        if (count >= kCapacity)
            return false;

        auto &event = notes[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = noteId;
        event.port_index = port;
        event.channel = channel;
        event.key = key;
        event.velocity = velocity;
        headers[count] = &event.header;
        ++count;
        return true;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *list) noexcept {
        if (!list || !list->ctx)
            return 0;
        return static_cast<const InputEvents *>(list->ctx)->count;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *list,
                                                    std::uint32_t index) noexcept {
        if (!list || !list->ctx)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(list->ctx);
        return index < self.count ? self.headers[index] : nullptr;
    }

    std::array<clap_event_note_t, kCapacity> notes{};
    std::array<const clap_event_header_t *, kCapacity> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

struct NoteEndCapture {
    void operator()(const clap_event_note_t &) noexcept { ++count; }
    std::size_t count = 0;
};

constexpr float kSilenceTolerance = 1.0e-7f;
constexpr float kRatioTolerance = 2.0e-5f;

bool nearZero(float value) noexcept {
    return std::fabs(value) <= kSilenceTolerance;
}

bool processNoteOn(VoiceEngine &engine,
                   std::int32_t noteId,
                   std::array<float, 8> &left,
                   std::array<float, 8> &right,
                   std::uint32_t frames,
                   NoteEndCapture &noteEnds) {
    InputEvents events;
    if (!events.pushNote(0, CLAP_EVENT_NOTE_ON, noteId, 0, 0, 69, 1.0))
        return false;
    left.fill(0.0f);
    right.fill(0.0f);
    return engine.process(&events.input, frames, left.data(), right.data(), noteEnds);
}

} // namespace

int main() {
    // Pan is a per-voice default. It must validate its full legal range and each
    // NOTE_ON generation must snapshot a coherent value so later control updates
    // cannot move an already sounding voice between channels.
    VoiceEngine panEngine;
    if (!panEngine.configure(1, 48000.0, 8) ||
        panEngine.setPan(-1.0001f) || panEngine.setPan(1.0001f) ||
        panEngine.setPan(std::numeric_limits<float>::quiet_NaN())) {
        std::cerr << "pan control accepted an invalid value\n";
        return 1;
    }
    if (!panEngine.setPan(-1.0f)) {
        std::cerr << "pan control rejected hard left\n";
        return 2;
    }

    std::array<float, 8> left{};
    std::array<float, 8> right{};
    NoteEndCapture panEnds;
    if (!processNoteOn(panEngine, 10, left, right, 2, panEnds) ||
        nearZero(left[0]) || !nearZero(right[0])) {
        std::cerr << "hard-left pan did not route a new voice deterministically\n";
        return 3;
    }

    if (!panEngine.setPan(1.0f)) {
        std::cerr << "pan control rejected hard right\n";
        return 4;
    }
    left.fill(0.0f);
    right.fill(0.0f);
    if (!panEngine.process(nullptr, 2, left.data(), right.data(), panEnds) ||
        nearZero(left[0]) || !nearZero(right[0])) {
        std::cerr << "active voice pan changed when the default was updated\n";
        return 5;
    }

    InputEvents choke;
    if (!choke.pushNote(0, CLAP_EVENT_NOTE_CHOKE, 10, 0, 0, 69))
        return 6;
    left.fill(0.0f);
    right.fill(0.0f);
    if (!panEngine.process(&choke.input, 1, left.data(), right.data(), panEnds))
        return 7;

    if (!processNoteOn(panEngine, 11, left, right, 2, panEnds) ||
        !nearZero(left[0]) || nearZero(right[0])) {
        std::cerr << "future NOTE_ON did not snapshot the updated pan default\n";
        return 8;
    }

    // Master gain is global and may change while voices are active. The control
    // API therefore supplies an explicit bounded sample ramp. Coefficients must
    // be prepared outside process(), and the ramp must be identical regardless
    // of host block partitioning.
    VoiceEngine reference;
    VoiceEngine ramped;
    VoiceEngine split;
    if (!reference.configure(1, 48000.0, 8) ||
        !ramped.configure(1, 48000.0, 8) ||
        !split.configure(1, 48000.0, 8))
        return 9;

    if (ramped.setMasterGainDb(-60.0001f, 4) ||
        ramped.setMasterGainDb(12.0001f, 4) ||
        ramped.setMasterGainDb(std::numeric_limits<float>::infinity(), 4) ||
        ramped.setMasterGainDb(-6.0f, 0)) {
        std::cerr << "master gain accepted an invalid value or zero-length ramp\n";
        return 10;
    }

    std::array<float, 8> refLeft{};
    std::array<float, 8> refRight{};
    std::array<float, 8> rampLeft{};
    std::array<float, 8> rampRight{};
    std::array<float, 8> splitLeft{};
    std::array<float, 8> splitRight{};
    NoteEndCapture refEnds;
    NoteEndCapture rampEnds;
    NoteEndCapture splitEnds;

    if (!processNoteOn(reference, 20, refLeft, refRight, 1, refEnds) ||
        !processNoteOn(ramped, 20, rampLeft, rampRight, 1, rampEnds) ||
        !processNoteOn(split, 20, splitLeft, splitRight, 1, splitEnds))
        return 11;

    if (!ramped.setMasterGainDb(-6.0f, 4) ||
        !split.setMasterGainDb(-6.0f, 4)) {
        std::cerr << "master gain rejected a legal ramp\n";
        return 12;
    }

    refLeft.fill(0.0f);
    refRight.fill(0.0f);
    rampLeft.fill(0.0f);
    rampRight.fill(0.0f);
    if (!reference.process(nullptr, 4, refLeft.data(), refRight.data(), refEnds) ||
        !ramped.process(nullptr, 4, rampLeft.data(), rampRight.data(), rampEnds))
        return 13;

    std::array<float, 4> ratios{};
    for (std::size_t frame = 0; frame < ratios.size(); ++frame) {
        if (std::fabs(refLeft[frame]) <= 1.0e-4f) {
            std::cerr << "master gain reference fixture hit an unusable zero crossing\n";
            return 14;
        }
        ratios[frame] = rampLeft[frame] / refLeft[frame];
        if (!std::isfinite(ratios[frame]) ||
            std::fabs(rampRight[frame] / refRight[frame] - ratios[frame]) > kRatioTolerance) {
            std::cerr << "master gain ramp diverged between stereo channels\n";
            return 15;
        }
    }

    if (!(ratios[0] < 1.0f && ratios[1] < ratios[0] &&
          ratios[2] < ratios[1] && ratios[3] < ratios[2])) {
        std::cerr << "master gain transition was not a monotonic sample ramp\n";
        return 16;
    }

    const float minusSixLinear = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
    if (std::fabs(ratios[3] - minusSixLinear) > kRatioTolerance) {
        std::cerr << "master gain ramp did not reach the exact target sample\n";
        return 17;
    }

    std::array<float, 2> splitALeft{};
    std::array<float, 2> splitARight{};
    std::array<float, 2> splitBLeft{};
    std::array<float, 2> splitBRight{};
    if (!split.process(nullptr, 2, splitALeft.data(), splitARight.data(), splitEnds) ||
        !split.process(nullptr, 2, splitBLeft.data(), splitBRight.data(), splitEnds))
        return 18;

    for (std::size_t frame = 0; frame < 2; ++frame) {
        if (std::fabs(splitALeft[frame] - rampLeft[frame]) > kRatioTolerance ||
            std::fabs(splitARight[frame] - rampRight[frame]) > kRatioTolerance ||
            std::fabs(splitBLeft[frame] - rampLeft[frame + 2]) > kRatioTolerance ||
            std::fabs(splitBRight[frame] - rampRight[frame + 2]) > kRatioTolerance) {
            std::cerr << "master gain smoothing changed with host block partitioning\n";
            return 19;
        }
    }

    return 0;
}
