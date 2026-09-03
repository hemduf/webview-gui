#pragma once

#include "preset_codec.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webview_gui::examples::presets {

// Format-neutral user-preset storage boundary. This interface deliberately has
// no filesystem dependency so WCLAP/WASI and browser/host-backed persistence can
// use the same preset document/codec contract as native desktop storage.
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
    WrongTargetPlugin,
    SerializeFailed,
    ParseFailed,
    IoFailure,
};

struct PresetStorageStatus {
    PresetStorageError error = PresetStorageError::None;
    PresetCodecError codecError = PresetCodecError::None;

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetStorageError::None;
    }
};

struct PresetStorageEntry {
    std::string identity;
    PresetMetadata metadata;
};

struct PresetStorageListResult {
    PresetStorageStatus status;
    std::vector<PresetStorageEntry> entries;

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
        return {{PresetStorageError::Unavailable}, {}};
    }

    [[nodiscard]] PresetStorageLoadResult load(std::string_view) const override {
        return {{PresetStorageError::Unavailable}, std::nullopt};
    }

    [[nodiscard]] PresetStorageSaveResult saveAs(
        std::string_view,
        const PresetDocument &,
        bool = false) override {
        return {{PresetStorageError::Unavailable}, {}};
    }

    [[nodiscard]] PresetStorageStatus remove(std::string_view) override {
        return {PresetStorageError::Unavailable};
    }

private:
    std::string targetPluginId_;
};

} // namespace webview_gui::examples::presets
