#pragma once

#include "preset_clap_contract.h"

#include <clap/factory/preset-discovery.h>

#include <cstdint>
#include <cstring>
#include <new>

namespace webview_gui::examples::presets {

// Tag requirements:
//   static constexpr const char *providerId;
//   static constexpr const char *providerName;
//   static constexpr const char *vendor;
//   static constexpr const char *targetPluginId;
//
// The target plug-in ID is deliberately carried by the tag now so #91 can map
// Preset Discovery metadata without introducing a registry or processor object.
template <typename Tag>
class PresetDiscoveryFactoryImpl {
    struct ProviderState {
        const clap_preset_discovery_indexer_t *indexer = nullptr;
        bool initialized = false;
        clap_preset_discovery_provider_t provider{};

        explicit ProviderState(const clap_preset_discovery_indexer_t *indexerIn) noexcept
            : indexer(indexerIn) {
            provider.desc = &providerDescriptor;
            provider.provider_data = this;
            provider.init = providerInit;
            provider.destroy = providerDestroy;
            provider.get_metadata = providerGetMetadata;
            provider.get_extension = providerGetExtension;
        }
    };

    static ProviderState *stateFrom(const clap_preset_discovery_provider_t *provider) noexcept {
        if (!provider || !provider->provider_data)
            return nullptr;
        auto *state = static_cast<ProviderState *>(provider->provider_data);
        return &state->provider == provider ? state : nullptr;
    }

    static bool CLAP_ABI providerInit(const clap_preset_discovery_provider_t *provider) noexcept {
        auto *state = stateFrom(provider);
        if (!state || !state->indexer || state->initialized)
            return false;

        // #89 owns lifecycle only. Filetypes and locations are declared by #90,
        // from init(), never from factory create().
        state->initialized = true;
        return true;
    }

    static void CLAP_ABI providerDestroy(const clap_preset_discovery_provider_t *provider) noexcept {
        auto *state = stateFrom(provider);
        if (!state)
            return;
        state->provider.provider_data = nullptr;
        delete state;
    }

    static bool CLAP_ABI providerGetMetadata(
        const clap_preset_discovery_provider_t *provider,
        std::uint32_t,
        const char *,
        const clap_preset_discovery_metadata_receiver_t *) noexcept {
        auto *state = stateFrom(provider);
        if (!state || !state->initialized)
            return false;
        // Metadata adaptation belongs to #91. Until then the provider fails
        // closed without instantiating a processor, WebView or storage engine.
        return false;
    }

    static const void *CLAP_ABI providerGetExtension(
        const clap_preset_discovery_provider_t *provider,
        const char *) noexcept {
        auto *state = stateFrom(provider);
        return state && state->initialized ? nullptr : nullptr;
    }

    static std::uint32_t CLAP_ABI factoryCount(
        const clap_preset_discovery_factory_t *factoryIn) noexcept {
        return factoryIn == &factory ? 1u : 0u;
    }

    static const clap_preset_discovery_provider_descriptor_t *CLAP_ABI factoryGetDescriptor(
        const clap_preset_discovery_factory_t *factoryIn,
        std::uint32_t index) noexcept {
        if (factoryIn != &factory || index != 0u)
            return nullptr;
        return &providerDescriptor;
    }

    static const clap_preset_discovery_provider_t *CLAP_ABI factoryCreate(
        const clap_preset_discovery_factory_t *factoryIn,
        const clap_preset_discovery_indexer_t *indexer,
        const char *providerId) noexcept {
        if (factoryIn != &factory || !indexer || !providerId ||
            std::strcmp(providerId, Tag::providerId) != 0)
            return nullptr;

        auto *state = new (std::nothrow) ProviderState(indexer);
        return state ? &state->provider : nullptr;
    }

public:
    inline static const clap_preset_discovery_provider_descriptor_t providerDescriptor{
        CLAP_VERSION,
        Tag::providerId,
        Tag::providerName,
        Tag::vendor,
    };

    inline static const clap_preset_discovery_factory_t factory{
        factoryCount,
        factoryGetDescriptor,
        factoryCreate,
    };
};

template <typename Tag>
const clap_preset_discovery_factory_t *presetDiscoveryFactory() noexcept {
    return &PresetDiscoveryFactoryImpl<Tag>::factory;
}

template <typename Tag>
const void *presetDiscoveryEntryFactory(const char *factoryId) noexcept {
    if (!factoryId || classifyPresetClapId(factoryId) != ClapPresetSurface::EntryFactory)
        return nullptr;
    return presetDiscoveryFactory<Tag>();
}

} // namespace webview_gui::examples::presets
