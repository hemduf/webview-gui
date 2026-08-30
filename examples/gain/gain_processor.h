#pragma once

#include <cmath>
#include <cstdint>

namespace webview_gui::examples::gain {

class GainProcessor {
public:
    static constexpr double kMinimumGainDb = -60.0;
    static constexpr double kMaximumGainDb = 12.0;

    bool setGainDb(double db) noexcept {
        if (!std::isfinite(db) || db < kMinimumGainDb || db > kMaximumGainDb)
            return false;

        gainDb_ = db;
        gainLinear_ = static_cast<float>(std::pow(10.0, db / 20.0));
        return true;
    }

    void setBypassed(bool value) noexcept { bypassed_ = value; }

    double gainDb() const noexcept { return gainDb_; }
    bool bypassed() const noexcept { return bypassed_; }

    bool process(const float *const *inputs,
                 float *const *outputs,
                 uint32_t frames) const noexcept {
        if (!inputs || !outputs || !inputs[0] || !inputs[1]
            || !outputs[0] || !outputs[1])
            return false;

        const float gain = bypassed_ ? 1.0f : gainLinear_;
        for (uint32_t i = 0; i < frames; ++i) {
            const float left = inputs[0][i];
            const float right = inputs[1][i];
            outputs[0][i] = left * gain;
            outputs[1][i] = right * gain;
        }
        return true;
    }

private:
    double gainDb_ = 0.0;
    float gainLinear_ = 1.0f;
    bool bypassed_ = false;
};

} // namespace webview_gui::examples::gain
