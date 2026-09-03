#include "preset_discovery_factory.h"
#include "gain/gain_preset_discovery.h"
#include "polysynth/polysynth_preset_discovery.h"

#include <clap/factory/preset-discovery.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace presets = webview_gui::examples::presets;

namespace {

struct DeclaredFiletype {
    std::string name;
    std::string description;
    std::string extension;
};

struct DeclaredLocation {
    std::uint32_t flags = 0;
    std::string name;
    std::uint32_t kind = 0;
    bool locationWasNull = false;
    std::string location;
};

struct FakeIndexerState {
    std::vector<DeclaredFiletype> filetypes;
    std::vector<DeclaredLocation> locations;
    unsigned soundpacks = 0;
    bool acceptFiletypes = true;
    bool acceptLocations = true;
};

bool CLAP_ABI declareFiletype(const clap_preset_discovery_indexer_t *indexer,
                              const clap_preset_discovery_filetype_t *filetype) {
    auto *state = static_cast<FakeIndexerState *>(indexer->indexer_data);
    if (!state->acceptFiletypes || !filetype || !filetype->name || !filetype->file_extension)
        return false;
    state->filetypes.push_back({filetype->name,
                                filetype->description ? filetype->description : "",
                                filetype->file_extension});
    return true;
}

bool CLAP_ABI declareLocation(const clap_preset_discovery_indexer_t *indexer,
                              const clap_preset_discovery_location_t *location) {
    auto *state = static_cast<FakeIndexerState *>(indexer->indexer_data);
    if (!state->acceptLocations || !location || !location->name)
        return false;
    state->locations.push_back({location->flags,
                                location->name,
                                location->kind,
                                location->location == nullptr,
                                location->location ? location->location : ""});
    return true;
}

bool CLAP_ABI declareSoundpack(const clap_preset_discovery_indexer_t *indexer,
                               const clap_preset_discovery_soundpack_t *) {
    auto *state = static_cast<FakeIndexerState *>(indexer->indexer_data);
    ++state->soundpacks;
    return true;
}

const void *CLAP_ABI indexerGetExtension(const clap_preset_discovery_indexer_t *, const char *) {
    return nullptr;
}

clap_preset_discovery_indexer_t makeIndexer(FakeIndexerState &state) {
    clap_preset_discovery_indexer_t indexer{};
    indexer.clap_version = CLAP_VERSION;
    indexer.name = "#90 fake indexer";
    indexer.vendor = "webview-gui tests";
    indexer.url = "https://example.invalid";
    indexer.version = "1";
    indexer.indexer_data = &state;
    indexer.declare_filetype = declareFiletype;
    indexer.declare_location = declareLocation;
    indexer.declare_soundpack = declareSoundpack;
    indexer.get_extension = indexerGetExtension;
    return indexer;
}

struct FakeProviderTag {
    static constexpr const char *providerId = "com.webview-gui.test.preset-provider";
    static constexpr const char *providerName = "webview-gui test preset provider";
    static constexpr const char *vendor = "webview-gui";
    static constexpr const char *targetPluginId = "com.webview-gui.test.plugin";
};

struct NativeUserProviderTag {
    static constexpr const char *providerId = "com.webview-gui.test.native-user-provider";
    static constexpr const char *providerName = "webview-gui native user preset provider";
    static constexpr const char *vendor = "webview-gui";
    static constexpr const char *targetPluginId = "com.webview-gui.test.plugin";
    static bool nativeUserPresetFilesAvailable() noexcept { return true; }
    static const char *nativeUserPresetRoot() noexcept { return "/tmp/webview-gui-user-presets"; }
};

struct BrowserOnlyProviderTag {
    static constexpr const char *providerId = "com.webview-gui.test.browser-only-provider";
    static constexpr const char *providerName = "webview-gui browser-only preset provider";
    static constexpr const char *vendor = "webview-gui";
    static constexpr const char *targetPluginId = "com.webview-gui.test.plugin";
    static bool nativeUserPresetFilesAvailable() noexcept { return false; }
    static const char *nativeUserPresetRoot() noexcept { return "browser-storage://presets"; }
};

struct InvalidNativeUserProviderTag {
    static constexpr const char *providerId = "com.webview-gui.test.invalid-user-provider";
    static constexpr const char *providerName = "webview-gui invalid user preset provider";
    static constexpr const char *vendor = "webview-gui";
    static constexpr const char *targetPluginId = "com.webview-gui.test.plugin";
    static bool nativeUserPresetFilesAvailable() noexcept { return true; }
    static const char *nativeUserPresetRoot() noexcept { return ""; }
};

void verifyCanonicalFiletype(const FakeIndexerState &state) {
    assert(state.filetypes.size() == 1u);
    assert(state.filetypes[0].name == "webview-gui preset");
    assert(state.filetypes[0].description == "webview-gui versioned preset");
    assert(state.filetypes[0].extension == "wvpreset");
}

void verifyFactoryLocation(const FakeIndexerState &state) {
    assert(!state.locations.empty());
    const auto &factoryLocation = state.locations[0];
    assert(factoryLocation.flags == CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
    assert(factoryLocation.name == "Factory presets");
    assert(factoryLocation.kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN);
    assert(factoryLocation.locationWasNull);
    assert(factoryLocation.location.empty());
}

void verifyFactoryContract(const clap_preset_discovery_factory_t *factory,
                           const char *expectedProviderId,
                           FakeIndexerState &indexerState,
                           clap_preset_discovery_indexer_t &indexer) {
    assert(factory != nullptr);
    assert(factory->count(factory) == 1u);
    const auto *descriptor = factory->get_descriptor(factory, 0u);
    assert(descriptor != nullptr);
    assert(descriptor->clap_version.major == CLAP_VERSION.major);
    assert(std::strcmp(descriptor->id, expectedProviderId) == 0);
    assert(factory->get_descriptor(factory, 1u) == nullptr);
    assert(factory->create(factory, nullptr, expectedProviderId) == nullptr);
    assert(factory->create(factory, &indexer, nullptr) == nullptr);
    assert(factory->create(factory, &indexer, "unknown.provider") == nullptr);

    const auto *provider = factory->create(factory, &indexer, expectedProviderId);
    assert(provider != nullptr);
    assert(provider->desc == descriptor);
    assert(indexerState.filetypes.empty());
    assert(indexerState.locations.empty());
    assert(indexerState.soundpacks == 0u);
    assert(provider->get_extension(provider, "unknown") == nullptr);
    assert(!provider->get_metadata(provider,
                                   CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                   nullptr,
                                   nullptr));

    assert(provider->init(provider));
    verifyCanonicalFiletype(indexerState);
    verifyFactoryLocation(indexerState);
    assert(indexerState.locations.size() == 1u);
    assert(indexerState.soundpacks == 0u);
    assert(!provider->init(provider));
    assert(indexerState.filetypes.size() == 1u);
    assert(indexerState.locations.size() == 1u);
    provider->destroy(provider);
}

template <typename Tag>
const clap_preset_discovery_provider_t *createProvider(FakeIndexerState &state,
                                                       clap_preset_discovery_indexer_t &indexer) {
    const auto *factory = presets::presetDiscoveryFactory<Tag>();
    const auto *descriptor = factory->get_descriptor(factory, 0u);
    assert(descriptor != nullptr);
    const auto *provider = factory->create(factory, &indexer, descriptor->id);
    assert(provider != nullptr);
    return provider;
}

} // namespace

int main() {
    FakeIndexerState indexerState;
    auto indexer = makeIndexer(indexerState);
    const auto *factory = presets::presetDiscoveryFactory<FakeProviderTag>();
    verifyFactoryContract(factory, FakeProviderTag::providerId, indexerState, indexer);

    const auto *gainFactory = webview_gui::examples::gain::gainPresetDiscoveryFactory();
    assert(gainFactory != nullptr);
    assert(std::strcmp(gainFactory->get_descriptor(gainFactory, 0u)->id,
                       "com.webview-gui.example.gain.presets") == 0);
    const auto *polyFactory = webview_gui::examples::polysynth::polysynthPresetDiscoveryFactory();
    assert(polyFactory != nullptr);
    assert(std::strcmp(polyFactory->get_descriptor(polyFactory, 0u)->id,
                       "com.webview-gui.example.polysynth.presets") == 0);

    FakeIndexerState nativeState;
    auto nativeIndexer = makeIndexer(nativeState);
    const auto *nativeProvider = createProvider<NativeUserProviderTag>(nativeState, nativeIndexer);
    assert(nativeProvider->init(nativeProvider));
    verifyCanonicalFiletype(nativeState);
    verifyFactoryLocation(nativeState);
    assert(nativeState.locations.size() == 2u);
    const auto &userLocation = nativeState.locations[1];
    assert(userLocation.flags == CLAP_PRESET_DISCOVERY_IS_USER_CONTENT);
    assert(userLocation.name == "User presets");
    assert(userLocation.kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE);
    assert(!userLocation.locationWasNull);
    assert(userLocation.location == NativeUserProviderTag::nativeUserPresetRoot());
    nativeProvider->destroy(nativeProvider);

    FakeIndexerState browserState;
    auto browserIndexer = makeIndexer(browserState);
    const auto *browserProvider = createProvider<BrowserOnlyProviderTag>(browserState, browserIndexer);
    assert(browserProvider->init(browserProvider));
    verifyCanonicalFiletype(browserState);
    verifyFactoryLocation(browserState);
    assert(browserState.locations.size() == 1u);
    browserProvider->destroy(browserProvider);

    FakeIndexerState invalidState;
    auto invalidIndexer = makeIndexer(invalidState);
    const auto *invalidProvider = createProvider<InvalidNativeUserProviderTag>(invalidState, invalidIndexer);
    assert(!invalidProvider->init(invalidProvider));
    assert(invalidState.filetypes.empty());
    assert(invalidState.locations.empty());
    assert(!invalidProvider->init(invalidProvider));
    invalidProvider->destroy(invalidProvider);

    FakeIndexerState rejectingState;
    rejectingState.acceptFiletypes = false;
    auto rejectingIndexer = makeIndexer(rejectingState);
    const auto *rejectingProvider = createProvider<FakeProviderTag>(rejectingState, rejectingIndexer);
    assert(!rejectingProvider->init(rejectingProvider));
    assert(rejectingState.filetypes.empty());
    assert(rejectingState.locations.empty());
    assert(!rejectingProvider->init(rejectingProvider));
    assert(rejectingState.filetypes.empty());
    rejectingProvider->destroy(rejectingProvider);

    FakeIndexerState locationRejectState;
    locationRejectState.acceptLocations = false;
    auto locationRejectIndexer = makeIndexer(locationRejectState);
    const auto *locationRejectProvider = createProvider<FakeProviderTag>(locationRejectState,
                                                                         locationRejectIndexer);
    assert(!locationRejectProvider->init(locationRejectProvider));
    assert(locationRejectState.filetypes.size() == 1u);
    assert(locationRejectState.locations.empty());
    assert(!locationRejectProvider->init(locationRejectProvider));
    assert(locationRejectState.filetypes.size() == 1u);
    locationRejectProvider->destroy(locationRejectProvider);

    std::atomic<bool> concurrentOk{true};
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8u; ++t) {
        threads.emplace_back([&] {
            FakeIndexerState threadIndexerState;
            auto threadIndexer = makeIndexer(threadIndexerState);
            for (unsigned i = 0; i < 128u; ++i) {
                const auto *descriptor = factory->get_descriptor(factory, 0u);
                if (!descriptor || factory->count(factory) != 1u) {
                    concurrentOk = false;
                    return;
                }
                const auto *provider = factory->create(factory, &threadIndexer, descriptor->id);
                if (!provider || !provider->init(provider) ||
                    threadIndexerState.filetypes.size() != i + 1u ||
                    threadIndexerState.locations.size() != i + 1u) {
                    concurrentOk = false;
                    if (provider)
                        provider->destroy(provider);
                    return;
                }
                provider->destroy(provider);
            }
        });
    }
    for (auto &thread : threads)
        thread.join();
    assert(concurrentOk.load());
    assert(indexerState.filetypes.size() == 1u);
    assert(indexerState.locations.size() == 1u);
    assert(indexerState.soundpacks == 0u);
    return 0;
}
