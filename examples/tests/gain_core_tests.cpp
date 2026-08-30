#include "gain_event_processor.h"
#include "gain_processor.h"
#include "../common/test_support.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

constexpr float kTolerance = 1.0e-6f;

bool approximatelyEqual(float actual, float expected) noexcept {
    return std::fabs(actual - expected) <= kTolerance;
}

bool expectSample(float actual, float expected, const char *label, std::size_t frame) {
    if (approximatelyEqual(actual, expected))
        return true;

    std::cerr << label << " frame " << frame << ": expected " << expected
              << ", got " << actual << '\n';
    return false;
}

bool expectGain(const webview_gui::examples::gain::GainProcessor &processor,
                double expected,
                const char *label) {
    if (processor.gainDb() == expected)
        return true;

    std::cerr << label << ": expected gain " << expected
              << " dB, got " << processor.gainDb() << " dB\n";
    return false;
}

bool expectConstantRange(const float *channel,
                         std::size_t begin,
                         std::size_t end,
                         float expected,
                         const char *label) {
    for (std::size_t frame = begin; frame < end; ++frame) {
        if (!expectSample(channel[frame], expected, label, frame))
            return false;
    }
    return true;
}

} // namespace

int main() {
    using webview_gui::examples::gain::GainEventProcessor;
    using webview_gui::examples::gain::GainProcessor;
    using webview_gui::examples::gain::kBypassParamId;
    using webview_gui::examples::gain::kGainParamId;
    using webview_gui::examples::test_support::InputEvents;
    using webview_gui::examples::test_support::StereoFloatBlock;

    GainProcessor processor;

    std::array<float, 4> leftIn{0.25f, -0.5f, 1.0f, -1.0f};
    std::array<float, 4> rightIn{-0.125f, 0.75f, -0.25f, 0.5f};
    std::array<float, 4> leftOut{};
    std::array<float, 4> rightOut{};

    const float *inputs[2]{leftIn.data(), rightIn.data()};
    float *outputs[2]{leftOut.data(), rightOut.data()};

    if (!processor.process(inputs, outputs, static_cast<uint32_t>(leftIn.size()))) {
        std::cerr << "default unity processing rejected a valid stereo block\n";
        return 1;
    }

    for (std::size_t i = 0; i < leftIn.size(); ++i) {
        if (!expectSample(leftOut[i], leftIn[i], "unity left", i) ||
            !expectSample(rightOut[i], rightIn[i], "unity right", i))
            return 1;
    }

    if (!processor.setGainDb(-6.0)) {
        std::cerr << "-6 dB was rejected\n";
        return 1;
    }

    const auto minusSix = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
    if (!processor.process(inputs, outputs, static_cast<uint32_t>(leftIn.size())))
        return 1;

    for (std::size_t i = 0; i < leftIn.size(); ++i) {
        if (!expectSample(leftOut[i], leftIn[i] * minusSix, "-6 dB left", i) ||
            !expectSample(rightOut[i], rightIn[i] * minusSix, "-6 dB right", i))
            return 1;
    }

    processor.setBypassed(true);
    if (!processor.bypassed()) {
        std::cerr << "bypass state was not retained\n";
        return 1;
    }
    if (!processor.process(inputs, outputs, static_cast<uint32_t>(leftIn.size())))
        return 1;

    for (std::size_t i = 0; i < leftIn.size(); ++i) {
        if (!expectSample(leftOut[i], leftIn[i], "bypass left", i) ||
            !expectSample(rightOut[i], rightIn[i], "bypass right", i))
            return 1;
    }

    processor.setBypassed(false);
    if (processor.bypassed()) {
        std::cerr << "bypass state did not clear\n";
        return 1;
    }
    if (!processor.setGainDb(6.0)) {
        std::cerr << "+6 dB was rejected\n";
        return 1;
    }

    std::array<float, 4> inPlaceLeft{0.1f, -0.2f, 0.3f, -0.4f};
    std::array<float, 4> inPlaceRight{-0.4f, 0.3f, -0.2f, 0.1f};
    const auto originalLeft = inPlaceLeft;
    const auto originalRight = inPlaceRight;
    const float *inPlaceInputs[2]{inPlaceLeft.data(), inPlaceRight.data()};
    float *inPlaceOutputs[2]{inPlaceLeft.data(), inPlaceRight.data()};
    const auto plusSix = static_cast<float>(std::pow(10.0, 6.0 / 20.0));

    if (!processor.process(inPlaceInputs, inPlaceOutputs,
                           static_cast<uint32_t>(inPlaceLeft.size()))) {
        std::cerr << "in-place processing rejected a valid stereo block\n";
        return 1;
    }

    for (std::size_t i = 0; i < inPlaceLeft.size(); ++i) {
        if (!expectSample(inPlaceLeft[i], originalLeft[i] * plusSix,
                          "in-place left", i) ||
            !expectSample(inPlaceRight[i], originalRight[i] * plusSix,
                          "in-place right", i))
            return 1;
    }

    if (!processor.setGainDb(GainProcessor::kMinimumGainDb)
        || !expectGain(processor, GainProcessor::kMinimumGainDb, "minimum boundary")) {
        std::cerr << "minimum gain boundary was rejected\n";
        return 1;
    }
    if (!processor.setGainDb(GainProcessor::kMaximumGainDb)
        || !expectGain(processor, GainProcessor::kMaximumGainDb, "maximum boundary")) {
        std::cerr << "maximum gain boundary was rejected\n";
        return 1;
    }

    constexpr double preservedGain = 3.0;
    if (!processor.setGainDb(preservedGain))
        return 1;

    const std::array<double, 5> invalidGains{
        GainProcessor::kMinimumGainDb - 0.0001,
        GainProcessor::kMaximumGainDb + 0.0001,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };

    for (const auto invalid : invalidGains) {
        if (processor.setGainDb(invalid)) {
            std::cerr << "invalid gain was accepted\n";
            return 1;
        }
        if (!expectGain(processor, preservedGain, "invalid gain atomicity"))
            return 1;
    }

    if (!processor.process(inputs, outputs, 0)) {
        std::cerr << "zero-frame processing rejected valid channel pointers\n";
        return 1;
    }

    if (processor.process(nullptr, outputs, 1)
        || processor.process(inputs, nullptr, 1)) {
        std::cerr << "null outer buffer table was accepted\n";
        return 1;
    }

    const float *invalidInputs[2]{leftIn.data(), nullptr};
    float *invalidOutputs[2]{leftOut.data(), nullptr};
    if (processor.process(invalidInputs, outputs, 1)
        || processor.process(inputs, invalidOutputs, 1)) {
        std::cerr << "null channel pointer was accepted\n";
        return 1;
    }

    // CLAP event processing: a PARAM_VALUE event must affect audio starting at
    // exactly header.time, never at block start or one sample later.
    {
        GainEventProcessor eventProcessor;
        StereoFloatBlock block(8);
        block.fillInput(1.0f, 1.0f);
        InputEvents events;
        if (!events.pushParamValue(3, kGainParamId, -6.0))
            return 1;

        clap_process_t process{};
        process.frames_count = block.frames();
        process.audio_inputs = block.input();
        process.audio_outputs = block.output();
        process.audio_inputs_count = 1;
        process.audio_outputs_count = 1;
        process.in_events = events.clapInputEvents();

        if (!eventProcessor.process(process)) {
            std::cerr << "sample-accurate gain processing rejected a valid CLAP block\n";
            return 1;
        }

        if (!expectConstantRange(block.outputChannel(0), 0, 3, 1.0f,
                                 "pre-event gain") ||
            !expectConstantRange(block.outputChannel(0), 3, 8, minusSix,
                                 "post-event gain"))
            return 1;
    }

    // Multiple events in one block must preserve exact ordering and timing.
    {
        GainEventProcessor eventProcessor;
        StereoFloatBlock block(8);
        block.fillInput(1.0f, 1.0f);
        InputEvents events;
        if (!events.pushParamValue(2, kGainParamId, -6.0) ||
            !events.pushParamValue(5, kGainParamId, 6.0))
            return 1;

        clap_process_t process{};
        process.frames_count = block.frames();
        process.audio_inputs = block.input();
        process.audio_outputs = block.output();
        process.audio_inputs_count = 1;
        process.audio_outputs_count = 1;
        process.in_events = events.clapInputEvents();

        if (!eventProcessor.process(process))
            return 1;

        if (!expectConstantRange(block.outputChannel(0), 0, 2, 1.0f,
                                 "multi pre") ||
            !expectConstantRange(block.outputChannel(0), 2, 5, minusSix,
                                 "multi middle") ||
            !expectConstantRange(block.outputChannel(0), 5, 8, plusSix,
                                 "multi post"))
            return 1;
    }

    // Bypass is a normal CLAP parameter event and must also switch at the exact
    // sample boundary without discarding the stored gain value.
    {
        GainEventProcessor eventProcessor;
        StereoFloatBlock block(8);
        block.fillInput(1.0f, 1.0f);
        InputEvents events;
        if (!events.pushParamValue(0, kGainParamId, -6.0) ||
            !events.pushParamValue(4, kBypassParamId, 1.0))
            return 1;

        clap_process_t process{};
        process.frames_count = block.frames();
        process.audio_inputs = block.input();
        process.audio_outputs = block.output();
        process.audio_inputs_count = 1;
        process.audio_outputs_count = 1;
        process.in_events = events.clapInputEvents();

        if (!eventProcessor.process(process))
            return 1;

        if (!expectConstantRange(block.outputChannel(0), 0, 4, minusSix,
                                 "bypass pre") ||
            !expectConstantRange(block.outputChannel(0), 4, 8, 1.0f,
                                 "bypass post"))
            return 1;

        if (eventProcessor.processor().gainDb() != -6.0 ||
            !eventProcessor.processor().bypassed()) {
            std::cerr << "bypass event corrupted retained gain state\n";
            return 1;
        }
    }

    // Gain and bypass are global parameters. A host event carrying note/port/
    // channel/key addressing must not be flattened into a global change.
    {
        GainEventProcessor eventProcessor;
        StereoFloatBlock block(4);
        block.fillInput(1.0f, 1.0f);
        InputEvents events;
        if (!events.pushParamValue(0, kGainParamId, -6.0,
                                   42, 0, 1, 60))
            return 1;

        clap_process_t process{};
        process.frames_count = block.frames();
        process.audio_inputs = block.input();
        process.audio_outputs = block.output();
        process.audio_inputs_count = 1;
        process.audio_outputs_count = 1;
        process.in_events = events.clapInputEvents();

        if (!eventProcessor.process(process))
            return 1;

        if (!expectConstantRange(block.outputChannel(0), 0, 4, 1.0f,
                                 "targeted global-param event") ||
            eventProcessor.processor().gainDb() != 0.0) {
            std::cerr << "targeted automation leaked into a global parameter\n";
            return 1;
        }
    }

    return 0;
}
