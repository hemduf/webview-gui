#pragma once

#include "preset_browser_model.h"
#include "presets/preset_factory_catalog.h"
#include "presets/preset_storage.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webview_gui::examples::presets {

enum class PresetBrowserControllerError {
    None,
    InvalidConfiguration,
    StorageUnavailable,
    StorageFailure,
    NotFound,
    InvalidDocument,
    InvalidBrowserSnapshot,
};

struct PresetBrowserControllerResult {
    PresetBrowserControllerError error = PresetBrowserControllerError::None;
    PresetStorageStatus storageStatus{};

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetBrowserControllerError::None;
    }
};

struct PresetBrowserDocumentResult : PresetBrowserControllerResult {
    std::optional<PresetDocument> document;

    [[nodiscard]] bool ok() const noexcept {
        return PresetBrowserControllerResult::ok() && document.has_value();
    }
};

// Main/UI-thread orchestration for #38. The controller intentionally resolves a
// preset document without applying it to processor state. The plug-in commits
// the candidate transactionally first and calls markLoaded() only after that
// commit succeeds, so failed loads cannot corrupt browser identity.
class PresetBrowserController {
public:
    PresetBrowserController(const FactoryPresetCatalog &factoryCatalog,
                            std::string targetPluginId,
                            PresetUserStorage *userStorage = nullptr) noexcept
        : factoryCatalog_(factoryCatalog),
          targetPluginId_(std::move(targetPluginId)),
          userStorage_(userStorage) {}

    [[nodiscard]] const PresetBrowserModel &model() const noexcept { return model_; }
    [[nodiscard]] PresetBrowserModel &model() noexcept { return model_; }

    [[nodiscard]] bool userMutationsAvailable() const noexcept {
        return userStorage_ &&
               userStorage_->kind() != PresetStorageKind::Unavailable &&
               userStorage_->targetPluginId() == targetPluginId_;
    }

    [[nodiscard]] PresetBrowserControllerResult refresh() {
        if (targetPluginId_.empty())
            return {PresetBrowserControllerError::InvalidConfiguration, {}};

        std::vector<PresetBrowserEntry> factoryEntries;
        factoryEntries.reserve(factoryCatalog_.size());
        for (std::size_t i = 0u; i < factoryCatalog_.size(); ++i) {
            const auto *resource = factoryCatalog_.at(i);
            if (!resource || resource->metadata.targetPluginId != targetPluginId_ ||
                resource->loadKey.empty() || resource->metadata.name.empty())
                return {PresetBrowserControllerError::InvalidConfiguration, {}};

            PresetBrowserEntry entry;
            entry.kind = PresetBrowserContentKind::Factory;
            entry.identity = resource->loadKey;
            entry.name = resource->metadata.name;
            entry.tags = resource->metadata.tags;
            factoryEntries.push_back(std::move(entry));
        }

        std::vector<PresetBrowserEntry> userEntries;
        if (userStorage_) {
            if (userStorage_->targetPluginId() != targetPluginId_)
                return {PresetBrowserControllerError::InvalidConfiguration, {}};

            if (userStorage_->kind() != PresetStorageKind::Unavailable) {
                const auto listed = userStorage_->list();
                if (!listed.ok())
                    return {PresetBrowserControllerError::StorageFailure, listed.status};
                userEntries.reserve(listed.entries.size());
                for (const auto &stored : listed.entries) {
                    if (stored.identity.empty() || stored.metadata.name.empty() ||
                        stored.metadata.targetPluginId != targetPluginId_)
                        return {PresetBrowserControllerError::InvalidConfiguration, {}};
                    PresetBrowserEntry entry;
                    entry.kind = PresetBrowserContentKind::User;
                    entry.identity = stored.identity;
                    entry.name = stored.metadata.name;
                    entry.tags = stored.metadata.tags;
                    userEntries.push_back(std::move(entry));
                }
            }
        }

        if (!model_.replaceEntries(std::move(factoryEntries), std::move(userEntries)))
            return {PresetBrowserControllerError::InvalidBrowserSnapshot, {}};
        return {};
    }

    [[nodiscard]] PresetBrowserDocumentResult resolve(PresetBrowserContentKind kind,
                                                      std::string_view identity) const {
        if (identity.empty())
            return {{PresetBrowserControllerError::NotFound, {}}, std::nullopt};

        if (kind == PresetBrowserContentKind::Factory) {
            const auto found = factoryCatalog_.find(identity);
            if (!found.ok() || !found.resource)
                return {{PresetBrowserControllerError::NotFound, {}}, std::nullopt};
            if (!validForTarget(found.resource->document))
                return {{PresetBrowserControllerError::InvalidDocument, {}}, std::nullopt};
            return {{}, found.resource->document};
        }

        if (kind != PresetBrowserContentKind::User)
            return {{PresetBrowserControllerError::NotFound, {}}, std::nullopt};
        if (!userMutationsAvailable())
            return {{PresetBrowserControllerError::StorageUnavailable,
                     unavailablePresetStorageStatus()},
                    std::nullopt};

        auto loaded = userStorage_->load(identity);
        if (!loaded.ok())
            return {{loaded.status.error == PresetStorageError::Unavailable
                         ? PresetBrowserControllerError::StorageUnavailable
                         : PresetBrowserControllerError::StorageFailure,
                     loaded.status},
                    std::nullopt};
        if (!loaded.document || !validForTarget(*loaded.document))
            return {{PresetBrowserControllerError::InvalidDocument, loaded.status}, std::nullopt};
        return {{}, std::move(loaded.document)};
    }

    [[nodiscard]] bool markLoaded(PresetBrowserContentKind kind,
                                  std::string_view identity) {
        return model_.markLoaded(kind, identity);
    }

    void markInitLoaded(std::string_view displayName = "Init") {
        model_.markInitLoaded(displayName);
    }

    void markPersistentEdit() noexcept { model_.markPersistentEdit(); }
    void markTransientChange() const noexcept { model_.markTransientChange(); }
    void clearIdentityAfterStateRestore() noexcept { model_.clearIdentityAfterStateRestore(); }

    [[nodiscard]] PresetBrowserControllerResult saveAs(std::string_view identity,
                                                       const PresetDocument &document,
                                                       bool overwrite) {
        if (!userMutationsAvailable())
            return {PresetBrowserControllerError::StorageUnavailable,
                    unavailablePresetStorageStatus()};
        if (identity.empty() || !validForTarget(document))
            return {PresetBrowserControllerError::InvalidDocument, {}};

        auto saved = userStorage_->saveAs(identity, document, overwrite);
        if (!saved.ok())
            return {saved.status.error == PresetStorageError::Unavailable
                        ? PresetBrowserControllerError::StorageUnavailable
                        : PresetBrowserControllerError::StorageFailure,
                    saved.status};

        const auto refreshed = refresh();
        if (!refreshed.ok())
            return refreshed;
        if (!model_.markLoaded(PresetBrowserContentKind::User, saved.identity))
            return {PresetBrowserControllerError::InvalidBrowserSnapshot, {}};
        return {};
    }

    [[nodiscard]] PresetBrowserControllerResult remove(std::string_view identity) {
        if (!userMutationsAvailable())
            return {PresetBrowserControllerError::StorageUnavailable,
                    unavailablePresetStorageStatus()};
        if (identity.empty())
            return {PresetBrowserControllerError::NotFound, {}};

        const auto removed = userStorage_->remove(identity);
        if (!removed.ok())
            return {removed.error == PresetStorageError::Unavailable
                        ? PresetBrowserControllerError::StorageUnavailable
                        : PresetBrowserControllerError::StorageFailure,
                    removed};
        return refresh();
    }

private:
    [[nodiscard]] bool validForTarget(const PresetDocument &document) const noexcept {
        return document.metadata.targetPluginId == targetPluginId_ &&
               validatePresetDocument(document).ok();
    }

    const FactoryPresetCatalog &factoryCatalog_;
    std::string targetPluginId_;
    PresetUserStorage *userStorage_ = nullptr;
    PresetBrowserModel model_;
};

} // namespace webview_gui::examples::presets
