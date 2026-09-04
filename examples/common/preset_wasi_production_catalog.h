#pragma once

#include "preset_clap_contract.h"
#include "presets/preset_factory_catalog.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace webview_gui::examples::presets {

// WCLAP factory presets use the same #101 definitions and #102 candidate/apply
// boundary as native builds. Keep WASI filesystem-free and parser-free: the
// canonical PresetDocument is built from the shared definitions in-process.
class WasiProductionPresetCatalog final : public PresetCatalog {
public:
    WasiProductionPresetCatalog(const FactoryPresetCatalog &factoryCatalog,
                                std::string targetPluginId) noexcept
        : factoryCatalog_(factoryCatalog),
          targetPluginId_(std::move(targetPluginId)) {}

    [[nodiscard]] std::string_view fileExtension() const noexcept override {
        return factoryCatalog_.fileExtension();
    }

    bool nativeUserLocation(std::string_view &location) const noexcept override {
        location = {};
        return false;
    }

    PresetResult enumerateFactoryMetadata(PresetMetadataSink &sink) const noexcept override {
        try {
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

    PresetResult metadataForFile(std::string_view,
                                 PresetMetadataSink &) const noexcept override {
        return PresetResult::unsupported("WCLAP FILE preset discovery is unavailable");
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

            const auto &document = lookup.resource->document;
            if (!document.metadata.factoryLoadKey ||
                *document.metadata.factoryLoadKey != loadKey)
                return PresetResult::error("factory preset load key mismatch");
            return emitState(document, sink);
        } catch (...) {
            return PresetResult::error("factory preset loading failed");
        }
    }

    PresetResult loadFile(std::string_view,
                          PresetStateSink &) const noexcept override {
        return PresetResult::unsupported("WCLAP FILE preset loading is unavailable");
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
        if ((metadata.creationTimestamp && *metadata.creationTimestamp < 0) ||
            (metadata.modificationTimestamp && *metadata.modificationTimestamp < 0))
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
};

[[nodiscard]] inline std::unique_ptr<PresetCatalog> makeWasiProductionPresetCatalog(
    const FactoryPresetCatalog &factoryCatalog,
    std::string_view targetPluginId) noexcept {
    try {
        return std::make_unique<WasiProductionPresetCatalog>(
            factoryCatalog, std::string{targetPluginId});
    } catch (...) {
        return {};
    }
}

template <typename = void>
[[nodiscard]] inline std::unique_ptr<PresetCatalog> makeDefaultProductionPresetCatalog(
    const FactoryPresetCatalog &factoryCatalog,
    std::string_view targetPluginId) noexcept {
    return makeWasiProductionPresetCatalog(factoryCatalog, targetPluginId);
}

} // namespace webview_gui::examples::presets
