#pragma once

#include <clap/ext/preset-load.h>
#include <clap/factory/plugin-factory.h>
#include <clap/factory/preset-discovery.h>

#include <cstdint>
#include <string_view>

namespace webview_gui::examples::presets {

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

class PresetMetadataSink {
public:
    virtual ~PresetMetadataSink() = default;

    virtual bool beginPreset(std::string_view name, std::string_view loadKey) noexcept = 0;
    virtual void setTargetPlugin(std::string_view pluginId) noexcept = 0;
    virtual void setFlags(std::uint32_t flags) noexcept = 0;
    virtual void addCreator(std::string_view creator) noexcept = 0;
    virtual void setDescription(std::string_view description) noexcept = 0;
    virtual void addFeature(std::string_view feature) noexcept = 0;
    virtual void setTimestamps(clap_timestamp creation,
                               clap_timestamp modification) noexcept = 0;
};

class PresetStateSink {
public:
    virtual ~PresetStateSink() = default;

    virtual bool beginCandidate(std::string_view targetPluginId) noexcept = 0;
    virtual bool setParameter(std::uint32_t stableParameterId, double value) noexcept = 0;
    virtual bool endCandidate() noexcept = 0;
};

class PresetCatalog {
public:
    virtual ~PresetCatalog() = default;

    virtual std::string_view fileExtension() const noexcept = 0;
    virtual bool nativeUserLocation(std::string_view &location) const noexcept = 0;
    virtual PresetResult enumerateFactoryMetadata(PresetMetadataSink &sink) const noexcept = 0;
    virtual PresetResult metadataForFile(std::string_view path,
                                         PresetMetadataSink &sink) const noexcept = 0;
    virtual PresetResult loadFactory(std::string_view loadKey,
                                     PresetStateSink &sink) const noexcept = 0;
    virtual PresetResult loadFile(std::string_view path,
                                  PresetStateSink &sink) const noexcept = 0;
};

} // namespace webview_gui::examples::presets
