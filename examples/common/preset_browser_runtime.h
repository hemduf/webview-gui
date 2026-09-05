#pragma once

#include "preset_browser_controller.h"
#include "preset_browser_protocol.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace webview_gui::examples::presets {

enum class PresetBrowserRuntimeError : std::uint8_t {
    None,
    ProtocolFailure,
    ControllerFailure,
    ApplyFailed,
    CaptureFailed,
    InitFailed,
    SnapshotFailed,
    SendFailed,
    NoSelection,
};

struct PresetBrowserRuntimeResult {
    bool handled = false;
    PresetBrowserRuntimeError error = PresetBrowserRuntimeError::None;
    PresetBrowserProtocolError protocolError = PresetBrowserProtocolError::None;
    PresetBrowserControllerError controllerError = PresetBrowserControllerError::None;

    [[nodiscard]] bool ok() const noexcept {
        return handled && error == PresetBrowserRuntimeError::None;
    }
};

// Main/UI-thread-only dispatcher joining the bounded WVP2/WVB2 protocol to the
// storage/model controller. Processor/base-state mutation remains an injected
// callback so each plug-in can build and commit its complete candidate first.
// Browser identity is advanced only after that callback succeeds.
class PresetBrowserRuntime {
public:
    explicit PresetBrowserRuntime(PresetBrowserController &controller) noexcept
        : controller_(controller) {}

    [[nodiscard]] PresetBrowserController &controller() noexcept { return controller_; }
    [[nodiscard]] const PresetBrowserController &controller() const noexcept { return controller_; }

    template <typename ApplyDocument,
              typename CaptureDocument,
              typename ResetInit,
              typename SendSnapshot>
    [[nodiscard]] PresetBrowserRuntimeResult receive(const void *buffer,
                                                     std::size_t size,
                                                     ApplyDocument &&applyDocument,
                                                     CaptureDocument &&captureDocument,
                                                     ResetInit &&resetInit,
                                                     SendSnapshot &&sendSnapshot) {
        if (!looksLikePresetBrowserRequest(buffer, size))
            return {};

        const auto decoded = decodePresetBrowserRequest(buffer, size);
        if (!decoded.ok()) {
            PresetBrowserRuntimeResult result;
            result.handled = true;
            result.error = PresetBrowserRuntimeError::ProtocolFailure;
            result.protocolError = decoded.error;
            return result;
        }

        switch (decoded.request.command) {
            // Snapshot is intentionally storage-free so a WebView may poll dirty
            // state/current identity without repeatedly touching the filesystem.
            // Refresh is the explicit catalog/storage rescan command.
            case PresetBrowserCommand::Snapshot:
                return emitSnapshot(sendSnapshot);
            case PresetBrowserCommand::Refresh:
                return refreshAndSend(sendSnapshot);

            case PresetBrowserCommand::Load:
                return loadAndSend(decoded.request.kind,
                                   decoded.request.identity,
                                   applyDocument,
                                   sendSnapshot);

            case PresetBrowserCommand::Next: {
                const auto *entry = controller_.model().next();
                if (!entry)
                    return failure(PresetBrowserRuntimeError::NoSelection);
                const auto kind = entry->kind;
                const std::string identity = entry->identity;
                return loadAndSend(kind, identity, applyDocument, sendSnapshot);
            }

            case PresetBrowserCommand::Previous: {
                const auto *entry = controller_.model().previous();
                if (!entry)
                    return failure(PresetBrowserRuntimeError::NoSelection);
                const auto kind = entry->kind;
                const std::string identity = entry->identity;
                return loadAndSend(kind, identity, applyDocument, sendSnapshot);
            }

            case PresetBrowserCommand::Init:
                if (!resetInit())
                    return failure(PresetBrowserRuntimeError::InitFailed);
                controller_.markInitLoaded();
                return emitSnapshot(sendSnapshot);

            case PresetBrowserCommand::SaveAs: {
                auto document = captureDocument(decoded.request.name);
                if (!document)
                    return failure(PresetBrowserRuntimeError::CaptureFailed);

                // A user save is never allowed to retain a bundled factory load
                // key. Display name comes from the explicit Save As request;
                // target plug-in and durable state remain owned by the capture
                // callback and are revalidated by the controller/storage layer.
                document->metadata.name = decoded.request.name;
                document->metadata.factoryLoadKey.reset();
                const auto saved = controller_.saveAs(decoded.request.identity,
                                                      *document,
                                                      decoded.request.overwrite);
                if (!saved.ok())
                    return controllerFailure(saved.error);
                return emitSnapshot(sendSnapshot);
            }

            case PresetBrowserCommand::Delete: {
                const auto removed = controller_.remove(decoded.request.identity);
                if (!removed.ok())
                    return controllerFailure(removed.error);
                return emitSnapshot(sendSnapshot);
            }
        }

        return failure(PresetBrowserRuntimeError::ProtocolFailure,
                       PresetBrowserProtocolError::UnsupportedCommand);
    }

private:
    [[nodiscard]] static bool looksLikePresetBrowserRequest(const void *buffer,
                                                            std::size_t size) noexcept {
        if (!buffer || size < 4u)
            return false;
        const auto *bytes = static_cast<const std::uint8_t *>(buffer);
        // WVP1 is already the PolySynth parameter-edit protocol. Match the
        // complete versioned magic so the preset layer can coexist without
        // stealing existing parameter/gesture traffic.
        return bytes[0] == 'W' && bytes[1] == 'V' && bytes[2] == 'P' && bytes[3] == '2';
    }

    [[nodiscard]] static PresetBrowserRuntimeResult failure(
        PresetBrowserRuntimeError error,
        PresetBrowserProtocolError protocolError = PresetBrowserProtocolError::None) noexcept {
        PresetBrowserRuntimeResult result;
        result.handled = true;
        result.error = error;
        result.protocolError = protocolError;
        return result;
    }

    [[nodiscard]] static PresetBrowserRuntimeResult controllerFailure(
        PresetBrowserControllerError error) noexcept {
        auto result = failure(PresetBrowserRuntimeError::ControllerFailure);
        result.controllerError = error;
        return result;
    }

    template <typename SendSnapshot>
    [[nodiscard]] PresetBrowserRuntimeResult refreshAndSend(SendSnapshot &sendSnapshot) {
        const auto refreshed = controller_.refresh();
        if (!refreshed.ok())
            return controllerFailure(refreshed.error);
        return emitSnapshot(sendSnapshot);
    }

    template <typename ApplyDocument, typename SendSnapshot>
    [[nodiscard]] PresetBrowserRuntimeResult loadAndSend(PresetBrowserContentKind kind,
                                                         std::string_view identity,
                                                         ApplyDocument &applyDocument,
                                                         SendSnapshot &sendSnapshot) {
        const auto resolved = controller_.resolve(kind, identity);
        if (!resolved.ok())
            return controllerFailure(resolved.error);
        if (!applyDocument(*resolved.document))
            return failure(PresetBrowserRuntimeError::ApplyFailed);
        if (!controller_.markLoaded(kind, identity))
            return controllerFailure(PresetBrowserControllerError::InvalidBrowserSnapshot);
        return emitSnapshot(sendSnapshot);
    }

    template <typename SendSnapshot>
    [[nodiscard]] PresetBrowserRuntimeResult emitSnapshot(SendSnapshot &sendSnapshot) {
        const auto encoded = encodePresetBrowserSnapshot(controller_.model(),
                                                         controller_.userMutationsAvailable());
        if (!encoded.ok())
            return failure(PresetBrowserRuntimeError::SnapshotFailed, encoded.error);
        if (!sendSnapshot(encoded.bytes.data(), encoded.bytes.size()))
            return failure(PresetBrowserRuntimeError::SendFailed);

        PresetBrowserRuntimeResult result;
        result.handled = true;
        return result;
    }

    PresetBrowserController &controller_;
};

} // namespace webview_gui::examples::presets
