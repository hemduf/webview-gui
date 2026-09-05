#include "../../common/preset_browser_controller.h"
#include "../../common/presets/preset_factory_catalog.h"

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace webview_gui::examples::presets;

namespace {

class FakeUserStorage final : public PresetUserStorage {
public:
    explicit FakeUserStorage(std::string targetPluginId,
                             PresetStorageKind kind = PresetStorageKind::HostProvided)
        : targetPluginId_(std::move(targetPluginId)), kind_(kind) {}

    PresetStorageKind kind() const noexcept override { return kind_; }
    std::string_view targetPluginId() const noexcept override { return targetPluginId_; }
    std::optional<std::string> nativeFileRoot() const override { return std::nullopt; }

    PresetStorageListResult list() const override {
        if (failList_) {
            PresetStorageStatus status;
            status.error = PresetStorageError::IoFailure;
            return {status, {}, {}};
        }
        if (kind_ == PresetStorageKind::Unavailable)
            return {unavailablePresetStorageStatus(), {}, {}};

        PresetStorageListResult result;
        for (const auto &stored : entries_)
            result.entries.push_back({stored.identity, stored.document.metadata});
        return result;
    }

    PresetStorageLoadResult load(std::string_view identity) const override {
        if (failLoad_) {
            PresetStorageStatus status;
            status.error = PresetStorageError::IoFailure;
            return {status, std::nullopt};
        }
        for (const auto &stored : entries_) {
            if (stored.identity == identity)
                return {{}, stored.document};
        }
        PresetStorageStatus status;
        status.error = PresetStorageError::NotFound;
        return {status, std::nullopt};
    }

    PresetStorageSaveResult saveAs(std::string_view identity,
                                   const PresetDocument &document,
                                   bool overwrite = false) override {
        if (kind_ == PresetStorageKind::Unavailable)
            return {unavailablePresetStorageStatus(), {}};
        if (failSave_) {
            PresetStorageStatus status;
            status.error = PresetStorageError::IoFailure;
            return {status, {}};
        }
        for (auto &stored : entries_) {
            if (stored.identity != identity)
                continue;
            if (!overwrite) {
                PresetStorageStatus status;
                status.error = PresetStorageError::AlreadyExists;
                return {status, {}};
            }
            stored.document = document;
            return {{}, std::string{identity}};
        }
        entries_.push_back({std::string{identity}, document});
        return {{}, std::string{identity}};
    }

    PresetStorageStatus remove(std::string_view identity) override {
        if (kind_ == PresetStorageKind::Unavailable)
            return unavailablePresetStorageStatus();
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

    void add(std::string identity, PresetDocument document) {
        entries_.push_back({std::move(identity), std::move(document)});
    }

    void failList(bool value) noexcept { failList_ = value; }
    void failLoad(bool value) noexcept { failLoad_ = value; }
    void failSave(bool value) noexcept { failSave_ = value; }

private:
    struct Stored {
        std::string identity;
        PresetDocument document;
    };

    std::string targetPluginId_;
    PresetStorageKind kind_ = PresetStorageKind::HostProvided;
    std::vector<Stored> entries_;
    bool failList_ = false;
    bool failLoad_ = false;
    bool failSave_ = false;
};

PresetDocument userDocument(const char *name, double gainDb) {
    auto document = detail::makeGainDocument(detail::kGainFactoryDefinitions[0]);
    document.metadata.name = name;
    document.metadata.factoryLoadKey.reset();
    document.metadata.tags = {"user", "gain"};
    document.parameters[0].value = gainDb;
    return document;
}

} // namespace

int main() {
    FakeUserStorage storage{std::string{kGainFactoryTargetPluginId}};
    storage.add("quiet", userDocument("Quiet", -12.0));
    storage.add("loud", userDocument("Loud", 9.0));

    PresetBrowserController controller{
        gainFactoryPresetCatalog(), std::string{kGainFactoryTargetPluginId}, &storage};

    // Refresh is transactional and merges canonical factory metadata with user
    // metadata without instantiating a processor or WebView.
    auto result = controller.refresh();
    assert(result.ok());
    assert(controller.model().factoryCount() == 3u);
    assert(controller.model().userCount() == 2u);
    assert(controller.userMutationsAvailable());

    const auto bass = controller.model().filtered("trim", "factory");
    assert(bass.size() == 1u);
    assert(bass[0]->identity == "gain:trim-minus-6db");

    // Resolution is side-effect-free: browser identity changes only after the
    // caller successfully applies the candidate to processor/base state.
    auto resolved = controller.resolve(PresetBrowserContentKind::Factory,
                                       "gain:trim-minus-6db");
    assert(resolved.ok());
    assert(resolved.document->parameters[0].value == -6.0);
    assert(controller.model().current().kind == PresetBrowserContentKind::None);
    assert(controller.markLoaded(PresetBrowserContentKind::Factory,
                                 "gain:trim-minus-6db"));

    storage.failLoad(true);
    resolved = controller.resolve(PresetBrowserContentKind::User, "quiet");
    assert(!resolved.ok());
    assert(controller.model().current().identity == "gain:trim-minus-6db");
    storage.failLoad(false);

    resolved = controller.resolve(PresetBrowserContentKind::User, "quiet");
    assert(resolved.ok());
    assert(resolved.document->metadata.name == "Quiet");
    assert(controller.markLoaded(PresetBrowserContentKind::User, "quiet"));

    // Failed writes preserve both storage and browser identity.
    storage.failSave(true);
    const auto beforeIdentity = controller.model().current().identity;
    result = controller.saveAs("new-user", userDocument("New User", -3.0), false);
    assert(!result.ok());
    assert(controller.model().current().identity == beforeIdentity);
    storage.failSave(false);

    result = controller.saveAs("new-user", userDocument("New User", -3.0), false);
    assert(result.ok());
    assert(controller.model().userCount() == 3u);
    assert(controller.model().current().kind == PresetBrowserContentKind::User);
    assert(controller.model().current().identity == "new-user");

    result = controller.remove("new-user");
    assert(result.ok());
    assert(controller.model().userCount() == 2u);
    assert(controller.model().current().kind == PresetBrowserContentKind::None);

    // A failed refresh leaves the previous complete snapshot intact.
    const auto countBeforeFailedRefresh = controller.model().entries().size();
    storage.failList(true);
    result = controller.refresh();
    assert(!result.ok());
    assert(controller.model().entries().size() == countBeforeFailedRefresh);

    // WCLAP/no-host-storage keeps factory browsing functional and exposes user
    // mutation unavailability explicitly instead of synthesizing a filesystem.
    UnavailablePresetUserStorage unavailable{std::string{kGainFactoryTargetPluginId}};
    PresetBrowserController wasiController{
        gainFactoryPresetCatalog(), std::string{kGainFactoryTargetPluginId}, &unavailable};
    result = wasiController.refresh();
    assert(result.ok());
    assert(wasiController.model().factoryCount() == 3u);
    assert(wasiController.model().userCount() == 0u);
    assert(!wasiController.userMutationsAvailable());
    result = wasiController.saveAs("cannot-save", userDocument("Nope", 0.0), false);
    assert(!result.ok());
    assert(result.error == PresetBrowserControllerError::StorageUnavailable);

    return 0;
}
