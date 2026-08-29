#pragma once

#include <atomic>
#include <cstdint>

namespace webview_gui::examples::gain {

struct GainMeterSnapshot {
    float leftPeak = 0.0f;
    float rightPeak = 0.0f;
    std::uint32_t sequence = 0;
};

class GainMeterHandoff {
public:
    GainMeterHandoff() noexcept = default;

    GainMeterHandoff(const GainMeterHandoff &) = delete;
    GainMeterHandoff &operator=(const GainMeterHandoff &) = delete;
    GainMeterHandoff(GainMeterHandoff &&) = delete;
    GainMeterHandoff &operator=(GainMeterHandoff &&) = delete;

    // Single audio-thread producer. The odd sequence marks an update in
    // progress; an even sequence is a complete snapshot. Starting odd also
    // makes reads fail closed before the first publication without reserving an
    // otherwise-valid wrapped sequence value. Sequential consistency keeps the
    // two peak atomics inside the sequence transaction with a simple, portable
    // ordering proof across native and WebAssembly-capable C++ toolchains.
    void publish(float leftPeak, float rightPeak) noexcept {
        auto writingSequence = sequence_.load();
        if ((writingSequence & 1u) == 0u)
            ++writingSequence;

        sequence_.store(writingSequence);
        leftPeak_.store(leftPeak);
        rightPeak_.store(rightPeak);
        sequence_.store(writingSequence + 1u);
    }

    // UI/main-thread consumer. Retry count is deliberately bounded: a busy
    // audio publisher causes the UI to keep its previous value rather than
    // spin or ever impede the real-time thread.
    [[nodiscard]] bool tryRead(GainMeterSnapshot &snapshot) const noexcept {
        constexpr unsigned kMaximumAttempts = 3;

        for (unsigned attempt = 0; attempt < kMaximumAttempts; ++attempt) {
            const auto before = sequence_.load();
            if ((before & 1u) != 0u)
                continue;

            const auto leftPeak = leftPeak_.load();
            const auto rightPeak = rightPeak_.load();
            const auto after = sequence_.load();

            if (before == after && (after & 1u) == 0u) {
                snapshot.leftPeak = leftPeak;
                snapshot.rightPeak = rightPeak;
                snapshot.sequence = after;
                return true;
            }
        }

        return false;
    }

private:
    static_assert(std::atomic<float>::is_always_lock_free,
                  "Gain metering requires lock-free float atomics on supported targets");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "Gain metering requires a lock-free 32-bit publication sequence");

    std::atomic<float> leftPeak_{0.0f};
    std::atomic<float> rightPeak_{0.0f};
    std::atomic<std::uint32_t> sequence_{1u};
};

} // namespace webview_gui::examples::gain
