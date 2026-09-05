#pragma once

#include <cstdint>

namespace webview_gui::examples::presets {

// Portable forward declaration for storage-facing status structures. The
// concrete enum and its stable values remain owned by preset_codec.h so the
// storage abstraction does not pull CHOC/JSON implementation dependencies into
// WASI/browser-only translation units.
enum class PresetCodecError : std::uint8_t;

} // namespace webview_gui::examples::presets
