#pragma once

#include <clap/ext/preset-load.h>
#include <clap/factory/plugin-factory.h>
#include <clap/factory/preset-discovery.h>

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

enum class PresetResultStatus : std::uint8_t {
    Success,
    Cancelled,
    NotFound,
    Unsupported,
    Error,
};

// Non-owning result used at the #37 adaptation boundary. `message` only needs to
// remain valid for the duration of the call which consumes the result; #37 may
// forward it immediately to a host error callback. Cancellation is deliberately
// distinct from errors because Preset Discovery receiver cancellation is normal.
struct PresetResult {
    PresetResultStatus status = PresetResultStatus::Success;
    std::int32_t osError = 0;
    std::string_view message{};

    constexpr bool succeeded() const noexcept {
        return status == PresetResultStatus::Success;
    }

    static constexpr PresetResult success() noexcept { return {}; }
    static constexpr PresetResult cancelled() noexcept {
        return {PresetResultStatus::Cancelled, 0, {}};
    }
    static constexpr PresetResult notFound(std::string_view message = {}) noexcept {
        return {PresetResultStatus::NotFound, 0, message};
    }
    static constexpr PresetResult unsupported(std::string_view message = {}) noexcept {
        return {PresetResultStatus::Unsupported, 0, message};
    }
    static constexpr PresetResult error(std::string_view message,
                                        std::int32_t osError = 0) noexcept {
        return {PresetResultStatus::Error, osError, message};
    }
};

// Non-owning metadata receiver used by the CLAP adapter. #36 owns parsing,
// migration, storage and its canonical metadata representation. #37 owns an
// adapter from that format-neutral API into this CLAP-facing sink, so #36 does
// not need to depend on CLAP types. All string_views are callback-lifetime only
// unless the receiver explicitly copies them.
class PresetMetadataSink {
public:
    virtual ~PresetMetadataSink() = default;

    // A catalog/container may report multiple presets. Returning false asks the
    // producer to stop immediately; the catalog must report Cancelled rather
    // than converting receiver cancellation into an error.
    virtual bool beginPreset(std::string_view name, std::string_view loadKey) noexcept = 0;
    virtual void setTargetPlugin(std::string_view pluginId) noexcept = 0;
    virtual void addCreator(std::string_view creator) noexcept = 0;
    virtual void setDescription(std::string_view description) noexcept = 0;
    virtual void addFeature(std::string_view feature) noexcept = 0;
    virtual void setTimestamps(clap_timestamp creation,
                               clap_timestamp modification) noexcept = 0;
};

// A preset loader never mutates live processor state directly. The #37 adapter
// asks #36 to parse/validate its canonical document and describes one complete
// persistent candidate to this sink. The plug-in-specific #37 code owns the
// final atomic/coherent commit after endCandidate() succeeds.
class PresetStateSink {
public:
    virtual ~PresetStateSink() = default;

    virtual bool beginCandidate(std::string_view targetPluginId) noexcept = 0;
    virtual bool setParameter(std::uint32_t stableParameterId, double value) noexcept = 0;
    virtual bool endCandidate() noexcept = 0;
};

// Narrow CLAP-side port that #37 implements over the format-neutral #36 preset
// subsystem. It intentionally contains no filesystem implementation, JSON/CBOR,
// WebView or audio-processor type. #37 tests can provide deterministic fakes;
// production code will adapt the API delivered by #36 rather than reimplement it.
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
    virtual PresetResult enumerateFactoryMetadata(PresetMetadataSink &sink) const noexcept = 0;

    // Native user FILE metadata path. Implementations return Unsupported for
    // storage backends which cannot expose a native file location.
    virtual PresetResult metadataForFile(std::string_view path,
                                         PresetMetadataSink &sink) const noexcept = 0;

    // Resolve/parse one factory or native user preset into a persistent candidate
    // only. These methods must not commit processor state themselves.
    virtual PresetResult loadFactory(std::string_view loadKey,
                                     PresetStateSink &sink) const noexcept = 0;
    virtual PresetResult loadFile(std::string_view path,
                                  PresetStateSink &sink) const noexcept = 0;
};

} // namespace webview_gui::examples::presets
