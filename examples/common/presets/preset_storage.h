#pragma once

#include "preset_codec_error.h"
#include "preset_document.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webview_gui::examples::presets {

// Format-neutral user-preset storage boundary. This interface deliberately has
// no filesystem or codec implementation dependency so WCLAP/WASI and
// browser/host-backed persistence can use the same preset document/status
// contract without pulling CHOC/native storage into module translation units.
enum class PresetStorageKind : std::uint8_t {
    Unavailable,
    Native,
    HostProvided,
};

enum class PresetStorageError : std::uint8_t {
    None,
    Unavailable,
    NotFound,
    AlreadyExists,
    InvalidIdentity,
    InvalidConfiguration,
    OutsideRoot,
    InputTooLarge,
    WrongTargetPlugin,
    SerializeFailed,
    ParseFailed,
    IoFailure,
};

// Portable status plus optional backend diagnostics. Native adapters preserve
// the exact #103 enum value, system error and diagnostic path without forcing
// std::filesystem into WCLAP/WASI/browser consumers. Host backends may leave
// these fields at their defaults.
struct PresetStorageStatus {
    PresetStorageError error = PresetStorageError::None;
    PresetCodecError codecError = PresetCodecError::None;
    std::uint32_t backendErrorCode = 0u;
    int systemErrorCode = 0;
    std::string diagnosticPath;

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetStorageError::None;
    }
};

[[nodiscard]] inline PresetStorageStatus unavailablePresetStorageStatus() {
    PresetStorageStatus status;
    status.error = PresetStorageError::Unavailable;
    return status;
}

struct PresetStorageEntry {
    std::string identity;
    PresetMetadata metadata;
};

struct PresetStorageListResult {
    PresetStorageStatus status;
    std::vector<PresetStorageEntry> entries;
    std::vector<PresetStorageStatus> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return status.ok(); }
};

struct PresetStorageLoadResult {
    PresetStorageStatus status;
    std::optional<PresetDocument> document;

    [[nodiscard]] bool ok() const noexcept {
        return status.ok() && document.has_value();
    }
};

struct PresetStorageSaveResult {
    PresetStorageStatus status;
    std::string identity;

    [[nodiscard]] bool ok() const noexcept { return status.ok(); }
};

class PresetUserStorage {
public:
    virtual ~PresetUserStorage() = default;

    [[nodiscard]] virtual PresetStorageKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::string_view targetPluginId() const noexcept = 0;

    // Native desktop backends may expose their verified FILE-discovery root.
    // Host/browser/WASI backends return nullopt and must never synthesize a path.
    [[nodiscard]] virtual std::optional<std::string> nativeFileRoot() const = 0;

    [[nodiscard]] virtual PresetStorageListResult list() const = 0;
    [[nodiscard]] virtual PresetStorageLoadResult load(std::string_view identity) const = 0;
    [[nodiscard]] virtual PresetStorageSaveResult saveAs(
        std::string_view identity,
        const PresetDocument &document,
        bool overwrite = false) = 0;
    [[nodiscard]] virtual PresetStorageStatus remove(std::string_view identity) = 0;
};

// Explicit no-user-storage backend for WCLAP/WASI when the host/browser has not
// supplied persistence. Factory catalogs are independent and remain available.
class UnavailablePresetUserStorage final : public PresetUserStorage {
public:
    explicit UnavailablePresetUserStorage(std::string targetPluginId)
        : targetPluginId_(std::move(targetPluginId)) {}

    [[nodiscard]] PresetStorageKind kind() const noexcept override {
        return PresetStorageKind::Unavailable;
    }

    [[nodiscard]] std::string_view targetPluginId() const noexcept override {
        return targetPluginId_;
    }

    [[nodiscard]] std::optional<std::string> nativeFileRoot() const override {
        return std::nullopt;
    }

    [[nodiscard]] PresetStorageListResult list() const override {
        return {unavailablePresetStorageStatus(), {}, {}};
    }

    [[nodiscard]] PresetStorageLoadResult load(std::string_view) const override {
        return {unavailablePresetStorageStatus(), std::nullopt};
    }

    [[nodiscard]] PresetStorageSaveResult saveAs(
        std::string_view,
        const PresetDocument &,
        bool = false) override {
        return {unavailablePresetStorageStatus(), {}};
    }

    [[nodiscard]] PresetStorageStatus remove(std::string_view) override {
        return unavailablePresetStorageStatus();
    }

private:
    std::string targetPluginId_;
};

} // namespace webview_gui::examples::presets
