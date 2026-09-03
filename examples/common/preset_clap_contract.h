#pragma once

#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>
#include <clap/plugin-factory.h>

#include <cstdint>
#include <string_view>

namespace webview_gui::examples::presets {

// #37 adapts two distinct CLAP ownership surfaces. Preset Discovery is a
// clap_entry factory, while preset-load is queried from a concrete plug-in
// instance. Keep this distinction explicit so entry/factory code cannot be
// accidentally routed through instance lifetime, or vice versa.
enum class ClapPresetSurface : std::uint8_t {
    None,
    EntryFactory,
    PluginExtension,
};

constexpr ClapPresetSurface classifyPresetClapId(std::string_view id) noexcept {
    if (id == std::string_view{CLAP_PRESET_DISCOVERY_FACTORY_ID} ||
        id == std::string_view{CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT})
        return ClapPresetSurface::EntryFactory;

    if (id == std::string_view{CLAP_EXT_PRESET_LOAD} ||
        id == std::string_view{CLAP_EXT_PRESET_LOAD_COMPAT})
        return ClapPresetSurface::PluginExtension;

    return ClapPresetSurface::None;
}

// Non-owning metadata receiver used by the CLAP adapter. #36 owns parsing,
// migration, storage and the canonical metadata representation; an adapter from
// #36 feeds this sink. All string_views are valid only for the duration of the
// callback that receives them unless the caller explicitly copies them.
class PresetMetadataSink {
public:
    virtual ~PresetMetadataSink() = default;

    // A catalog/container may report multiple presets. Returning false asks the
    // producer to stop immediately without treating receiver cancellation as a
    // malformed preset.
    virtual bool beginPreset(std::string_view name, std::string_view loadKey) noexcept = 0;
    virtual void setTargetPlugin(std::string_view pluginId) noexcept = 0;
    virtual void addCreator(std::string_view creator) noexcept = 0;
    virtual void setDescription(std::string_view description) noexcept = 0;
    virtual void addFeature(std::string_view feature) noexcept = 0;
    virtual void setTimestamps(clap_timestamp creation,
                               clap_timestamp modification) noexcept = 0;
};

// A preset loader never mutates live processor state directly. #36 parses and
// validates its document, then describes one complete persistent candidate to
// this sink. The plug-in-specific #37 adapter owns the final atomic/coherent
// commit after endCandidate() succeeds.
class PresetStateSink {
public:
    virtual ~PresetStateSink() = default;

    virtual bool beginCandidate(std::string_view targetPluginId) noexcept = 0;
    virtual bool setParameter(std::uint32_t stableParameterId, double value) noexcept = 0;
    virtual bool setSetting(std::string_view key, std::string_view value) noexcept = 0;
    virtual bool endCandidate() noexcept = 0;
};

// Narrow #36 -> #37 integration seam. It intentionally contains no filesystem,
// JSON/CBOR, WebView or audio-processor type. Production implementations are
// supplied by #36; #37 tests can provide deterministic fakes.
class PresetCatalog {
public:
    virtual ~PresetCatalog() = default;

    // Extension without the leading dot, e.g. "wvpreset".
    virtual std::string_view fileExtension() const noexcept = 0;

    // Returns true only for a real native filesystem-backed user preset root.
    // WCLAP/browser storage must return false and must not be advertised as a
    // CLAP_PRESET_DISCOVERY_LOCATION_FILE location.
    virtual bool nativeUserLocation(std::string_view &location) const noexcept = 0;

    // Bundled factory content is exposed as a PLUGIN container and may emit
    // multiple presets with stable load keys.
    virtual bool enumerateFactoryMetadata(PresetMetadataSink &sink) const noexcept = 0;

    // Native user FILE metadata path. Implementations return false for storage
    // backends which cannot expose a native file location.
    virtual bool metadataForFile(std::string_view path,
                                 PresetMetadataSink &sink) const noexcept = 0;

    // Resolve/parse one factory or native user preset into a persistent candidate
    // only. These methods must not commit processor state themselves.
    virtual bool loadFactory(std::string_view loadKey,
                             PresetStateSink &sink) const noexcept = 0;
    virtual bool loadFile(std::string_view path,
                          PresetStateSink &sink) const noexcept = 0;
};

} // namespace webview_gui::examples::presets
