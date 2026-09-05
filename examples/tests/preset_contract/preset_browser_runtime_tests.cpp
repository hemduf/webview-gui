#include "../../common/preset_browser_runtime.h"
#include "../../common/presets/preset_factory_catalog.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace webview_gui::examples::presets;

namespace {

class FakeUserStorage final : public PresetUserStorage {
public:
    explicit FakeUserStorage(std::string targetPluginId)
        : targetPluginId_(std::move(targetPluginId)) {}

    PresetStorageKind kind() const noexcept override { return PresetStorageKind::HostProvided; }
    std::string_view targetPluginId() const noexcept override { return targetPluginId_; }
    std::optional<std::string> nativeFileRoot() const override { return std::nullopt; }

    PresetStorageListResult list() const override {
        PresetStorageListResult result;
        for (const auto &entry : entries_)
            result.entries.push_back({entry.identity, entry.document.metadata});
        return result;
    }

    PresetStorageLoadResult load(std::string_view identity) const override {
        for (const auto &entry : entries_) {
            if (entry.identity == identity)
                return {{}, entry.document};
        }
        PresetStorageStatus status;
        status.error = PresetStorageError::NotFound;
        return {status, std::nullopt};
    }

    PresetStorageSaveResult saveAs(std::string_view identity,
                                   const PresetDocument &document,
                                   bool overwrite) override {
        for (auto &entry : entries_) {
            if (entry.identity != identity)
                continue;
            if (!overwrite) {
                PresetStorageStatus status;
                status.error = PresetStorageError::AlreadyExists;
                return {status, {}};
            }
            entry.document = document;
            return {{}, std::string{identity}};
        }
        entries_.push_back({std::string{identity}, document});
        return {{}, std::string{identity}};
    }

    PresetStorageStatus remove(std::string_view identity) override {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->identity == identity) {
                entries_.erase(it);
                return {};
            }
        }
        PresetStorageStatus status;
        status.error = PresetStorageError::NotFound;
        return status;
    }

private:
    struct Entry {
        std::string identity;
        PresetDocument document;
    };

    std::string targetPluginId_;
    std::vector<Entry> entries_;
};

PresetDocument userDocument(std::string name, double gainDb) {
    auto document = detail::makeGainDocument(detail::kGainFactoryDefinitions[0]);
    document.metadata.name = std::move(name);
    document.metadata.factoryLoadKey.reset();
    document.metadata.tags = {"user", "gain"};
    document.parameters[0].value = gainDb;
    return document;
}

std::vector<std::uint8_t> request(PresetBrowserCommand command,
                                  PresetBrowserContentKind kind = PresetBrowserContentKind::None,
                                  std::string_view identity = {},
                                  std::string_view name = {},
                                  bool overwrite = false) {
    return encodePresetBrowserRequestForTest(command, kind, identity, name, overwrite);
}

} // namespace

int main() {
    FakeUserStorage storage{std::string{kGainFactoryTargetPluginId}};
    PresetBrowserController controller{
        gainFactoryPresetCatalog(), std::string{kGainFactoryTargetPluginId}, &storage};
    PresetBrowserRuntime runtime{controller};

    std::optional<PresetDocument> applied;
    bool applySucceeds = true;
    int initCount = 0;
    std::vector<std::vector<std::uint8_t>> snapshots;

    auto apply = [&](const PresetDocument &document) {
        if (!applySucceeds)
            return false;
        applied = document;
        return true;
    };
    auto capture = [&](std::string_view displayName) -> std::optional<PresetDocument> {
        return userDocument(std::string{displayName}, -2.0);
    };
    auto resetInit = [&]() {
        ++initCount;
        return true;
    };
    auto send = [&](const void *data, std::size_t size) {
        const auto *bytes = static_cast<const std::uint8_t *>(data);
        snapshots.emplace_back(bytes, bytes + size);
        return true;
    };

    // Non-preset messages pass through untouched so WVG/WVQ/WVM can continue
    // using the existing parameter and telemetry bridge.
    const std::uint8_t other[] = {'W', 'V', 'G', '1'};
    auto result = runtime.receive(other, sizeof(other), apply, capture, resetInit, send);
    assert(!result.handled);
    assert(!applied.has_value());
    assert(snapshots.empty());

    // Snapshot refreshes storage/catalog state and emits a bounded WVB2 frame.
    auto bytes = request(PresetBrowserCommand::Snapshot);
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(result.ok());
    assert(controller.model().factoryCount() == 3u);
    assert(!snapshots.empty());
    auto decoded = decodePresetBrowserSnapshotForTest(snapshots.back().data(), snapshots.back().size());
    assert(decoded.ok());
    assert(decoded.entries.size() == 3u);
    assert(decoded.userMutationsAvailable);

    // Successful editor load commits processor/base state before current-preset
    // identity is advanced.
    bytes = request(PresetBrowserCommand::Load,
                    PresetBrowserContentKind::Factory,
                    "gain:trim-minus-6db");
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(result.ok());
    assert(applied.has_value());
    assert(applied->parameters[0].value == -6.0);
    assert(controller.model().current().identity == "gain:trim-minus-6db");
    assert(!controller.model().current().dirty);

    // Failed transactional apply preserves the previously valid browser identity.
    const auto beforeFailedApply = controller.model().current().identity;
    applySucceeds = false;
    bytes = request(PresetBrowserCommand::Load,
                    PresetBrowserContentKind::Factory,
                    "gain:unity");
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(!result.ok());
    assert(result.error == PresetBrowserRuntimeError::ApplyFailed);
    assert(controller.model().current().identity == beforeFailedApply);
    applySucceeds = true;

    // Next/previous resolve through the same transactional path.
    bytes = request(PresetBrowserCommand::Next);
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(result.ok());
    assert(controller.model().current().identity != beforeFailedApply);
    bytes = request(PresetBrowserCommand::Previous);
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(result.ok());
    assert(controller.model().current().identity == beforeFailedApply);

    // Save As captures only the current durable/base state, strips factory
    // identity and refreshes user metadata after storage commit.
    bytes = request(PresetBrowserCommand::SaveAs,
                    PresetBrowserContentKind::User,
                    "my-gain.wvpreset",
                    "My Gain");
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(result.ok());
    assert(controller.model().current().kind == PresetBrowserContentKind::User);
    assert(controller.model().current().identity == "my-gain.wvpreset");
    assert(controller.model().current().name == "My Gain");
    assert(controller.model().userCount() == 1u);

    // Overwrite is explicit; a no-overwrite collision fails without changing
    // current identity or the processor state.
    bytes = request(PresetBrowserCommand::SaveAs,
                    PresetBrowserContentKind::User,
                    "my-gain.wvpreset",
                    "My Gain 2",
                    false);
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(!result.ok());
    assert(result.error == PresetBrowserRuntimeError::ControllerFailure);
    assert(controller.model().current().identity == "my-gain.wvpreset");

    bytes = request(PresetBrowserCommand::SaveAs,
                    PresetBrowserContentKind::User,
                    "my-gain.wvpreset",
                    "My Gain 2",
                    true);
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(result.ok());
    assert(controller.model().current().name == "My Gain 2");

    // Init and delete are UI/main-thread state transitions and always emit the
    // new complete snapshot after successful mutation.
    bytes = request(PresetBrowserCommand::Init);
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(result.ok());
    assert(initCount == 1);
    assert(controller.model().current().kind == PresetBrowserContentKind::Init);

    bytes = request(PresetBrowserCommand::Delete,
                    PresetBrowserContentKind::User,
                    "my-gain.wvpreset");
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(result.ok());
    assert(controller.model().userCount() == 0u);

    // Malformed WVP traffic is consumed and rejected rather than leaking into
    // the parameter bridge. No callback executes for the rejected command.
    const auto appliedBeforeInvalid = applied;
    const auto snapshotsBeforeInvalid = snapshots.size();
    bytes = request(PresetBrowserCommand::Snapshot);
    bytes[3] = '9';
    result = runtime.receive(bytes.data(), bytes.size(), apply, capture, resetInit, send);
    assert(result.handled);
    assert(!result.ok());
    assert(result.error == PresetBrowserRuntimeError::ProtocolFailure);
    assert(applied.has_value() == appliedBeforeInvalid.has_value());
    assert(snapshots.size() == snapshotsBeforeInvalid);

    return 0;
}
