#pragma once

#include "native_preset_storage.h"
#include "preset_storage.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace webview_gui::examples::presets {

[[nodiscard]] inline PresetStorageError portableStorageError(
    NativePresetStorageError error) noexcept {
    switch (error) {
        case NativePresetStorageError::None:
            return PresetStorageError::None;
        case NativePresetStorageError::MissingEnvironment:
        case NativePresetStorageError::UnsupportedPlatform:
            return PresetStorageError::Unavailable;
        case NativePresetStorageError::InvalidBaseRoot:
        case NativePresetStorageError::InvalidTargetPluginId:
            return PresetStorageError::InvalidConfiguration;
        case NativePresetStorageError::InvalidIdentity:
        case NativePresetStorageError::InvalidName:
            return PresetStorageError::InvalidIdentity;
        case NativePresetStorageError::OutsideRoot:
            return PresetStorageError::OutsideRoot;
        case NativePresetStorageError::InputTooLarge:
            return PresetStorageError::InputTooLarge;
        case NativePresetStorageError::NotFound:
            return PresetStorageError::NotFound;
        case NativePresetStorageError::AlreadyExists:
            return PresetStorageError::AlreadyExists;
        case NativePresetStorageError::SerializeFailed:
            return PresetStorageError::SerializeFailed;
        case NativePresetStorageError::ParseFailed:
            return PresetStorageError::ParseFailed;
        case NativePresetStorageError::WrongTargetPlugin:
            return PresetStorageError::WrongTargetPlugin;
        case NativePresetStorageError::CreateDirectoryFailed:
        case NativePresetStorageError::EnumerateFailed:
        case NativePresetStorageError::OpenFailed:
        case NativePresetStorageError::ReadFailed:
        case NativePresetStorageError::WriteFailed:
        case NativePresetStorageError::FlushFailed:
        case NativePresetStorageError::ReplaceFailed:
        case NativePresetStorageError::DeleteFailed:
        case NativePresetStorageError::Collision:
        case NativePresetStorageError::CleanupFailed:
            return PresetStorageError::IoFailure;
    }
    return PresetStorageError::IoFailure;
}

[[nodiscard]] inline PresetStorageStatus portableStorageStatus(
    const NativePresetStorageStatus &status) {
    PresetStorageStatus result;
    result.error = portableStorageError(status.error);
    result.codecError = status.codecError;
    result.backendErrorCode = static_cast<std::uint32_t>(status.error);
    result.systemErrorCode = status.systemError.value();
    result.diagnosticPath = status.path.generic_string();
    return result;
}

// Production native adapter for the filesystem-free #104 storage boundary.
// All path validation, capability-pinned I/O and atomic-write behavior remains
// owned by the canonical #103 NativePresetStorage implementation. This adapter
// only translates result envelopes and never reimplements storage semantics.
class NativePresetUserStorage final : public PresetUserStorage {
public:
    NativePresetUserStorage(std::filesystem::path baseRoot,
                            std::string targetPluginId,
                            NativePresetStorageOptions options = {})
        : storage_(std::move(baseRoot), std::move(targetPluginId), options) {}

    [[nodiscard]] PresetStorageKind kind() const noexcept override {
        return PresetStorageKind::Native;
    }

    [[nodiscard]] std::string_view targetPluginId() const noexcept override {
        return storage_.targetPluginId();
    }

    [[nodiscard]] std::optional<std::string> nativeFileRoot() const override {
        const auto ready = storage_.ensureReady();
        if (!ready.ok())
            return std::nullopt;
        return storage_.root().generic_string();
    }

    [[nodiscard]] PresetStorageListResult list() const override {
        const auto native = storage_.list();
        PresetStorageListResult result;
        result.status = portableStorageStatus(native.status);
        result.entries.reserve(native.entries.size());
        for (const auto &entry : native.entries)
            result.entries.push_back({entry.identity, entry.metadata});
        result.diagnostics.reserve(native.diagnostics.size());
        for (const auto &diagnostic : native.diagnostics)
            result.diagnostics.push_back(portableStorageStatus(diagnostic.status));
        return result;
    }

    [[nodiscard]] PresetStorageLoadResult load(std::string_view identity) const override {
        auto native = storage_.load(identity);
        return {portableStorageStatus(native.status), std::move(native.document)};
    }

    [[nodiscard]] PresetStorageSaveResult saveAs(
        std::string_view identity,
        const PresetDocument &document,
        bool overwrite = false) override {
        auto native = storage_.saveAs(identity, document, overwrite);
        return {portableStorageStatus(native.status), std::move(native.identity)};
    }

    [[nodiscard]] PresetStorageStatus remove(std::string_view identity) override {
        return portableStorageStatus(storage_.remove(identity));
    }

    // Native callers that need the complete #103 diagnostic object retain an
    // explicit escape hatch without weakening the portable interface.
    [[nodiscard]] NativePresetStorage &nativeStorage() noexcept { return storage_; }
    [[nodiscard]] const NativePresetStorage &nativeStorage() const noexcept { return storage_; }

private:
    NativePresetStorage storage_;
};

} // namespace webview_gui::examples::presets
