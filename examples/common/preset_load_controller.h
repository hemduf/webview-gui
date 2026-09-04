#pragma once

#include "preset_clap_contract.h"
#include "presets/preset_document.h"

#if defined(__wasi__)
#include "preset_wasi_production_catalog.h"
#endif

#include <clap/ext/params.h>
#include <clap/factory/preset-discovery.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace webview_gui::examples::presets {

class PresetDocumentStateSink final : public PresetStateSink {
public:
    bool beginCandidate(std::string_view targetPluginId) noexcept override {
        if (building_ || targetPluginId.empty())
            return false;
        try {
            document_ = {};
            document_.metadata.targetPluginId.assign(targetPluginId.data(), targetPluginId.size());
            document_.metadata.name = "CLAP preset load candidate";
            building_ = true;
            completed_ = false;
            return true;
        } catch (...) {
            document_ = {};
            building_ = false;
            completed_ = false;
            return false;
        }
    }

    bool setParameter(std::uint32_t stableParameterId, double value) noexcept override {
        if (!building_)
            return false;
        try {
            document_.parameters.push_back({stableParameterId, value});
            return true;
        } catch (...) {
            return false;
        }
    }

    bool endCandidate() noexcept override {
        if (!building_)
            return false;
        building_ = false;
        const auto validation = validatePresetDocument(document_);
        completed_ = validation.ok();
        return completed_;
    }

    [[nodiscard]] const PresetDocument *candidate() const noexcept {
        return completed_ ? &document_ : nullptr;
    }

private:
    PresetDocument document_{};
    bool building_ = false;
    bool completed_ = false;
};

namespace preset_load_detail {

inline void notifyLoadError(const clap_host_t *host,
                            const clap_host_preset_load_t *hostPresetLoad,
                            std::uint32_t locationKind,
                            const char *location,
                            const char *loadKey,
                            const PresetResult &result) noexcept {
    if (!host || !hostPresetLoad || !hostPresetLoad->on_error)
        return;

    try {
        const std::string message = result.message.empty()
                                        ? std::string{"preset load failed"}
                                        : std::string{result.message};
        hostPresetLoad->on_error(host,
                                 locationKind,
                                 location,
                                 loadKey,
                                 result.osError,
                                 message.c_str());
    } catch (...) {
        hostPresetLoad->on_error(host,
                                 locationKind,
                                 location,
                                 loadKey,
                                 result.osError,
                                 "preset load failed");
    }
}

inline void notifyHostParameterValuesChanged(const clap_host_t *host) noexcept {
    if (!host || !host->get_extension)
        return;
    const auto *hostParams = static_cast<const clap_host_params_t *>(
        host->get_extension(host, CLAP_EXT_PARAMS));
    if (hostParams && hostParams->rescan)
        hostParams->rescan(host, CLAP_PARAM_RESCAN_VALUES);
}

[[nodiscard]] inline PresetResult validateLocationShape(std::uint32_t locationKind,
                                                        const char *location,
                                                        const char *loadKey) noexcept {
    if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
        if (location != nullptr)
            return PresetResult::error("PLUGIN preset location must be null");
        if (!loadKey || loadKey[0] == '\0')
            return PresetResult::error("PLUGIN preset load key is required");
        return PresetResult::success();
    }

    if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_FILE) {
        if (!location || location[0] == '\0')
            return PresetResult::error("FILE preset location is required");
        if (loadKey != nullptr)
            return PresetResult::error("FILE preset load key must be null");
        return PresetResult::success();
    }

    return PresetResult::unsupported("unsupported preset location kind");
}

} // namespace preset_load_detail

inline void notifyPresetLoadFailure(const clap_host_t *host,
                                    const clap_host_preset_load_t *hostPresetLoad,
                                    std::uint32_t locationKind,
                                    const char *location,
                                    const char *loadKey,
                                    const PresetResult &result) noexcept {
    preset_load_detail::notifyLoadError(host,
                                        hostPresetLoad,
                                        locationKind,
                                        location,
                                        loadKey,
                                        result);
}

template <typename CommitFn>
[[nodiscard]] bool loadPresetFromLocation(PresetCatalog &catalog,
                                          std::uint32_t locationKind,
                                          const char *location,
                                          const char *loadKey,
                                          const clap_host_t *host,
                                          const clap_host_preset_load_t *hostPresetLoad,
                                          CommitFn &&commit) noexcept {
    const auto locationValidation =
        preset_load_detail::validateLocationShape(locationKind, location, loadKey);
    if (!locationValidation.succeeded()) {
        notifyPresetLoadFailure(host,
                                hostPresetLoad,
                                locationKind,
                                location,
                                loadKey,
                                locationValidation);
        return false;
    }

    PresetDocumentStateSink sink;
    PresetResult loadResult = PresetResult::error("preset load failed");
    try {
        if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN)
            loadResult = catalog.loadFactory(loadKey, sink);
        else
            loadResult = catalog.loadFile(location, sink);
    } catch (...) {
        loadResult = PresetResult::error("preset catalog load threw an exception");
    }

    if (!loadResult.succeeded()) {
        notifyPresetLoadFailure(host,
                                hostPresetLoad,
                                locationKind,
                                location,
                                loadKey,
                                loadResult);
        return false;
    }

    const auto *document = sink.candidate();
    if (!document) {
        const auto result = PresetResult::error("preset catalog produced no complete candidate");
        notifyPresetLoadFailure(host,
                                hostPresetLoad,
                                locationKind,
                                location,
                                loadKey,
                                result);
        return false;
    }

    PresetResult commitResult = PresetResult::error("preset state commit failed");
    try {
        commitResult = std::forward<CommitFn>(commit)(*document);
    } catch (...) {
        commitResult = PresetResult::error("preset state commit threw an exception");
    }

    if (!commitResult.succeeded()) {
        notifyPresetLoadFailure(host,
                                hostPresetLoad,
                                locationKind,
                                location,
                                loadKey,
                                commitResult);
        return false;
    }

    // A preset load is an out-of-band base-state replacement, not automation.
    // Publish the complete candidate first, then invalidate host-visible values,
    // and only then report the successful preset-load transaction.
    preset_load_detail::notifyHostParameterValuesChanged(host);
    if (host && hostPresetLoad && hostPresetLoad->loaded)
        hostPresetLoad->loaded(host, locationKind, location, loadKey);
    return true;
}

} // namespace webview_gui::examples::presets
