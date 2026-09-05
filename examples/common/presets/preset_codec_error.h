#pragma once

#include <cstdint>

namespace webview_gui::examples::presets {

enum class PresetCodecError : std::uint8_t {
    None,
    MalformedInput,
    TruncatedInput,
    InputTooLarge,
    NestingTooDeep,
    UnsupportedSchemaVersion,
    WrongTargetPlugin,
    InvalidDocument,
    InvalidParameter,
    DuplicateParameterId,
    NonFiniteParameterValue,
    InvalidSetting,
    MigrationFailed,
};

} // namespace webview_gui::examples::presets
