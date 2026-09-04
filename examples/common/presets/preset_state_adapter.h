#pragma once

#include <cstdint>
#include <optional>

namespace webview_gui::examples::presets {

enum class PresetStateAdapterError : std::uint8_t {
    None,
    WrongTargetPlugin,
    InvalidDocument,
    InvalidKnownParameter,
};

template <typename Snapshot>
struct PresetStateCandidateResult {
    PresetStateAdapterError error = PresetStateAdapterError::None;
    std::optional<Snapshot> candidate;

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetStateAdapterError::None && candidate.has_value();
    }
};

} // namespace webview_gui::examples::presets
