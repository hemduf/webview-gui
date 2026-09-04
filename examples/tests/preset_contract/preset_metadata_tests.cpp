#include "preset_production_catalog.h"
#include "preset_discovery_factory.h"

#include "presets/preset_factory_catalog.h"
#include "presets/preset_storage.h"

#include <clap/factory/preset-discovery.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace presets = webview_gui::examples::presets;

namespace {

#if defined(_WIN32)
constexpr std::string_view kUserRoot = "C:/webview-gui-test-presets";
constexpr std::string_view kUserFile = "C:/webview-gui-test-presets/user-one.wvpreset";
constexpr std::string_view kWrongFile = "C:/webview-gui-test-presets/wrong.wvpreset";
constexpr std::string_view kBrokenFile = "C:/webview-gui-test-presets/broken.wvpreset";
#else
constexpr std::string_view kUserRoot = "/tmp/webview-gui-test-presets";
constexpr std::string_view kUserFile = "/tmp/webview-gui-test-presets/user-one.wvpreset";
constexpr std::string_view kWrongFile = "/tmp/webview-gui-test-presets/wrong.wvpreset";
constexpr std::string_view kBrokenFile = "/tmp/webview-gui-test-presets/broken.wvpreset";
#endif

struct ReceiverState {
    bool acceptBegin = true;
    int errorCount = 0;
    int beginCount = 0;
    int pluginIdCount = 0;
    int flagsCount = 0;
    int creatorCount = 0;
    int descriptionCount = 0;
    int timestampCount = 0;
    int featureCount = 0;
    int callbackCountAfterRejectedBegin = 0;
    std::int32_t lastOsError = 0;
    std::string lastError;
    std::vector<std::string> names;
    std::vector<std::optional<std::string>> loadKeys;
    std::vector<std::string> pluginAbis;
    std::vector<std::string> pluginIds;
    std::vector<std::uint32_t> flags;
    std::vector<std::string> creators;
    std::vector<std::string> descriptions;
    std::vector<std::string> features;
    std::vector<std::pair<clap_timestamp, clap_timestamp>> timestamps;
    bool rejectedBeginSeen = false;
};

ReceiverState *receiverState(
    const clap_preset_discovery_metadata_receiver_t *receiver) noexcept {
    return receiver ? static_cast<ReceiverState *>(receiver->receiver_data) : nullptr;
}

void CLAP_ABI receiverError(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    std::int32_t osError,
    const char *message) {
    auto *state = receiverState(receiver);
    assert(state != nullptr);
    state->errorCount++;
    state->lastOsError = osError;
    state->lastError = message ? message : "";
}

bool CLAP_ABI receiverBegin(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    const char *name,
    const char *loadKey) {
    auto *state = receiverState(receiver);
    assert(state != nullptr);
    state->beginCount++;
    state->names.emplace_back(name ? name : "");
    if (loadKey)
        state->loadKeys.emplace_back(std::string{loadKey});
    else
        state->loadKeys.emplace_back(std::nullopt);
    if (!state->acceptBegin)
        state->rejectedBeginSeen = true;
    return state->acceptBegin;
}

void noteCallbackAfterBegin(ReceiverState &state) {
    if (state.rejectedBeginSeen)
        state.callbackCountAfterRejectedBegin++;
}

void CLAP_ABI receiverPluginId(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    const clap_universal_plugin_id_t *pluginId) {
    auto *state = receiverState(receiver);
    assert(state != nullptr);
    noteCallbackAfterBegin(*state);
    state->pluginIdCount++;
    assert(pluginId != nullptr);
    state->pluginAbis.emplace_back(pluginId->abi ? pluginId->abi : "");
    state->pluginIds.emplace_back(pluginId->id ? pluginId->id : "");
}

void CLAP_ABI receiverFlags(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    std::uint32_t flags) {
    auto *state = receiverState(receiver);
    assert(state != nullptr);
    noteCallbackAfterBegin(*state);
    state->flagsCount++;
    state->flags.push_back(flags);
}

void CLAP_ABI receiverCreator(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    const char *creator) {
    auto *state = receiverState(receiver);
    assert(state != nullptr);
    noteCallbackAfterBegin(*state);
    state->creatorCount++;
    state->creators.emplace_back(creator ? creator : "");
}

void CLAP_ABI receiverDescription(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    const char *description) {
    auto *state = receiverState(receiver);
    assert(state != nullptr);
    noteCallbackAfterBegin(*state);
    state->descriptionCount++;
    state->descriptions.emplace_back(description ? description : "");
}

void CLAP_ABI receiverTimestamps(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    clap_timestamp creation,
    clap_timestamp modification) {
    auto *state = receiverState(receiver);
    assert(state != nullptr);
    noteCallbackAfterBegin(*state);
    state->timestampCount++;
    state->timestamps.emplace_back(creation, modification);
}

void CLAP_ABI receiverFeature(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    const char *feature) {
    auto *state = receiverState(receiver);
    assert(state != nullptr);
    noteCallbackAfterBegin(*state);
    state->featureCount++;
    state->features.emplace_back(feature ? feature : "");
}

clap_preset_discovery_metadata_receiver_t makeReceiver(ReceiverState &state) {
    clap_preset_discovery_metadata_receiver_t receiver{};
    receiver.receiver_data = &state;
    receiver.on_error = receiverError;
    receiver.begin_preset = receiverBegin;
    receiver.add_plugin_id = receiverPluginId;
    receiver.set_flags = receiverFlags;
    receiver.add_creator = receiverCreator;
    receiver.set_description = receiverDescription;
    receiver.set_timestamps = receiverTimestamps;
    receiver.add_feature = receiverFeature;
    return receiver;
}

struct IndexerState {
    int filetypeCount = 0;
    int locationCount = 0;
    std::vector<std::uint32_t> locationKinds;
    std::vector<std::string> locations;
};

IndexerState *indexerState(const clap_preset_discovery_indexer_t *indexer) noexcept {
    return indexer ? static_cast<IndexerState *>(indexer->indexer_data) : nullptr;
}

bool CLAP_ABI declareFiletype(const clap_preset_discovery_indexer_t *indexer,
                              const clap_preset_discovery_filetype_t *filetype) {
    auto *state = indexerState(indexer);
    assert(state != nullptr);
    assert(filetype != nullptr);
    assert(std::string_view{filetype->file_extension} == "wvpreset");
    state->filetypeCount++;
    return true;
}

bool CLAP_ABI declareLocation(const clap_preset_discovery_indexer_t *indexer,
                              const clap_preset_discovery_location_t *location) {
    auto *state = indexerState(indexer);
    assert(state != nullptr);
    assert(location != nullptr);
    state->locationCount++;
    state->locationKinds.push_back(location->kind);
    state->locations.emplace_back(location->location ? location->location : "");
    return true;
}

clap_preset_discovery_indexer_t makeIndexer(IndexerState &state) {
    clap_preset_discovery_indexer_t indexer{};
    indexer.clap_version = CLAP_VERSION;
    indexer.name = "#91 test indexer";
    indexer.indexer_data = &state;
    indexer.declare_filetype = declareFiletype;
    indexer.declare_location = declareLocation;
    return indexer;
}

class FakeUserStorage final : public presets::PresetUserStorage {
public:
    std::string target = "com.webview-gui.example.gain";
    std::optional<std::string> root = std::string{kUserRoot};
    presets::PresetStorageListResult listing{};
    mutable int listCalls = 0;

    presets::PresetStorageKind kind() const noexcept override {
        return root ? presets::PresetStorageKind::Native
                    : presets::PresetStorageKind::Unavailable;
    }

    std::string_view targetPluginId() const noexcept override { return target; }

    std::optional<std::string> nativeFileRoot() const override { return root; }

    presets::PresetStorageListResult list() const override {
        listCalls++;
        return listing;
    }

    presets::PresetStorageLoadResult load(std::string_view) const override {
        return {{presets::PresetStorageError::Unavailable}, std::nullopt};
    }

    presets::PresetStorageSaveResult saveAs(
        std::string_view,
        const presets::PresetDocument &,
        bool) override {
        return {{presets::PresetStorageError::Unavailable}, {}};
    }

    presets::PresetStorageStatus remove(std::string_view) override {
        return {presets::PresetStorageError::Unavailable};
    }
};

presets::PresetMetadata makeUserMetadata(std::string target) {
    presets::PresetMetadata metadata;
    metadata.targetPluginId = std::move(target);
    metadata.name = "User One";
    metadata.creator = "User";
    metadata.description = "User metadata fixture";
    metadata.tags = {"user", "lead"};
    metadata.features = {"instrument", "synthesizer"};
    return metadata;
}

std::unique_ptr<presets::PresetCatalog> makeFactoryOnlyCatalog() noexcept {
    auto storage = std::make_unique<presets::UnavailablePresetUserStorage>(
        std::string{presets::kGainFactoryTargetPluginId});
    return presets::makeProductionPresetCatalog(
        presets::gainFactoryPresetCatalog(),
        presets::kGainFactoryTargetPluginId,
        std::move(storage));
}

std::unique_ptr<presets::PresetCatalog> makeUserCatalog(
    presets::PresetMetadata metadata,
    bool diagnostic = false) noexcept {
    auto storage = std::make_unique<FakeUserStorage>();
    if (diagnostic) {
        presets::PresetStorageStatus status;
        status.error = presets::PresetStorageError::ParseFailed;
        status.codecError = presets::PresetCodecError::MalformedInput;
        status.diagnosticPath = std::string{kBrokenFile};
        storage->listing.diagnostics.push_back(std::move(status));
    } else {
        storage->listing.entries.push_back({"user-one.wvpreset", std::move(metadata)});
    }
    return presets::makeProductionPresetCatalog(
        presets::gainFactoryPresetCatalog(),
        presets::kGainFactoryTargetPluginId,
        std::move(storage));
}

struct FactoryTag {
    inline static constexpr const char *providerId = "com.webview-gui.test.metadata.factory";
    inline static constexpr const char *providerName = "#91 factory metadata";
    inline static constexpr const char *vendor = "webview-gui";
    inline static constexpr const char *targetPluginId = "com.webview-gui.example.gain";

    static std::unique_ptr<presets::PresetCatalog> createPresetCatalog() noexcept {
        return makeFactoryOnlyCatalog();
    }
};

struct UserTag {
    inline static constexpr const char *providerId = "com.webview-gui.test.metadata.user";
    inline static constexpr const char *providerName = "#91 user metadata";
    inline static constexpr const char *vendor = "webview-gui";
    inline static constexpr const char *targetPluginId = "com.webview-gui.example.gain";

    static std::unique_ptr<presets::PresetCatalog> createPresetCatalog() noexcept {
        auto metadata = makeUserMetadata(targetPluginId);
        metadata.creationTimestamp = 123;
        metadata.modificationTimestamp = 456;
        return makeUserCatalog(std::move(metadata));
    }
};

const clap_preset_discovery_provider_t *createAndInitProvider(
    const clap_preset_discovery_factory_t *factory,
    const char *providerId,
    clap_preset_discovery_indexer_t &indexer) {
    assert(factory != nullptr);
    const auto *provider = factory->create(factory, &indexer, providerId);
    assert(provider != nullptr);
    assert(provider->init(provider));
    return provider;
}

void testFactoryMetadataThroughClapProvider() {
    IndexerState indexerStateValue;
    auto indexer = makeIndexer(indexerStateValue);
    const auto *factory = presets::presetDiscoveryFactory<FactoryTag>();
    const auto *provider = createAndInitProvider(factory, FactoryTag::providerId, indexer);

    assert(indexerStateValue.filetypeCount == 1);
    assert(indexerStateValue.locationCount == 1);
    assert(indexerStateValue.locationKinds[0] == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN);
    assert(indexerStateValue.locations[0].empty());

    ReceiverState state;
    auto receiver = makeReceiver(state);
    assert(provider->get_metadata(provider,
                                  CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                  nullptr,
                                  &receiver));

    assert(state.errorCount == 0);
    assert(state.beginCount == 3);
    assert(state.pluginIdCount == 3);
    assert(state.flagsCount == 3);
    assert(state.creatorCount == 3);
    assert(state.descriptionCount == 3);
    assert(state.timestampCount == 0);
    assert(state.names == std::vector<std::string>({"Unity", "-6 dB Trim", "+6 dB Boost"}));
    assert(state.loadKeys.size() == 3u);
    for (const auto &key : state.loadKeys) {
        assert(key.has_value());
        assert(!key->empty());
    }
    assert(state.loadKeys[0] == std::optional<std::string>{"gain:unity"});
    assert(state.loadKeys[1] == std::optional<std::string>{"gain:trim-minus-6db"});
    assert(state.loadKeys[2] == std::optional<std::string>{"gain:boost-plus-6db"});
    for (std::size_t i = 0; i < state.pluginIds.size(); ++i) {
        assert(state.pluginAbis[i] == "clap");
        assert(state.pluginIds[i] == FactoryTag::targetPluginId);
        assert((state.flags[i] & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT) != 0u);
    }
    assert(std::count(state.features.begin(), state.features.end(), "factory") == 3);
    assert(std::count(state.features.begin(), state.features.end(), "audio-effect") == 3);

    provider->destroy(provider);
}

void testReceiverCancellationIsSuccessWithoutLeakage() {
    IndexerState indexerStateValue;
    auto indexer = makeIndexer(indexerStateValue);
    const auto *factory = presets::presetDiscoveryFactory<FactoryTag>();
    const auto *provider = createAndInitProvider(factory, FactoryTag::providerId, indexer);

    ReceiverState state;
    state.acceptBegin = false;
    auto receiver = makeReceiver(state);
    assert(provider->get_metadata(provider,
                                  CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                  nullptr,
                                  &receiver));
    assert(state.errorCount == 0);
    assert(state.beginCount == 1);
    assert(state.pluginIdCount == 0);
    assert(state.callbackCountAfterRejectedBegin == 0);
    provider->destroy(provider);
}

void testUserFileMetadataAndTimestamps() {
    IndexerState indexerStateValue;
    auto indexer = makeIndexer(indexerStateValue);
    const auto *factory = presets::presetDiscoveryFactory<UserTag>();
    const auto *provider = createAndInitProvider(factory, UserTag::providerId, indexer);

    assert(indexerStateValue.filetypeCount == 1);
    assert(indexerStateValue.locationCount == 2);
    assert(indexerStateValue.locationKinds[0] == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN);
    assert(indexerStateValue.locationKinds[1] == CLAP_PRESET_DISCOVERY_LOCATION_FILE);
    assert(indexerStateValue.locations[1] == kUserRoot);

    ReceiverState state;
    auto receiver = makeReceiver(state);
    const std::string location{kUserFile};
    assert(provider->get_metadata(provider,
                                  CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                  location.c_str(),
                                  &receiver));
    assert(state.errorCount == 0);
    assert(state.beginCount == 1);
    assert(state.names[0] == "User One");
    assert(!state.loadKeys[0].has_value());
    assert(state.pluginAbis == std::vector<std::string>{"clap"});
    assert(state.pluginIds == std::vector<std::string>{UserTag::targetPluginId});
    assert(state.flagsCount == 1);
    assert((state.flags[0] & CLAP_PRESET_DISCOVERY_IS_USER_CONTENT) != 0u);
    assert(state.timestampCount == 1);
    assert(state.timestamps[0].first == 123u);
    assert(state.timestamps[0].second == 456u);
    provider->destroy(provider);
}

void testWrongPluginAndMalformedFailBeforeBegin() {
    {
        auto wrongMetadata = makeUserMetadata("com.webview-gui.example.polysynth");
        auto catalog = makeUserCatalog(std::move(wrongMetadata));
        class Sink final : public presets::PresetMetadataSink {
        public:
            bool beginPreset(std::string_view, std::string_view) noexcept override {
                beginCount++;
                return true;
            }
            void setTargetPlugin(std::string_view) noexcept override {}
            void addCreator(std::string_view) noexcept override {}
            void setDescription(std::string_view) noexcept override {}
            void addFeature(std::string_view) noexcept override {}
            void setFlags(std::uint32_t) noexcept override {}
            void setTimestamps(clap_timestamp, clap_timestamp) noexcept override {}
            int beginCount = 0;
        } sink;
        const auto result = catalog->metadataForFile(kUserFile, sink);
        assert(result.status == presets::PresetResultStatus::Error);
        assert(sink.beginCount == 0);
    }

    {
        auto metadata = makeUserMetadata("com.webview-gui.example.gain");
        auto catalog = makeUserCatalog(std::move(metadata), true);
        class Sink final : public presets::PresetMetadataSink {
        public:
            bool beginPreset(std::string_view, std::string_view) noexcept override {
                beginCount++;
                return true;
            }
            void setTargetPlugin(std::string_view) noexcept override {}
            void addCreator(std::string_view) noexcept override {}
            void setDescription(std::string_view) noexcept override {}
            void addFeature(std::string_view) noexcept override {}
            void setFlags(std::uint32_t) noexcept override {}
            void setTimestamps(clap_timestamp, clap_timestamp) noexcept override {}
            int beginCount = 0;
        } sink;
        const auto result = catalog->metadataForFile(kBrokenFile, sink);
        assert(result.status == presets::PresetResultStatus::Error);
        assert(sink.beginCount == 0);
    }
}

void testMissingTimestampsAreNotManufactured() {
    auto metadata = makeUserMetadata("com.webview-gui.example.gain");
    auto catalog = makeUserCatalog(std::move(metadata));

    class Sink final : public presets::PresetMetadataSink {
    public:
        bool beginPreset(std::string_view, std::string_view) noexcept override { return true; }
        void setTargetPlugin(std::string_view) noexcept override {}
        void addCreator(std::string_view) noexcept override {}
        void setDescription(std::string_view) noexcept override {}
        void addFeature(std::string_view) noexcept override {}
        void setFlags(std::uint32_t) noexcept override {}
        void setTimestamps(clap_timestamp, clap_timestamp) noexcept override { timestampCount++; }
        int timestampCount = 0;
    } sink;

    const auto result = catalog->metadataForFile(kUserFile, sink);
    assert(result.succeeded());
    assert(sink.timestampCount == 0);
}

} // namespace

int main() {
    testFactoryMetadataThroughClapProvider();
    testReceiverCancellationIsSuccessWithoutLeakage();
    testUserFileMetadataAndTimestamps();
    testWrongPluginAndMalformedFailBeforeBegin();
    testMissingTimestampsAreNotManufactured();
    return 0;
}
