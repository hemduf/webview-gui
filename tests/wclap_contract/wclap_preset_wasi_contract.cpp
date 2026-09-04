#include "preset_discovery_factory.h"
#include "preset_load_controller.h"
#include "preset_production_catalog.h"
#include "presets/preset_factory_catalog.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace webview_gui::examples::presets;

struct GainWasiTag {
    inline static constexpr const char *providerId = "com.webview-gui.test.gain.wasi-presets";
    inline static constexpr const char *providerName = "Gain WASI presets";
    inline static constexpr const char *vendor = "webview-gui";

    static std::unique_ptr<PresetCatalog> createPresetCatalog() noexcept {
        return makeDefaultProductionPresetCatalog(gainFactoryPresetCatalog(),
                                                  kGainFactoryTargetPluginId);
    }
};

struct IndexerState {
    clap_preset_discovery_indexer_t indexer{};
    std::uint32_t filetypes = 0u;
    std::uint32_t pluginLocations = 0u;
    std::uint32_t fileLocations = 0u;

    IndexerState() noexcept {
        indexer.clap_version = CLAP_VERSION;
        indexer.name = "WASI preset contract";
        indexer.vendor = "webview-gui";
        indexer.url = "";
        indexer.version = "0";
        indexer.indexer_data = this;
        indexer.declare_filetype = declareFiletype;
        indexer.declare_location = declareLocation;
        indexer.declare_soundpack = declareSoundpack;
        indexer.get_extension = getExtension;
    }

    static IndexerState *from(const clap_preset_discovery_indexer_t *indexer) noexcept {
        return indexer ? static_cast<IndexerState *>(indexer->indexer_data) : nullptr;
    }

    static bool CLAP_ABI declareFiletype(const clap_preset_discovery_indexer_t *indexer,
                                         const clap_preset_discovery_filetype_t *filetype) {
        auto *self = from(indexer);
        if (!self || !filetype || !filetype->file_extension ||
            std::strcmp(filetype->file_extension, "wvpreset") != 0)
            return false;
        ++self->filetypes;
        return true;
    }

    static bool CLAP_ABI declareLocation(const clap_preset_discovery_indexer_t *indexer,
                                         const clap_preset_discovery_location_t *location) {
        auto *self = from(indexer);
        if (!self || !location)
            return false;
        if (location->kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
            if (location->location != nullptr ||
                (location->flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT) == 0u)
                return false;
            ++self->pluginLocations;
            return true;
        }
        if (location->kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE) {
            ++self->fileLocations;
            return false;
        }
        return false;
    }

    static bool CLAP_ABI declareSoundpack(const clap_preset_discovery_indexer_t *,
                                          const clap_preset_discovery_soundpack_t *) {
        return false;
    }

    static const void *CLAP_ABI getExtension(const clap_preset_discovery_indexer_t *,
                                             const char *) {
        return nullptr;
    }
};

struct MetadataState {
    clap_preset_discovery_metadata_receiver_t receiver{};
    std::uint32_t presets = 0u;
    std::uint32_t pluginIds = 0u;
    std::uint32_t errors = 0u;
    std::vector<std::string> loadKeys;

    MetadataState() noexcept {
        receiver.receiver_data = this;
        receiver.on_error = onError;
        receiver.begin_preset = beginPreset;
        receiver.add_plugin_id = addPluginId;
        receiver.set_soundpack_id = setSoundpackId;
        receiver.set_flags = setFlags;
        receiver.add_creator = addCreator;
        receiver.set_description = setDescription;
        receiver.set_timestamps = setTimestamps;
        receiver.add_feature = addFeature;
        receiver.add_extra_info = addExtraInfo;
    }

    static MetadataState *from(const clap_preset_discovery_metadata_receiver_t *receiver) noexcept {
        return receiver ? static_cast<MetadataState *>(receiver->receiver_data) : nullptr;
    }

    static void CLAP_ABI onError(const clap_preset_discovery_metadata_receiver_t *receiver,
                                 std::int32_t,
                                 const char *) {
        if (auto *self = from(receiver))
            ++self->errors;
    }

    static bool CLAP_ABI beginPreset(const clap_preset_discovery_metadata_receiver_t *receiver,
                                     const char *name,
                                     const char *loadKey) {
        auto *self = from(receiver);
        if (!self || !name || name[0] == '\0' || !loadKey || loadKey[0] == '\0')
            return false;
        ++self->presets;
        self->loadKeys.emplace_back(loadKey);
        return true;
    }

    static void CLAP_ABI addPluginId(const clap_preset_discovery_metadata_receiver_t *receiver,
                                     const clap_universal_plugin_id_t *pluginId) {
        auto *self = from(receiver);
        if (self && pluginId && pluginId->abi && pluginId->id &&
            std::strcmp(pluginId->abi, "clap") == 0 &&
            std::strcmp(pluginId->id, kGainFactoryTargetPluginId.data()) == 0)
            ++self->pluginIds;
    }

    static void CLAP_ABI setSoundpackId(const clap_preset_discovery_metadata_receiver_t *,
                                        const char *) {}
    static void CLAP_ABI setFlags(const clap_preset_discovery_metadata_receiver_t *,
                                  std::uint32_t) {}
    static void CLAP_ABI addCreator(const clap_preset_discovery_metadata_receiver_t *,
                                    const char *) {}
    static void CLAP_ABI setDescription(const clap_preset_discovery_metadata_receiver_t *,
                                        const char *) {}
    static void CLAP_ABI setTimestamps(const clap_preset_discovery_metadata_receiver_t *,
                                       clap_timestamp,
                                       clap_timestamp) {}
    static void CLAP_ABI addFeature(const clap_preset_discovery_metadata_receiver_t *,
                                    const char *) {}
    static void CLAP_ABI addExtraInfo(const clap_preset_discovery_metadata_receiver_t *,
                                      const char *,
                                      const char *) {}
};

struct CandidateSink final : PresetStateSink {
    std::string target;
    std::vector<PresetParameterValue> params;
    bool building = false;
    bool complete = false;

    bool beginCandidate(std::string_view targetPluginId) noexcept override {
        if (building || targetPluginId.empty())
            return false;
        target.assign(targetPluginId.data(), targetPluginId.size());
        params.clear();
        building = true;
        complete = false;
        return true;
    }

    bool setParameter(std::uint32_t id, double value) noexcept override {
        if (!building || !std::isfinite(value))
            return false;
        params.push_back({id, value});
        return true;
    }

    bool endCandidate() noexcept override {
        if (!building)
            return false;
        building = false;
        complete = true;
        return true;
    }
};

struct HostState {
    clap_host_t host{};
    clap_host_params_t params{};
    clap_host_preset_load_t presetLoad{};
    std::uint32_t rescans = 0u;
    std::uint32_t loaded = 0u;
    std::uint32_t errors = 0u;
    std::string order;

    HostState() noexcept {
        params.rescan = rescan;
        params.clear = clear;
        params.request_flush = requestFlush;
        presetLoad.on_error = onError;
        presetLoad.loaded = onLoaded;
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "WASI preset host";
        host.vendor = "webview-gui";
        host.url = "";
        host.version = "0";
        host.get_extension = getExtension;
        host.request_restart = requestRestart;
        host.request_process = requestProcess;
        host.request_callback = requestCallback;
    }

    static HostState *from(const clap_host_t *host) noexcept {
        return host ? static_cast<HostState *>(host->host_data) : nullptr;
    }

    static const void *CLAP_ABI getExtension(const clap_host_t *host, const char *id) {
        auto *self = from(host);
        if (!self || !id)
            return nullptr;
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &self->params;
        if (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0 ||
            std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0)
            return &self->presetLoad;
        return nullptr;
    }

    static void CLAP_ABI rescan(const clap_host_t *host, clap_param_rescan_flags flags) {
        auto *self = from(host);
        if (self && (flags & CLAP_PARAM_RESCAN_VALUES) != 0u) {
            ++self->rescans;
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

bool near(double actual, double expected) noexcept {
    return std::fabs(actual - expected) < 1.0e-6;
}

} // namespace

int main() {
    const auto &factoryCatalog = gainFactoryPresetCatalog();
    if (factoryCatalog.size() != 3u || factoryCatalog.fileExtension() != "wvpreset")
        return 1;

    const auto *factory = presetDiscoveryFactory<GainWasiTag>();
    if (!factory || !factory->count || factory->count(factory) != 1u ||
        !factory->get_descriptor || !factory->create)
        return 2;

    IndexerState indexer;
    const auto *descriptor = factory->get_descriptor(factory, 0u);
    if (!descriptor || !descriptor->id)
        return 3;
    const auto *provider = factory->create(factory, &indexer.indexer, descriptor->id);
    if (!provider || !provider->init || !provider->destroy || !provider->get_metadata)
        return 4;
    if (!provider->init(provider) || indexer.filetypes != 1u ||
        indexer.pluginLocations != 1u || indexer.fileLocations != 0u) {
        provider->destroy(provider);
        return 5;
    }

    MetadataState metadata;
    if (!provider->get_metadata(provider,
                                CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                nullptr,
                                &metadata.receiver) ||
        metadata.errors != 0u || metadata.presets != 3u || metadata.pluginIds != 3u ||
        metadata.loadKeys.size() != 3u || metadata.loadKeys[0] != "gain:unity" ||
        metadata.loadKeys[1] != "gain:trim-minus-6db" ||
        metadata.loadKeys[2] != "gain:boost-plus-6db") {
        provider->destroy(provider);
        return 6;
    }
    provider->destroy(provider);

    auto catalog = makeDefaultProductionPresetCatalog(factoryCatalog,
                                                       kGainFactoryTargetPluginId);
    if (!catalog)
        return 7;
    std::string_view userLocation;
    if (catalog->nativeUserLocation(userLocation))
        return 8;

    CandidateSink sink;
    const auto load = catalog->loadFactory("gain:trim-minus-6db", sink);
    if (!load.succeeded() || !sink.complete || sink.target != kGainFactoryTargetPluginId ||
        sink.params.size() != 2u || sink.params[0].stableParameterId != 0x1000u ||
        !near(sink.params[0].value, -6.0) || sink.params[1].stableParameterId != 0x1001u ||
        !near(sink.params[1].value, 0.0))
        return 9;

    HostState host;
    bool committed = false;
    const bool loaded = loadPresetFromLocation(
        *catalog,
        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        nullptr,
        "gain:boost-plus-6db",
        &host.host,
        &host.presetLoad,
        [&](const PresetDocument &document) noexcept {
            committed = document.metadata.targetPluginId == kGainFactoryTargetPluginId &&
                        document.parameters.size() == 2u &&
                        near(document.parameters[0].value, 6.0);
            if (committed)
                host.order.push_back('C');
            return committed ? PresetResult::success()
                             : PresetResult::error("unexpected WASI candidate");
        });
    if (!loaded || !committed || host.rescans != 1u || host.loaded != 1u ||
        host.errors != 0u || host.order != "CRL")
        return 10;

    committed = false;
    host.order.clear();
    if (loadPresetFromLocation(*catalog,
                               CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                               "/presets/user.wvpreset",
                               nullptr,
                               &host.host,
                               &host.presetLoad,
                               [&](const PresetDocument &) noexcept {
                                   committed = true;
                                   return PresetResult::success();
                               }) || committed || host.order != "E")
        return 11;

    return 0;
}
