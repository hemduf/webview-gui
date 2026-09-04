#pragma once

#include "preset_clap_contract.h"
#include "presets/preset_codec.h"
#include "presets/preset_factory_catalog.h"
#include "presets/preset_storage.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace webview_gui::examples::presets {

namespace production_catalog_detail {

[[nodiscard]] inline bool validTimestamp(const std::optional<std::int64_t> &value) noexcept {
    return !value || *value >= 0;
}

[[nodiscard]] inline PresetResult storageStatusResult(
    const PresetStorageStatus &status) noexcept {
    const auto osError = static_cast<std::int32_t>(status.systemErrorCode);
    switch (status.error) {
        case PresetStorageError::None:
            return PresetResult::success();
        case PresetStorageError::Unavailable:
            return PresetResult::unsupported("native user preset storage unavailable");
        case PresetStorageError::NotFound:
            return PresetResult::notFound("user preset metadata not found");
        case PresetStorageError::WrongTargetPlugin:
            return PresetResult::error("preset targets a different plug-in", osError);
        case PresetStorageError::ParseFailed:
            return PresetResult::error("preset metadata parse failed", osError);
        case PresetStorageError::InputTooLarge:
            return PresetResult::error("preset metadata input is too large", osError);
        case PresetStorageError::OutsideRoot:
            return PresetResult::error("preset path is outside the declared user root", osError);
        case PresetStorageError::AlreadyExists:
        case PresetStorageError::InvalidIdentity:
        case PresetStorageError::InvalidConfiguration:
        case PresetStorageError::SerializeFailed:
        case PresetStorageError::IoFailure:
            return PresetResult::error("preset storage metadata error", osError);
    }
    return PresetResult::error("unknown preset storage metadata error", osError);
}

[[nodiscard]] inline PresetResult storageLoadStatusResult(
    const PresetStorageStatus &status) noexcept {
    const auto osError = static_cast<std::int32_t>(status.systemErrorCode);
    switch (status.error) {
        case PresetStorageError::None:
            return PresetResult::success();
        case PresetStorageError::Unavailable:
            return PresetResult::unsupported("native user preset storage unavailable");
        case PresetStorageError::NotFound:
            return PresetResult::notFound("user preset file not found");
        case PresetStorageError::WrongTargetPlugin:
            return PresetResult::error("preset targets a different plug-in", osError);
        case PresetStorageError::ParseFailed:
            return PresetResult::error("preset file parse failed", osError);
        case PresetStorageError::InputTooLarge:
            return PresetResult::error("preset file is too large", osError);
        case PresetStorageError::OutsideRoot:
            return PresetResult::error("preset path is outside the declared user root", osError);
        case PresetStorageError::InvalidIdentity:
            return PresetResult::error("preset file identity is invalid", osError);
        case PresetStorageError::InvalidConfiguration:
            return PresetResult::error("preset storage configuration is invalid", osError);
        case PresetStorageError::IoFailure:
            return PresetResult::error("preset file I/O failed", osError);
        case PresetStorageError::AlreadyExists:
        case PresetStorageError::SerializeFailed:
            return PresetResult::error("preset storage load error", osError);
    }
    return PresetResult::error("unknown preset storage load error", osError);
}

[[nodiscard]] inline PresetResult codecLoadResult(PresetCodecError error) noexcept {
    switch (error) {
        case PresetCodecError::None:
            return PresetResult::success();
        case PresetCodecError::WrongTargetPlugin:
            return PresetResult::error("preset targets a different plug-in");
        case PresetCodecError::InputTooLarge:
            return PresetResult::error("preset input is too large");
        case PresetCodecError::UnsupportedSchemaVersion:
            return PresetResult::unsupported("preset schema version is unsupported");
        case PresetCodecError::MalformedInput:
        case PresetCodecError::TruncatedInput:
            return PresetResult::error("preset input is malformed or truncated");
        case PresetCodecError::InvalidParameter:
        case PresetCodecError::DuplicateParameterId:
        case PresetCodecError::NonFiniteParameterValue:
            return PresetResult::error("preset contains invalid parameter state");
        case PresetCodecError::InvalidSetting:
            return PresetResult::error("preset contains invalid persistent settings");
        case PresetCodecError::MigrationFailed:
            return PresetResult::error("preset migration failed");
        case PresetCodecError::InvalidDocument:
            return PresetResult::error("preset document is invalid");
    }
    return PresetResult::error("unknown preset codec error");
}

[[nodiscard]] inline std::string normalizePath(std::string_view path) {
    std::string result;
    result.reserve(path.size());
    for (const char c : path)
        result.push_back(c == '\\' ? '/' : c);
    while (result.size() > 1u && result.back() == '/')
        result.pop_back();
    return result;
}

[[nodiscard]] inline bool pathMatchesEntry(std::string_view root,
                                           std::string_view identity,
                                           std::string_view location) {
    if (root.empty() || identity.empty() || location.empty())
        return false;
    if (identity == "." || identity == ".." ||
        identity.find('/') != std::string_view::npos ||
        identity.find('\\') != std::string_view::npos)
        return false;

    auto normalizedRoot = normalizePath(root);
    auto normalizedLocation = normalizePath(location);
    std::string expected = normalizedRoot;
    expected.push_back('/');
    expected.append(identity.data(), identity.size());
    return normalizedLocation == expected;
}

[[nodiscard]] inline std::optional<std::string> directChildIdentity(
    std::string_view root,
    std::string_view location) {
    if (root.empty() || location.empty())
        return std::nullopt;

    auto normalizedRoot = normalizePath(root);
    const auto normalizedLocation = normalizePath(location);
    if (normalizedRoot.empty() || normalizedLocation.empty())
        return std::nullopt;

    std::string prefix = normalizedRoot;
    if (prefix.back() != '/')
        prefix.push_back('/');
    if (normalizedLocation.size() <= prefix.size() ||
        normalizedLocation.compare(0u, prefix.size(), prefix) != 0)
        return std::nullopt;

    auto identity = normalizedLocation.substr(prefix.size());
    if (identity.empty() || identity == "." || identity == ".." ||
        identity.find('/') != std::string::npos ||
        identity.find('\\') != std::string::npos ||
        identity.size() <= kPresetFileSuffix.size() ||
        identity.compare(identity.size() - kPresetFileSuffix.size(),
                         kPresetFileSuffix.size(),
                         kPresetFileSuffix) != 0)
        return std::nullopt;
    return identity;
}

[[nodiscard]] inline bool diagnosticMatchesPath(const PresetStorageStatus &diagnostic,
                                                std::string_view location) {
    if (diagnostic.diagnosticPath.empty())
        return false;
    return normalizePath(diagnostic.diagnosticPath) == normalizePath(location);
}

} // namespace production_catalog_detail

class ProductionPresetCatalog final : public PresetCatalog {
public:
    ProductionPresetCatalog(const FactoryPresetCatalog &factoryCatalog,
                            std::string targetPluginId,
                            std::unique_ptr<PresetUserStorage> userStorage) noexcept
        : factoryCatalog_(factoryCatalog),
          targetPluginId_(std::move(targetPluginId)),
          userStorage_(std::move(userStorage)) {}

    [[nodiscard]] std::string_view fileExtension() const noexcept override {
        return factoryCatalog_.fileExtension();
    }

    bool nativeUserLocation(std::string_view &location) const noexcept override {
        location = {};
        if (!userStorage_)
            return false;
        try {
            const auto root = userStorage_->nativeFileRoot();
            if (!root || root->empty()) {
                nativeRootCache_.clear();
                return false;
            }
            nativeRootCache_ = *root;
            location = nativeRootCache_;
            return true;
        } catch (...) {
            nativeRootCache_.clear();
            return false;
        }
    }

    PresetResult enumerateFactoryMetadata(PresetMetadataSink &sink) const noexcept override {
        try {
            // Validate the whole container before the first receiver callback so
            // a corrupt later resource cannot leak a partial factory listing.
            for (std::size_t i = 0u; i < factoryCatalog_.size(); ++i) {
                const auto *resource = factoryCatalog_.at(i);
                if (!resource || !resource->valid())
                    return PresetResult::error("invalid bundled factory preset resource");
                const auto validation = validateMetadata(resource->metadata, true);
                if (!validation.succeeded())
                    return validation;
                if (resource->loadKey != *resource->metadata.factoryLoadKey)
                    return PresetResult::error("factory preset load key mismatch");
            }

            for (std::size_t i = 0u; i < factoryCatalog_.size(); ++i) {
                const auto *resource = factoryCatalog_.at(i);
                const auto result = emitMetadata(resource->metadata,
                                                 resource->loadKey,
                                                 CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
                                                 sink);
                if (!result.succeeded())
                    return result;
            }
            return PresetResult::success();
        } catch (...) {
            return PresetResult::error("factory preset metadata extraction failed");
        }
    }

    PresetResult metadataForFile(std::string_view path,
                                 PresetMetadataSink &sink) const noexcept override {
        if (!userStorage_)
            return PresetResult::unsupported("native user preset storage unavailable");

        try {
            std::string_view rootView;
            if (!nativeUserLocation(rootView))
                return PresetResult::unsupported("native user preset storage unavailable");
            const std::string root{rootView};

            if (!userSnapshot_) {
                auto listing = userStorage_->list();
                if (!listing.ok())
                    return production_catalog_detail::storageStatusResult(listing.status);
                userSnapshot_ = std::move(listing);
            }

            for (const auto &entry : userSnapshot_->entries) {
                if (!production_catalog_detail::pathMatchesEntry(root, entry.identity, path))
                    continue;
                const auto validation = validateMetadata(entry.metadata, false);
                if (!validation.succeeded())
                    return validation;
                return emitMetadata(entry.metadata,
                                    {},
                                    CLAP_PRESET_DISCOVERY_IS_USER_CONTENT,
                                    sink);
            }

            for (const auto &diagnostic : userSnapshot_->diagnostics) {
                if (production_catalog_detail::diagnosticMatchesPath(diagnostic, path))
                    return production_catalog_detail::storageStatusResult(diagnostic);
            }

            return PresetResult::notFound("user preset metadata not found");
        } catch (...) {
            return PresetResult::error("user preset metadata extraction failed");
        }
    }

    PresetResult loadFactory(std::string_view loadKey,
                             PresetStateSink &sink) const noexcept override {
        if (loadKey.empty())
            return PresetResult::error("factory preset load key is empty");
        try {
            const auto lookup = factoryCatalog_.find(loadKey);
            if (!lookup.ok()) {
                if (lookup.error == FactoryPresetCatalogError::NotFound)
                    return PresetResult::notFound("factory preset load key was not found");
                return PresetResult::error("factory preset resource is invalid");
            }

            const auto parsed = parsePresetDocument(lookup.resource->bytes, targetPluginId_);
            if (!parsed.ok())
                return production_catalog_detail::codecLoadResult(parsed.error);
            if (!parsed.document->metadata.factoryLoadKey ||
                *parsed.document->metadata.factoryLoadKey != loadKey)
                return PresetResult::error("factory preset load key mismatch");
            return emitState(*parsed.document, sink);
        } catch (...) {
            return PresetResult::error("factory preset loading failed");
        }
    }

    PresetResult loadFile(std::string_view path,
                          PresetStateSink &sink) const noexcept override {
        if (!userStorage_)
            return PresetResult::unsupported("native user preset storage unavailable");
        try {
            std::string_view rootView;
            if (!nativeUserLocation(rootView))
                return PresetResult::unsupported("native user preset storage unavailable");

            const auto identity = production_catalog_detail::directChildIdentity(rootView, path);
            if (!identity)
                return PresetResult::error("preset path is outside the declared user root");

            auto loaded = userStorage_->load(*identity);
            if (!loaded.ok())
                return production_catalog_detail::storageLoadStatusResult(loaded.status);
            if (!loaded.document)
                return PresetResult::error("preset storage returned no document");
            if (loaded.document->metadata.targetPluginId != targetPluginId_)
                return PresetResult::error("preset targets a different plug-in");
            return emitState(*loaded.document, sink);
        } catch (...) {
            return PresetResult::error("user preset loading failed");
        }
    }

private:
    [[nodiscard]] PresetResult validateMetadata(const PresetMetadata &metadata,
                                                bool requireLoadKey) const noexcept {
        if (metadata.targetPluginId != targetPluginId_)
            return PresetResult::error("preset targets a different plug-in");
        if (metadata.name.empty())
            return PresetResult::error("preset metadata is missing a name");
        if (requireLoadKey &&
            (!metadata.factoryLoadKey || metadata.factoryLoadKey->empty()))
            return PresetResult::error("factory preset metadata is missing a load key");
        if (!production_catalog_detail::validTimestamp(metadata.creationTimestamp) ||
            !production_catalog_detail::validTimestamp(metadata.modificationTimestamp))
            return PresetResult::error("preset metadata contains an invalid timestamp");
        return PresetResult::success();
    }

    [[nodiscard]] PresetResult emitMetadata(const PresetMetadata &metadata,
                                            std::string_view loadKey,
                                            std::uint32_t flags,
                                            PresetMetadataSink &sink) const noexcept {
        if (!sink.beginPreset(metadata.name, loadKey))
            return PresetResult::cancelled();

        sink.setTargetPlugin(metadata.targetPluginId);
        sink.setFlags(flags);
        if (!metadata.creator.empty())
            sink.addCreator(metadata.creator);
        if (!metadata.description.empty())
            sink.setDescription(metadata.description);
        for (const auto &tag : metadata.tags)
            sink.addFeature(tag);
        for (const auto &feature : metadata.features)
            sink.addFeature(feature);

        if (metadata.creationTimestamp || metadata.modificationTimestamp) {
            const auto creation = metadata.creationTimestamp
                                      ? static_cast<clap_timestamp>(*metadata.creationTimestamp)
                                      : CLAP_TIMESTAMP_UNKNOWN;
            const auto modification = metadata.modificationTimestamp
                                          ? static_cast<clap_timestamp>(*metadata.modificationTimestamp)
                                          : CLAP_TIMESTAMP_UNKNOWN;
            sink.setTimestamps(creation, modification);
        }
        return PresetResult::success();
    }

    [[nodiscard]] PresetResult emitState(const PresetDocument &document,
                                         PresetStateSink &sink) const noexcept {
        const auto validation = validatePresetDocument(document);
        if (!validation.ok())
            return PresetResult::error("preset document failed validation");
        if (document.metadata.targetPluginId != targetPluginId_)
            return PresetResult::error("preset targets a different plug-in");
        // The current Gain/PolySynth persistent state surface consists only of
        // published base parameters. Do not silently discard future typed settings.
        if (!document.settings.empty())
            return PresetResult::unsupported("preset contains unsupported persistent settings");

        if (!sink.beginCandidate(document.metadata.targetPluginId))
            return PresetResult::error("preset state candidate could not start");
        for (const auto &parameter : document.parameters) {
            if (!sink.setParameter(parameter.stableParameterId, parameter.value))
                return PresetResult::error("preset state candidate rejected a parameter");
        }
        if (!sink.endCandidate())
            return PresetResult::error("preset state candidate could not complete");
        return PresetResult::success();
    }

    const FactoryPresetCatalog &factoryCatalog_;
    std::string targetPluginId_;
    std::unique_ptr<PresetUserStorage> userStorage_;
    mutable std::string nativeRootCache_;
    mutable std::optional<PresetStorageListResult> userSnapshot_;
};

[[nodiscard]] inline std::unique_ptr<PresetCatalog> makeProductionPresetCatalog(
    const FactoryPresetCatalog &factoryCatalog,
    std::string_view targetPluginId,
    std::unique_ptr<PresetUserStorage> userStorage) noexcept {
    try {
        return std::make_unique<ProductionPresetCatalog>(factoryCatalog,
                                                         std::string{targetPluginId},
                                                         std::move(userStorage));
    } catch (...) {
        return {};
    }
}

// Native implementation is isolated in preset_production_catalog.cpp so the
// generic CLAP metadata adapter and future WCLAP provider do not import native
// filesystem APIs merely by including this header.
[[nodiscard]] std::unique_ptr<PresetCatalog> makeNativeProductionPresetCatalog(
    const FactoryPresetCatalog &factoryCatalog,
    std::string_view targetPluginId) noexcept;

template <typename = void>
[[nodiscard]] inline std::unique_ptr<PresetCatalog> makeDefaultProductionPresetCatalog(
    const FactoryPresetCatalog &factoryCatalog,
    std::string_view targetPluginId) noexcept {
#if defined(__wasi__)
    try {
        return makeProductionPresetCatalog(
            factoryCatalog,
            targetPluginId,
            std::make_unique<UnavailablePresetUserStorage>(std::string{targetPluginId}));
    } catch (...) {
        return {};
    }
#else
    return makeNativeProductionPresetCatalog(factoryCatalog, targetPluginId);
#endif
}

} // namespace webview_gui::examples::presets
