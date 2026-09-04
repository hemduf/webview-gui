#pragma once

#if !defined(__wasi__)
#error "preset_wasi_production_catalog.h is a WCLAP/WASI-only adapter"
#endif

// #92 intentionally leaves makeDefaultProductionPresetCatalog() unavailable on
// WASI. Rename that stub while including the shared contract, then provide the
// real #94 factory-only implementation below. The normal native implementation
// remains untouched.
#define makeDefaultProductionPresetCatalog makeDeferredWasiProductionPresetCatalog
#include "preset_production_catalog.h"
#undef makeDefaultProductionPresetCatalog

#include "example_plugin_ids.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>

namespace webview_gui::examples::presets {

namespace wasi_factory_detail {

struct GainDefinition {
    std::string_view loadKey;
    std::string_view name;
    std::string_view description;
    double gainDb = 0.0;
};

struct PolyDefinition {
    std::string_view loadKey;
    std::string_view name;
    std::string_view description;
    std::string_view category;
    std::array<double, 13> values{};
};

inline constexpr std::array<GainDefinition, 3> kGainDefinitions{{
    {"gain:unity", "Unity", "Neutral unity-gain starting point.", 0.0},
    {"gain:trim-minus-6db", "-6 dB Trim", "Clean six-decibel attenuation for gain staging.", -6.0},
    {"gain:boost-plus-6db", "+6 dB Boost", "Clean six-decibel boost for level matching.", 6.0},
}};

inline constexpr std::array<PolyDefinition, 6> kPolyDefinitions{{
    {"polysynth:init", "Init", "Neutral PolySynth starting point with the published defaults.", "init",
     {{0.0, 0.0, 0.0, 0.0, 6000.0, 0.0, 0.01, 0.1, 0.8, 0.25, 0.0, 0.0, 1.0}}},
    {"polysynth:bass", "Bass", "Compact square-wave bass with a short filter contour.", "bass",
     {{-3.0, 2.0, -12.0, 0.0, 220.0, 0.25, 0.005, 0.15, 0.7, 0.2, 0.55, 0.0, 1.0}}},
    {"polysynth:lead", "Lead", "Focused saw lead with a responsive envelope.", "lead",
     {{-3.0, 1.0, 0.0, 0.0, 2500.0, 0.2, 0.01, 0.08, 0.75, 0.15, 0.3, 0.0, 1.0}}},
    {"polysynth:pad", "Pad", "Slow saw pad with a soft filter and long release.", "pad",
     {{-6.0, 1.0, 0.0, 0.0, 3500.0, 0.15, 1.5, 1.0, 0.8, 2.5, 0.2, 0.0, 0.85}}},
    {"polysynth:pluck", "Pluck", "Fast bright pluck demonstrating filter-envelope movement.", "pluck",
     {{-4.0, 1.0, 0.0, 0.0, 1800.0, 0.2, 0.001, 0.25, 0.05, 0.3, 0.7, 0.0, 1.0}}},
    {"polysynth:poly-expression-demo", "Poly Expression Demo", "Balanced base patch intended for per-note expressive control.", "expression",
     {{-6.0, 1.0, 0.0, 0.0, 5000.0, 0.2, 0.01, 0.15, 0.8, 0.5, 0.35, 0.0, 0.8}}},
}};

enum class CatalogKind : std::uint8_t { Gain, PolySynth };

inline bool emitCommonMetadata(PresetMetadataSink &sink,
                               std::string_view targetPluginId,
                               std::string_view loadKey,
                               std::string_view name,
                               std::string_view description,
                               std::string_view category,
                               CatalogKind kind) noexcept {
    if (!sink.beginPreset(name, loadKey))
        return false;
    sink.setTargetPlugin(targetPluginId);
    sink.setFlags(CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    sink.addCreator("webview-gui");
    sink.setDescription(description);
    sink.addFeature("factory");
    sink.addFeature(category);
    if (kind == CatalogKind::Gain) {
        sink.addFeature("audio-effect");
        sink.addFeature("utility");
    } else {
        sink.addFeature("instrument");
        sink.addFeature("synthesizer");
    }
    return true;
}

} // namespace wasi_factory_detail

class WasiFactoryPresetCatalog final : public PresetCatalog {
public:
    explicit constexpr WasiFactoryPresetCatalog(wasi_factory_detail::CatalogKind kind) noexcept
        : kind_(kind) {}

    [[nodiscard]] std::string_view fileExtension() const noexcept override {
        return "wvpreset";
    }

    bool nativeUserLocation(std::string_view &location) const noexcept override {
        location = {};
        return false;
    }

    PresetResult enumerateFactoryMetadata(PresetMetadataSink &sink) const noexcept override {
        if (kind_ == wasi_factory_detail::CatalogKind::Gain) {
            for (const auto &definition : wasi_factory_detail::kGainDefinitions) {
                if (!wasi_factory_detail::emitCommonMetadata(
                        sink, plugin_ids::kGainPluginId, definition.loadKey,
                        definition.name, definition.description, "gain", kind_))
                    return PresetResult::cancelled();
            }
            return PresetResult::success();
        }

        for (const auto &definition : wasi_factory_detail::kPolyDefinitions) {
            if (!wasi_factory_detail::emitCommonMetadata(
                    sink, plugin_ids::kPolySynthPluginId, definition.loadKey,
                    definition.name, definition.description, definition.category, kind_))
                return PresetResult::cancelled();
        }
        return PresetResult::success();
    }

    PresetResult metadataForFile(std::string_view, PresetMetadataSink &) const noexcept override {
        return PresetResult::unsupported("WCLAP does not advertise native FILE preset discovery");
    }

    PresetResult loadFactory(std::string_view loadKey, PresetStateSink &sink) const noexcept override {
        if (loadKey.empty())
            return PresetResult::error("factory preset load key is empty");

        if (kind_ == wasi_factory_detail::CatalogKind::Gain) {
            for (const auto &definition : wasi_factory_detail::kGainDefinitions) {
                if (definition.loadKey != loadKey)
                    continue;
                if (!sink.beginCandidate(plugin_ids::kGainPluginId) ||
                    !sink.setParameter(0x1000u, definition.gainDb) ||
                    !sink.setParameter(0x1001u, 0.0) ||
                    !sink.endCandidate())
                    return PresetResult::error("Gain factory preset candidate was rejected");
                return PresetResult::success();
            }
            return PresetResult::notFound("Gain factory preset load key was not found");
        }

        for (const auto &definition : wasi_factory_detail::kPolyDefinitions) {
            if (definition.loadKey != loadKey)
                continue;
            if (!sink.beginCandidate(plugin_ids::kPolySynthPluginId))
                return PresetResult::error("PolySynth factory preset candidate could not start");
            for (std::size_t index = 0; index < definition.values.size(); ++index) {
                if (!sink.setParameter(static_cast<std::uint32_t>(1000u + index),
                                       definition.values[index]))
                    return PresetResult::error("PolySynth factory preset parameter was rejected");
            }
            if (!sink.endCandidate())
                return PresetResult::error("PolySynth factory preset candidate could not complete");
            return PresetResult::success();
        }
        return PresetResult::notFound("PolySynth factory preset load key was not found");
    }

    PresetResult loadFile(std::string_view, PresetStateSink &) const noexcept override {
        return PresetResult::unsupported("WCLAP native FILE preset loading is unavailable");
    }

private:
    wasi_factory_detail::CatalogKind kind_;
};

[[nodiscard]] inline std::unique_ptr<PresetCatalog> makeDefaultProductionPresetCatalog(
    const FactoryPresetCatalog &,
    std::string_view targetPluginId) noexcept {
    wasi_factory_detail::CatalogKind kind{};
    if (targetPluginId == plugin_ids::kGainPluginId)
        kind = wasi_factory_detail::CatalogKind::Gain;
    else if (targetPluginId == plugin_ids::kPolySynthPluginId)
        kind = wasi_factory_detail::CatalogKind::PolySynth;
    else
        return {};

    return std::unique_ptr<PresetCatalog>{new (std::nothrow) WasiFactoryPresetCatalog{kind}};
}

} // namespace webview_gui::examples::presets
