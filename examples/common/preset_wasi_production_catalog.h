#pragma once

#if !defined(__wasi__)
#error "preset_wasi_production_catalog.h is a WCLAP/WASI-only adapter"
#endif

// Include the shared PresetCatalog contract while hiding the #92 deferred WASI
// factory function. #94 provides the real factory-only implementation below.
#define makeDefaultProductionPresetCatalog makeDeferredWasiProductionPresetCatalog
#include "preset_production_catalog.h"
#undef makeDefaultProductionPresetCatalog

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>

namespace webview_gui::examples::presets {

namespace wasi_factory_detail {

inline bool emitMetadata(PresetMetadataSink &sink,
                         const FactoryPresetDefinitionView &definition) noexcept {
    if (!definition.valid() || !sink.beginPreset(definition.name, definition.loadKey))
        return false;
    sink.setTargetPlugin(definition.targetPluginId);
    sink.setFlags(CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    sink.addCreator("webview-gui");
    sink.setDescription(definition.description);
    sink.addFeature("factory");
    sink.addFeature(definition.category);
    if (definition.targetPluginId == kGainFactoryTargetPluginId) {
        sink.addFeature("audio-effect");
        sink.addFeature("utility");
    } else {
        sink.addFeature("instrument");
        sink.addFeature("synthesizer");
    }
    return true;
}

inline PresetResult emitState(PresetStateSink &sink,
                              const FactoryPresetDefinitionView &definition) noexcept {
    if (!definition.valid())
        return PresetResult::error("factory preset definition is invalid");
    if (!sink.beginCandidate(definition.targetPluginId))
        return PresetResult::error("factory preset candidate could not start");
    for (std::size_t index = 0u; index < definition.valueCount; ++index) {
        if (!sink.setParameter(definition.firstStableParameterId +
                                   static_cast<std::uint32_t>(index),
                               definition.values[index]))
            return PresetResult::error("factory preset parameter was rejected");
    }
    if (!sink.endCandidate())
        return PresetResult::error("factory preset candidate could not complete");
    return PresetResult::success();
}

} // namespace wasi_factory_detail

class WasiFactoryPresetCatalog final : public PresetCatalog {
public:
    explicit constexpr WasiFactoryPresetCatalog(const FactoryPresetCatalog &factoryCatalog) noexcept
        : factoryCatalog_(factoryCatalog) {}

    [[nodiscard]] std::string_view fileExtension() const noexcept override {
        return factoryCatalog_.fileExtension();
    }

    bool nativeUserLocation(std::string_view &location) const noexcept override {
        location = {};
        return false;
    }

    PresetResult enumerateFactoryMetadata(PresetMetadataSink &sink) const noexcept override {
        for (std::size_t index = 0u; index < factoryCatalog_.size(); ++index) {
            const auto definition = factoryCatalog_.at(index);
            if (!definition.valid())
                return PresetResult::error("factory preset definition is invalid");
            if (!wasi_factory_detail::emitMetadata(sink, definition))
                return PresetResult::cancelled();
        }
        return PresetResult::success();
    }

    PresetResult metadataForFile(std::string_view,
                                 PresetMetadataSink &) const noexcept override {
        return PresetResult::unsupported(
            "WCLAP does not advertise native FILE preset discovery");
    }

    PresetResult loadFactory(std::string_view loadKey,
                             PresetStateSink &sink) const noexcept override {
        if (loadKey.empty())
            return PresetResult::error("factory preset load key is empty");
        const auto definition = factoryCatalog_.find(loadKey);
        if (!definition.valid())
            return PresetResult::notFound("factory preset load key was not found");
        if (definition.targetPluginId != factoryCatalog_.targetPluginId())
            return PresetResult::error("factory preset targets a different plug-in");
        return wasi_factory_detail::emitState(sink, definition);
    }

    PresetResult loadFile(std::string_view,
                          PresetStateSink &) const noexcept override {
        return PresetResult::unsupported(
            "WCLAP native FILE preset loading is unavailable");
    }

private:
    const FactoryPresetCatalog &factoryCatalog_;
};

[[nodiscard]] inline std::unique_ptr<PresetCatalog> makeDefaultProductionPresetCatalog(
    const FactoryPresetCatalog &factoryCatalog,
    std::string_view targetPluginId) noexcept {
    if (targetPluginId.empty() || targetPluginId != factoryCatalog.targetPluginId())
        return {};
    return std::unique_ptr<PresetCatalog>{
        new (std::nothrow) WasiFactoryPresetCatalog{factoryCatalog}};
}

} // namespace webview_gui::examples::presets
