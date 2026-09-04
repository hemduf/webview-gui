#include "../../common/preset_load_controller.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {

using namespace webview_gui::examples::presets;

struct FakeCatalog final : PresetCatalog {
    bool failLoad = false;

    std::string_view fileExtension() const noexcept override { return ".wvpreset"; }
    bool nativeUserLocation(std::string_view &) const noexcept override { return false; }
    PresetResult enumerateFactoryMetadata(PresetMetadataSink &) const noexcept override {
        return PresetResult::success();
    }
    PresetResult metadataForFile(std::string_view, PresetMetadataSink &) const noexcept override {
        return PresetResult::unsupported();
    }
    PresetResult loadFactory(std::string_view loadKey, PresetStateSink &sink) const noexcept override {
        if (failLoad)
            return PresetResult::error("catalog failure");
        if (loadKey != "test:key" || !sink.beginCandidate("test.plugin") ||
            !sink.setParameter(1u, 0.5) || !sink.endCandidate())
            return PresetResult::error("candidate failure");
        return PresetResult::success();
    }
    PresetResult loadFile(std::string_view, PresetStateSink &) const noexcept override {
        return PresetResult::unsupported();
    }
};

struct HostState {
    clap_host_t host{};
    clap_host_params_t params{};
    clap_host_preset_load_t presetLoad{};
    bool exposeParams = true;
    bool exposePresetLoad = true;
    std::uint32_t rescans = 0u;
    std::uint32_t loaded = 0u;
    std::uint32_t errors = 0u;
    clap_param_rescan_flags flags = 0u;
    std::string order;

    HostState() noexcept {
        params.rescan = rescan;
        params.clear = clear;
        params.request_flush = requestFlush;
        presetLoad.on_error = onError;
        presetLoad.loaded = onLoaded;
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "preset notification contract";
        host.vendor = "webview-gui";
        host.url = "";
        host.version = "0";
        host.get_extension = getExtension;
        host.request_restart = requestRestart;
        host.request_process = requestProcess;
        host.request_callback = requestCallback;
    }

    void reset() noexcept {
        rescans = 0u;
        loaded = 0u;
        errors = 0u;
        flags = 0u;
        order.clear();
    }

    static HostState *from(const clap_host_t *host) noexcept {
        return host ? static_cast<HostState *>(host->host_data) : nullptr;
    }

    static const void *CLAP_ABI getExtension(const clap_host_t *host, const char *id) {
        auto *self = from(host);
        if (!self || !id)
            return nullptr;
        if (self->exposeParams && std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &self->params;
        if (self->exposePresetLoad &&
            (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0 ||
             std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0))
            return &self->presetLoad;
        return nullptr;
    }

    static void CLAP_ABI rescan(const clap_host_t *host, clap_param_rescan_flags newFlags) {
        if (auto *self = from(host)) {
            ++self->rescans;
            self->flags |= newFlags;
            self->order.push_back('R');
        }
    }
    static void CLAP_ABI clear(const clap_host_t *, clap_id, clap_param_clear_flags) {}
    static void CLAP_ABI requestFlush(const clap_host_t *) {}
    static void CLAP_ABI requestRestart(const clap_host_t *) {}
    static void CLAP_ABI requestProcess(const clap_host_t *) {}
    static void CLAP_ABI requestCallback(const clap_host_t *) {}

    static void CLAP_ABI onError(const clap_host_t *host,
                                 std::uint32_t,
                                 const char *,
                                 const char *,
                                 std::int32_t,
                                 const char *) {
        if (auto *self = from(host)) {
            ++self->errors;
            self->order.push_back('E');
        }
    }

    static void CLAP_ABI onLoaded(const clap_host_t *host,
                                  std::uint32_t,
                                  const char *,
                                  const char *) {
        if (auto *self = from(host)) {
            ++self->loaded;
            self->order.push_back('L');
        }
    }
};

const clap_host_preset_load_t *presetHostExtension(HostState &host) noexcept {
    return host.exposePresetLoad ? &host.presetLoad : nullptr;
}

bool runLoad(FakeCatalog &catalog,
             HostState &host,
             bool commitSucceeds,
             std::uint32_t &commitCalls) noexcept {
    return loadPresetFromLocation(
        catalog,
        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        nullptr,
        "test:key",
        &host.host,
        presetHostExtension(host),
        [&](const PresetDocument &document) noexcept {
            ++commitCalls;
            if (document.metadata.targetPluginId != "test.plugin" || document.parameters.size() != 1u)
                return PresetResult::error("unexpected candidate");
            return commitSucceeds ? PresetResult::success()
                                  : PresetResult::error("commit rejected");
        });
}

} // namespace

int main() {
    FakeCatalog catalog;
    HostState host;
    std::uint32_t commitCalls = 0u;

    if (!runLoad(catalog, host, true, commitCalls) || commitCalls != 1u ||
        host.rescans != 1u || (host.flags & CLAP_PARAM_RESCAN_VALUES) == 0u ||
        host.loaded != 1u || host.errors != 0u || host.order != "RL")
        return 1;

    host.reset();
    host.exposeParams = false;
    if (!runLoad(catalog, host, true, commitCalls) || host.rescans != 0u ||
        host.loaded != 1u || host.errors != 0u || host.order != "L")
        return 2;

    host.reset();
    host.exposeParams = true;
    host.exposePresetLoad = false;
    if (!runLoad(catalog, host, true, commitCalls) || host.rescans != 1u ||
        host.loaded != 0u || host.errors != 0u || host.order != "R")
        return 3;

    host.reset();
    host.exposePresetLoad = true;
    if (runLoad(catalog, host, false, commitCalls) || host.rescans != 0u ||
        host.loaded != 0u || host.errors != 1u || host.order != "E")
        return 4;

    host.reset();
    catalog.failLoad = true;
    if (runLoad(catalog, host, true, commitCalls) || host.rescans != 0u ||
        host.loaded != 0u || host.errors != 1u || host.order != "E")
        return 5;

    return 0;
}
