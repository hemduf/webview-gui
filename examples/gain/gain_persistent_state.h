#pragma once

namespace webview_gui::examples::gain {

struct GainParameterSnapshot {
    float gainDb = 0.0f;
    bool bypassed = false;
};

[[nodiscard]] constexpr GainParameterSnapshot defaultGainParameterSnapshot() noexcept {
    return {};
}

[[nodiscard]] constexpr bool gainParameterSnapshotsEqual(
    const GainParameterSnapshot &a,
    const GainParameterSnapshot &b) noexcept {
    return a.gainDb == b.gainDb && a.bypassed == b.bypassed;
}

} // namespace webview_gui::examples::gain
