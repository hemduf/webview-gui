#pragma once

#include "preset_codec.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace webview_gui::examples::presets {

enum class NativePresetPlatform : std::uint8_t {
    MacOS,
    Windows,
    Linux,
    Unsupported,
};

enum class NativePresetStorageError : std::uint8_t {
    None,
    MissingEnvironment,
    UnsupportedPlatform,
    InvalidBaseRoot,
    InvalidTargetPluginId,
    InvalidIdentity,
    CreateDirectoryFailed,
    EnumerateFailed,
    NotFound,
    OpenFailed,
    ReadFailed,
    SerializeFailed,
    ParseFailed,
    WrongTargetPlugin,
    WriteFailed,
    FlushFailed,
    ReplaceFailed,
    Collision,
    CleanupFailed,
};

struct NativePresetStorageStatus {
    NativePresetStorageError error = NativePresetStorageError::None;
    std::filesystem::path path;
    std::error_code systemError;
    PresetCodecError codecError = PresetCodecError::None;

    [[nodiscard]] bool ok() const noexcept {
        return error == NativePresetStorageError::None;
    }
};

struct NativePresetPathResult {
    NativePresetStorageStatus status;
    std::filesystem::path path;

    [[nodiscard]] bool ok() const noexcept { return status.ok(); }
};

struct NativePresetEnvironment {
    std::string home;
    std::string appData;
    std::string xdgConfigHome;
};

[[nodiscard]] NativePresetPathResult resolveNativePresetBaseRoot(
    NativePresetPlatform platform,
    const NativePresetEnvironment &environment) noexcept;

[[nodiscard]] NativePresetPathResult resolveCurrentNativePresetBaseRoot() noexcept;

[[nodiscard]] NativePresetPathResult nativePresetScopedRoot(
    const std::filesystem::path &baseRoot,
    std::string_view targetPluginId) noexcept;

enum class NativePresetWriteStage : std::uint8_t {
    AfterTemporaryOpen,
    AfterPartialWrite,
    BeforeReplace,
};

using NativePresetWriteFaultHook =
    bool (*)(NativePresetWriteStage stage, void *userData) noexcept;

struct NativePresetStorageOptions {
    NativePresetWriteFaultHook shouldFailWrite = nullptr;
    void *faultUserData = nullptr;
};

struct NativePresetStorageEntry {
    std::string identity;
    std::filesystem::path path;
    PresetMetadata metadata;
};

struct NativePresetStorageDiagnostic {
    NativePresetStorageStatus status;
};

struct NativePresetListResult {
    NativePresetStorageStatus status;
    std::vector<NativePresetStorageEntry> entries;
    std::vector<NativePresetStorageDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept { return status.ok(); }
};

struct NativePresetSaveResult {
    NativePresetStorageStatus status;
    std::string identity;
    std::filesystem::path path;

    [[nodiscard]] bool ok() const noexcept { return status.ok(); }
};

struct NativePresetLoadResult {
    NativePresetStorageStatus status;
    std::optional<PresetDocument> document;

    [[nodiscard]] bool ok() const noexcept {
        return status.ok() && document.has_value();
    }
};

class NativePresetStorage {
public:
    NativePresetStorage(std::filesystem::path baseRoot,
                        std::string targetPluginId,
                        NativePresetStorageOptions options = {});

    NativePresetStorage(const NativePresetStorage &) = delete;
    NativePresetStorage &operator=(const NativePresetStorage &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const noexcept {
        return root_;
    }

    [[nodiscard]] std::string_view targetPluginId() const noexcept {
        return targetPluginId_;
    }

    [[nodiscard]] NativePresetStorageStatus ensureReady() const noexcept;
    [[nodiscard]] NativePresetListResult list() const noexcept;
    [[nodiscard]] NativePresetLoadResult load(std::string_view identity) const noexcept;
    [[nodiscard]] NativePresetSaveResult saveNew(
        const PresetDocument &document) noexcept;
    [[nodiscard]] NativePresetSaveResult replace(
        std::string_view identity,
        const PresetDocument &document) noexcept;

private:
    [[nodiscard]] NativePresetStorageStatus ensureReadyUnlocked() const noexcept;
    [[nodiscard]] NativePresetStorageStatus validateDocumentForStorage(
        const PresetDocument &document,
        PresetSerializeResult &serialized) const noexcept;
    [[nodiscard]] NativePresetStorageStatus writeCanonicalFile(
        const std::filesystem::path &destination,
        std::string_view bytes,
        bool replaceExisting) const noexcept;

    [[nodiscard]] bool shouldFail(NativePresetWriteStage stage) const noexcept;

    std::filesystem::path baseRoot_;
    std::filesystem::path root_;
    std::string targetPluginId_;
    NativePresetStorageOptions options_;
    NativePresetStorageStatus scopeStatus_;
    mutable std::mutex mutex_;
};

} // namespace webview_gui::examples::presets
