#include "gain_event_processor.h"
#include "../common/test_support.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

constexpr float kTolerance = 1.0e-5f;

bool approximatelyEqual(float actual, float expected) noexcept {
    return std::fabs(actual - expected) <= kTolerance;
}

} // namespace

int main() {
    using webview_gui::examples::gain::GainEventProcessor;
    using webview_gui::examples::gain::GainMeterSnapshot;
    using webview_gui::examples::gain::kGainParamId;
    using webview_gui::examples::test_support::InputEvents;
    using webview_gui::examples::test_support::StereoFloatBlock;

    GainEventProcessor processor;
    GainMeterSnapshot snapshot{};

    if (processor.tryReadMeter(snapshot)) {
        std::cerr << "meter exposed a snapshot before the audio thread published one\n";
        return 1;
    }

    StereoFloatBlock block(8);
    block.fillInput(-0.25f, 0.75f);

    clap_process_t process{};
    process.frames_count = block.frames();
    process.audio_inputs = block.input();
    process.audio_outputs = block.output();
    process.audio_inputs_count = 1;
    process.audio_outputs_count = 1;

    if (!processor.process(process)) {
        std::cerr << "meter fixture rejected a valid stereo block\n";
        return 1;
    }

    if (!processor.tryReadMeter(snapshot)) {
        std::cerr << "meter did not publish the completed audio block\n";
        return 1;
    }

    if (!approximatelyEqual(snapshot.leftPeak, 0.25f) ||
        !approximatelyEqual(snapshot.rightPeak, 0.75f)) {
        std::cerr << "meter peak snapshot does not match the processed stereo output\n";
        return 1;
    }

    const auto firstSequence = snapshot.sequence;

    // The handoff is a latest-value snapshot rather than an unbounded RT queue.
    // A second processed block must replace the previous values and advance the
    // publication sequence without requiring the UI to drain intermediate data.
    block.fillInput(0.5f, 0.25f);
    InputEvents events;
    if (!events.pushParamValue(0, kGainParamId, 6.0))
        return 1;
    process.in_events = events.clapInputEvents();

    if (!processor.process(process))
        return 1;
    if (!processor.tryReadMeter(snapshot)) {
        std::cerr << "meter did not expose the latest completed block\n";
        return 1;
    }

    const auto plusSix = static_cast<float>(std::pow(10.0, 6.0 / 20.0));
    if (!approximatelyEqual(snapshot.leftPeak, 0.5f * plusSix) ||
        !approximatelyEqual(snapshot.rightPeak, 0.25f * plusSix)) {
        std::cerr << "meter did not observe post-gain output samples\n";
        return 1;
    }

    if (snapshot.sequence == firstSequence) {
        std::cerr << "meter publication sequence did not advance\n";
        return 1;
    }

    return 0;
}
