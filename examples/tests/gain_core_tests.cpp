#include "gain_processor.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

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

} // namespace

int main() {
    using webview_gui::examples::gain::GainProcessor;

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
    if (!processor.process(inputs, outputs, static_cast<uint32_t>(leftIn.size())))
        return 1;

    for (std::size_t i = 0; i < leftIn.size(); ++i) {
        if (!expectSample(leftOut[i], leftIn[i], "bypass left", i) ||
            !expectSample(rightOut[i], rightIn[i], "bypass right", i))
            return 1;
    }

    processor.setBypassed(false);
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

    return 0;
}
