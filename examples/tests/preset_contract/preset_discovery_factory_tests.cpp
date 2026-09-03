#include "preset_discovery_factory.h"
#include "../gain/gain_preset_discovery.h"
#include "../polysynth/polysynth_preset_discovery.h"

#include <clap/factory/preset-discovery.h>

#include <atomic>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>

namespace presets = webview_gui::examples::presets;

namespace {

struct FakeIndexerState {
    std::atomic<unsigned> filetypes{0};
    std::atomic<unsigned> locations{0};
    std::atomic<unsigned> soundpacks{0};
};

bool CLAP_ABI declareFiletype(const clap_preset_discovery_indexer_t *indexer,
                              const clap_preset_discovery_filetype_t *) {
    auto *state = static_cast<FakeIndexerState *>(indexer->indexer_data);
    ++state->filetypes;
    return true;
}

bool CLAP_ABI declareLocation(const clap_preset_discovery_indexer_t *indexer,
                              const clap_preset_discovery_location_t *) {
    auto *state = static_cast<FakeIndexerState *>(indexer->indexer_data);
    ++state->locations;
    return true;
}

bool CLAP_ABI declareSoundpack(const clap_preset_discovery_indexer_t *indexer,
                               const clap_preset_discovery_soundpack_t *) {
    auto *state = static_cast<FakeIndexerState *>(indexer->indexer_data);
    ++state->soundpacks;
    return true;
}

const void *CLAP_ABI indexerGetExtension(const clap_preset_discovery_indexer_t *,
                                         const char *) {
    return nullptr;
}

clap_preset_discovery_indexer_t makeIndexer(FakeIndexerState &state) {
    clap_preset_discovery_indexer_t indexer{};
    indexer.clap_version = CLAP_VERSION;
    indexer.name = "#89 fake indexer";
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

void verifyFactoryContract(const clap_preset_discovery_factory_t *factory,
                           const char *expectedProviderId,
                           FakeIndexerState &indexerState,
                           clap_preset_discovery_indexer_t &indexer) {
    assert(factory != nullptr);
    assert(factory->count != nullptr);
    assert(factory->get_descriptor != nullptr);
    assert(factory->create != nullptr);
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
    assert(provider->provider_data != nullptr);
    assert(provider->init != nullptr);
    assert(provider->destroy != nullptr);
    assert(provider->get_metadata != nullptr);
    assert(provider->get_extension != nullptr);

    // create() must not call into the indexer, and #89 deliberately owns no
    // declarations yet. #90 will add those calls inside init().
    assert(indexerState.filetypes.load() == 0u);
    assert(indexerState.locations.load() == 0u);
    assert(indexerState.soundpacks.load() == 0u);

    // Fail closed before init without touching catalog/processor/WebView state.
    assert(provider->get_extension(provider, "unknown") == nullptr);
    assert(!provider->get_metadata(provider,
                                   CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                   nullptr,
                                   nullptr));

    assert(provider->init(provider));
    assert(!provider->init(provider));
    assert(indexerState.filetypes.load() == 0u);
    assert(indexerState.locations.load() == 0u);
    assert(indexerState.soundpacks.load() == 0u);
    assert(provider->get_extension(provider, "unknown") == nullptr);
    assert(!provider->get_metadata(provider,
                                   CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                   nullptr,
                                   nullptr));

    provider->destroy(provider);
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

    // The CLAP factory methods are thread-safe. Exercise concurrent create/init/
    // destroy without any shared mutable registry or processor construction.
    std::atomic<bool> concurrentOk{true};
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8u; ++t) {
        threads.emplace_back([&] {
            for (unsigned i = 0; i < 128u; ++i) {
                const auto *descriptor = factory->get_descriptor(factory, 0u);
                if (!descriptor || factory->count(factory) != 1u) {
                    concurrentOk = false;
                    return;
                }
                const auto *provider = factory->create(factory, &indexer, descriptor->id);
                if (!provider || !provider->init(provider)) {
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

    assert(indexerState.filetypes.load() == 0u);
    assert(indexerState.locations.load() == 0u);
    assert(indexerState.soundpacks.load() == 0u);
    return 0;
}
