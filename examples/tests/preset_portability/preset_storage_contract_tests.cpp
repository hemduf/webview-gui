#include "preset_factory_catalog.h"
#include "preset_storage.h"

#include <cassert>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace presets = webview_gui::examples::presets;

namespace {

class FakeBrowserPresetStorage final : public presets::PresetUserStorage {
public:
    explicit FakeBrowserPresetStorage(std::string targetPluginId)
        : targetPluginId_(std::move(targetPluginId)) {}

    presets::PresetStorageKind kind() const noexcept override {
        return presets::PresetStorageKind::HostProvided;
    }

    std::string_view targetPluginId() const noexcept override {
        return targetPluginId_;
    }

    std::optional<std::string> nativeFileRoot() const override {
        return std::nullopt;
    }

    presets::PresetStorageListResult list() const override {
        presets::PresetStorageListResult result;
        for (const auto &[identity, bytes] : blobs_) {
            const auto metadata = presets::parsePresetMetadata(bytes, targetPluginId_);
            if (!metadata.ok()) {
                result.status.error = presets::PresetStorageError::ParseFailed;
                result.status.codecError = metadata.error;
                result.entries.clear();
                return result;
            }
            result.entries.push_back({identity, metadata.metadata});
        }
        return result;
    }

    presets::PresetStorageLoadResult load(std::string_view identity) const override {
        const auto found = blobs_.find(std::string{identity});
        if (found == blobs_.end())
            return {{presets::PresetStorageError::NotFound}, std::nullopt};

        const auto parsed = presets::parsePresetDocument(found->second, targetPluginId_);
        if (!parsed.ok())
            return {{presets::PresetStorageError::ParseFailed, parsed.error}, std::nullopt};
        return {{}, std::move(parsed.document)};
    }

    presets::PresetStorageSaveResult saveAs(std::string_view identity,
                                            const presets::PresetDocument &document,
                                            bool overwrite = false) override {
        if (document.metadata.targetPluginId != targetPluginId_)
            return {{presets::PresetStorageError::WrongTargetPlugin}, {}};
        if (identity.empty())
            return {{presets::PresetStorageError::InvalidIdentity}, {}};

        auto encoded = presets::serializePresetDocument(document);
        if (!encoded.ok())
            return {{presets::PresetStorageError::SerializeFailed, encoded.error}, {}};

        const std::string key{identity};
        const auto found = blobs_.find(key);
        if (found != blobs_.end() && !overwrite)
            return {{presets::PresetStorageError::AlreadyExists}, {}};

        blobs_[key] = std::move(encoded.bytes);
        return {{}, key};
    }

    presets::PresetStorageStatus remove(std::string_view identity) override {
        const auto erased = blobs_.erase(std::string{identity});
        if (erased == 0u)
            return {presets::PresetStorageError::NotFound};
        return {};
    }

private:
    std::string targetPluginId_;
    std::map<std::string, std::string> blobs_;
};

presets::PresetDocument makeGainDocument(double gainDb) {
    presets::PresetDocument document;
    document.metadata.targetPluginId = std::string{presets::kGainFactoryTargetPluginId};
    document.metadata.name = "Browser User";
    document.metadata.creator = "test";
    document.parameters = {
        {0x1000u, gainDb},
        {0x1001u, 0.0},
    };
    return document;
}

void verifyFactoryCatalogWithoutNativeStorage(
    const presets::FactoryPresetCatalog &catalog,
    std::string_view targetPluginId,
    std::size_t expectedSize) {
    assert(catalog.size() == expectedSize);
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        const auto *resource = catalog.at(i);
        assert(resource != nullptr);
        assert(resource->valid());
        const auto parsed = presets::parsePresetDocument(resource->bytes, targetPluginId);
        assert(parsed.ok());
        assert(parsed.document.has_value());
        const auto canonical = presets::serializePresetDocument(*parsed.document);
        assert(canonical.ok());
        assert(canonical.bytes == resource->bytes);
    }
}

} // namespace

int main() {
    // #104 contract: factory banks remain available without constructing any
    // native filesystem storage object.
    verifyFactoryCatalogWithoutNativeStorage(
        presets::gainFactoryPresetCatalog(),
        presets::kGainFactoryTargetPluginId,
        3u);
    verifyFactoryCatalogWithoutNativeStorage(
        presets::polySynthFactoryPresetCatalog(),
        presets::kPolySynthFactoryTargetPluginId,
        6u);

    // WCLAP with no browser/host persistence backend must advertise user
    // storage as unavailable and must never invent a native FILE location.
    presets::UnavailablePresetUserStorage unavailable{
        std::string{presets::kGainFactoryTargetPluginId}};
    assert(unavailable.kind() == presets::PresetStorageKind::Unavailable);
    assert(unavailable.targetPluginId() == presets::kGainFactoryTargetPluginId);
    assert(!unavailable.nativeFileRoot().has_value());
    assert(unavailable.list().status.error == presets::PresetStorageError::Unavailable);
    assert(unavailable.load("anything").status.error == presets::PresetStorageError::Unavailable);
    assert(unavailable.saveAs("anything", makeGainDocument(0.0)).status.error ==
           presets::PresetStorageError::Unavailable);
    assert(unavailable.remove("anything").error == presets::PresetStorageError::Unavailable);

    // A host/browser backend is injected through the same format-neutral
    // interface. It stores canonical #100 bytes and does not expose native paths.
    FakeBrowserPresetStorage browser{std::string{presets::kGainFactoryTargetPluginId}};
    presets::PresetUserStorage &storage = browser;
    assert(storage.kind() == presets::PresetStorageKind::HostProvided);
    assert(!storage.nativeFileRoot().has_value());

    auto document = makeGainDocument(-6.0);
    const auto save = storage.saveAs("browser-user", document);
    assert(save.ok());
    assert(save.identity == "browser-user");

    const auto duplicate = storage.saveAs("browser-user", document);
    assert(!duplicate.ok());
    assert(duplicate.status.error == presets::PresetStorageError::AlreadyExists);

    const auto listed = storage.list();
    assert(listed.ok());
    assert(listed.entries.size() == 1u);
    assert(listed.entries[0].identity == "browser-user");
    assert(listed.entries[0].metadata.name == document.metadata.name);

    const auto loaded = storage.load("browser-user");
    assert(loaded.ok());
    assert(loaded.document.has_value());
    assert(loaded.document->metadata.targetPluginId == presets::kGainFactoryTargetPluginId);
    assert(loaded.document->parameters.size() == 2u);
    assert(loaded.document->parameters[0].stableParameterId == 0x1000u);
    assert(loaded.document->parameters[0].baseValue == -6.0);

    document.parameters[0].baseValue = 6.0;
    const auto overwritten = storage.saveAs("browser-user", document, true);
    assert(overwritten.ok());
    const auto reloaded = storage.load("browser-user");
    assert(reloaded.ok());
    assert(reloaded.document->parameters[0].baseValue == 6.0);

    const auto removed = storage.remove("browser-user");
    assert(removed.ok());
    assert(storage.load("browser-user").status.error == presets::PresetStorageError::NotFound);

    return 0;
}
