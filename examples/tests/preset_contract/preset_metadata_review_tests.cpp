#include "preset_discovery_factory.h"
#include "preset_production_catalog.h"
#include "presets/preset_factory_catalog.h"
#include "presets/preset_storage.h"

#include <clap/factory/preset-discovery.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace presets = webview_gui::examples::presets;

namespace {

#if defined(_WIN32)
constexpr std::string_view kRoot = "C:/webview-gui-review-presets";
constexpr std::string_view kMissing = "C:/webview-gui-review-presets/missing.wvpreset";
constexpr std::string_view kBroken = "C:/webview-gui-review-presets/broken.wvpreset";
constexpr std::string_view kWrong = "C:/webview-gui-review-presets/wrong.wvpreset";
#else
constexpr std::string_view kRoot = "/tmp/webview-gui-review-presets";
constexpr std::string_view kMissing = "/tmp/webview-gui-review-presets/missing.wvpreset";
constexpr std::string_view kBroken = "/tmp/webview-gui-review-presets/broken.wvpreset";
constexpr std::string_view kWrong = "/tmp/webview-gui-review-presets/wrong.wvpreset";
#endif

struct IndexerState {
    int locations = 0;
};

bool CLAP_ABI declareFiletype(const clap_preset_discovery_indexer_t *,
                              const clap_preset_discovery_filetype_t *filetype) {
    return filetype && filetype->file_extension &&
           std::string_view{filetype->file_extension} == "wvpreset";
}

bool CLAP_ABI declareLocation(const clap_preset_discovery_indexer_t *indexer,
                              const clap_preset_discovery_location_t *location) {
    auto *state = static_cast<IndexerState *>(indexer->indexer_data);
    assert(state && location);
    state->locations++;
    return true;
}

clap_preset_discovery_indexer_t makeIndexer(IndexerState &state) {
    clap_preset_discovery_indexer_t indexer{};
    indexer.clap_version = CLAP_VERSION;
    indexer.name = "#91 review indexer";
    indexer.indexer_data = &state;
    indexer.declare_filetype = declareFiletype;
    indexer.declare_location = declareLocation;
    return indexer;
}

struct ReceiverState {
    bool throwOnBegin = false;
    int errors = 0;
    int begins = 0;
    int metadataCallbacks = 0;
    std::string errorMessage;
    std::vector<std::string> abis;
    std::vector<std::string> pluginIds;
};

ReceiverState *stateOf(const clap_preset_discovery_metadata_receiver_t *receiver) {
    return receiver ? static_cast<ReceiverState *>(receiver->receiver_data) : nullptr;
}

void CLAP_ABI onError(const clap_preset_discovery_metadata_receiver_t *receiver,
                      std::int32_t,
                      const char *message) {
    auto *state = stateOf(receiver);
    assert(state);
    state->errors++;
    state->errorMessage = message ? message : "";
}

bool CLAP_ABI beginPreset(const clap_preset_discovery_metadata_receiver_t *receiver,
                          const char *,
                          const char *) {
    auto *state = stateOf(receiver);
    assert(state);
    state->begins++;
    if (state->throwOnBegin)
        throw std::runtime_error{"deterministic receiver adapter failure"};
    return true;
}

void CLAP_ABI addPluginId(const clap_preset_discovery_metadata_receiver_t *receiver,
                          const clap_universal_plugin_id_t *id) {
    auto *state = stateOf(receiver);
    assert(state && id);
    state->metadataCallbacks++;
    state->abis.emplace_back(id->abi ? id->abi : "");
    state->pluginIds.emplace_back(id->id ? id->id : "");
}

void CLAP_ABI setFlags(const clap_preset_discovery_metadata_receiver_t *receiver,
                       std::uint32_t) {
    auto *state = stateOf(receiver);
    assert(state);
    state->metadataCallbacks++;
}

void CLAP_ABI stringMetadata(const clap_preset_discovery_metadata_receiver_t *receiver,
                             const char *) {
    auto *state = stateOf(receiver);
    assert(state);
    state->metadataCallbacks++;
}

void CLAP_ABI timestamps(const clap_preset_discovery_metadata_receiver_t *receiver,
                         clap_timestamp,
                         clap_timestamp) {
    auto *state = stateOf(receiver);
    assert(state);
    state->metadataCallbacks++;
}

clap_preset_discovery_metadata_receiver_t makeReceiver(ReceiverState &state) {
    clap_preset_discovery_metadata_receiver_t receiver{};
    receiver.receiver_data = &state;
    receiver.on_error = onError;
    receiver.begin_preset = beginPreset;
    receiver.add_plugin_id = addPluginId;
    receiver.set_flags = setFlags;
    receiver.add_creator = stringMetadata;
    receiver.set_description = stringMetadata;
    receiver.set_timestamps = timestamps;
    receiver.add_feature = stringMetadata;
    return receiver;
}

class ListingStorage final : public presets::PresetUserStorage {
public:
    explicit ListingStorage(presets::PresetStorageListResult listing)
        : listing_(std::move(listing)) {}

    presets::PresetStorageKind kind() const noexcept override {
        return presets::PresetStorageKind::Native;
    }
    std::string_view targetPluginId() const noexcept override {
        return presets::kGainFactoryTargetPluginId;
    }
    std::optional<std::string> nativeFileRoot() const override {
        return std::string{kRoot};
    }
    presets::PresetStorageListResult list() const override { return listing_; }
    presets::PresetStorageLoadResult load(std::string_view) const override {
        return {{presets::PresetStorageError::Unavailable}, std::nullopt};
    }
    presets::PresetStorageSaveResult saveAs(std::string_view,
                                             const presets::PresetDocument &,
                                             bool) override {
        return {{presets::PresetStorageError::Unavailable}, {}};
    }
    presets::PresetStorageStatus remove(std::string_view) override {
        return {presets::PresetStorageError::Unavailable};
    }

private:
    presets::PresetStorageListResult listing_;
};

std::unique_ptr<presets::PresetCatalog> makePolyCatalog() noexcept {
    try {
        return presets::makeProductionPresetCatalog(
            presets::polySynthFactoryPresetCatalog(),
            presets::kPolySynthFactoryTargetPluginId,
            std::make_unique<presets::UnavailablePresetUserStorage>(
                std::string{presets::kPolySynthFactoryTargetPluginId}));
    } catch (...) {
        return {};
    }
}

std::unique_ptr<presets::PresetCatalog> makeListingCatalog(
    presets::PresetStorageListResult listing) noexcept {
    try {
        return presets::makeProductionPresetCatalog(
            presets::gainFactoryPresetCatalog(),
            presets::kGainFactoryTargetPluginId,
            std::make_unique<ListingStorage>(std::move(listing)));
    } catch (...) {
        return {};
    }
}

struct PolyTag {
    inline static constexpr const char *providerId = "com.webview-gui.review.poly";
    inline static constexpr const char *providerName = "#91 review PolySynth";
    inline static constexpr const char *vendor = "webview-gui";
    inline static constexpr const char *targetPluginId = "com.webview-gui.example.polysynth";
    static std::unique_ptr<presets::PresetCatalog> createPresetCatalog() noexcept {
        return makePolyCatalog();
    }
};

struct MissingTag {
    inline static constexpr const char *providerId = "com.webview-gui.review.missing";
    inline static constexpr const char *providerName = "#91 review missing";
    inline static constexpr const char *vendor = "webview-gui";
    inline static constexpr const char *targetPluginId = "com.webview-gui.example.gain";
    static std::unique_ptr<presets::PresetCatalog> createPresetCatalog() noexcept {
        return makeListingCatalog({});
    }
};

struct MalformedTag {
    inline static constexpr const char *providerId = "com.webview-gui.review.malformed";
    inline static constexpr const char *providerName = "#91 review malformed";
    inline static constexpr const char *vendor = "webview-gui";
    inline static constexpr const char *targetPluginId = "com.webview-gui.example.gain";
    static std::unique_ptr<presets::PresetCatalog> createPresetCatalog() noexcept {
        presets::PresetStorageListResult listing;
        presets::PresetStorageStatus diagnostic;
        diagnostic.error = presets::PresetStorageError::ParseFailed;
        diagnostic.codecError = presets::PresetCodecError::MalformedInput;
        diagnostic.diagnosticPath = std::string{kBroken};
        listing.diagnostics.push_back(std::move(diagnostic));
        return makeListingCatalog(std::move(listing));
    }
};

struct WrongTag {
    inline static constexpr const char *providerId = "com.webview-gui.review.wrong";
    inline static constexpr const char *providerName = "#91 review wrong target";
    inline static constexpr const char *vendor = "webview-gui";
    inline static constexpr const char *targetPluginId = "com.webview-gui.example.gain";
    static std::unique_ptr<presets::PresetCatalog> createPresetCatalog() noexcept {
        presets::PresetStorageListResult listing;
        presets::PresetMetadata metadata;
        metadata.targetPluginId = presets::kPolySynthFactoryTargetPluginId;
        metadata.name = "Wrong Target";
        listing.entries.push_back({"wrong.wvpreset", std::move(metadata)});
        return makeListingCatalog(std::move(listing));
    }
};

template <typename Tag>
const clap_preset_discovery_provider_t *createProvider(
    IndexerState &indexerState,
    clap_preset_discovery_indexer_t &indexer) {
    const auto *factory = presets::presetDiscoveryFactory<Tag>();
    assert(factory);
    const auto *provider = factory->create(factory, &indexer, Tag::providerId);
    assert(provider);
    assert(provider->init(provider));
    return provider;
}

void testPolySynthUniversalIds() {
    IndexerState indexerState;
    auto indexer = makeIndexer(indexerState);
    const auto *provider = createProvider<PolyTag>(indexerState, indexer);
    ReceiverState state;
    auto receiver = makeReceiver(state);
    assert(provider->get_metadata(provider,
                                  CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                  nullptr,
                                  &receiver));
    assert(state.errors == 0);
    assert(state.begins == 6);
    assert(state.abis.size() == 6u);
    assert(state.pluginIds.size() == 6u);
    for (std::size_t i = 0u; i < 6u; ++i) {
        assert(state.abis[i] == "clap");
        assert(state.pluginIds[i] == presets::kPolySynthFactoryTargetPluginId);
    }
    provider->destroy(provider);
}

void testAdapterFailureIsNotCancellation() {
    IndexerState indexerState;
    auto indexer = makeIndexer(indexerState);
    const auto *provider = createProvider<PolyTag>(indexerState, indexer);
    ReceiverState state;
    state.throwOnBegin = true;
    auto receiver = makeReceiver(state);
    assert(!provider->get_metadata(provider,
                                   CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                   nullptr,
                                   &receiver));
    assert(state.begins == 1);
    assert(state.metadataCallbacks == 0);
    assert(state.errors == 1);
    assert(!state.errorMessage.empty());
    provider->destroy(provider);
}

template <typename Tag>
void expectFileError(std::string_view location) {
    IndexerState indexerState;
    auto indexer = makeIndexer(indexerState);
    const auto *provider = createProvider<Tag>(indexerState, indexer);
    assert(indexerState.locations == 2);
    ReceiverState state;
    auto receiver = makeReceiver(state);
    const std::string locationText{location};
    assert(!provider->get_metadata(provider,
                                   CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                   locationText.c_str(),
                                   &receiver));
    assert(state.begins == 0);
    assert(state.metadataCallbacks == 0);
    assert(state.errors == 1);
    assert(!state.errorMessage.empty());
    provider->destroy(provider);
}

void testProviderErrorPropagation() {
    expectFileError<MissingTag>(kMissing);
    expectFileError<MalformedTag>(kBroken);
    expectFileError<WrongTag>(kWrong);

    IndexerState indexerState;
    auto indexer = makeIndexer(indexerState);
    const auto *provider = createProvider<PolyTag>(indexerState, indexer);
    ReceiverState state;
    auto receiver = makeReceiver(state);
    assert(!provider->get_metadata(provider, 0xFFFFFFFFu, nullptr, &receiver));
    assert(state.begins == 0);
    assert(state.metadataCallbacks == 0);
    assert(state.errors == 1);
    assert(!state.errorMessage.empty());
    provider->destroy(provider);
}

} // namespace

int main() {
    testPolySynthUniversalIds();
    testAdapterFailureIsNotCancellation();
    testProviderErrorPropagation();
    return 0;
}
