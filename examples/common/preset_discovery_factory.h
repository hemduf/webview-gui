#pragma once

#include "preset_clap_contract.h"

#include <clap/factory/preset-discovery.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

namespace webview_gui::examples::presets {

namespace detail {

// #90 owns only the CLAP declaration policy. Production user-root discovery
// remains owned by #36. A Tag may expose the two functions below when a real
// filesystem-backed #36 storage root exists; otherwise user FILE discovery is
// unavailable. Test tags use this seam before #36 lands.
template <typename Tag, typename = void>
struct NativeUserPresetLocation {
    static bool available() noexcept { return false; }
    static const char *root() noexcept { return nullptr; }
};

template <typename Tag>
struct NativeUserPresetLocation<
    Tag,
    std::void_t<decltype(Tag::nativeUserPresetFilesAvailable()),
                decltype(Tag::nativeUserPresetRoot())>> {
    static bool available() noexcept {
#if defined(__wasi__)
        // WCLAP/WASI storage is not a native OS FILE location. Factory content
        // remains discoverable through the PLUGIN container below.
        return false;
#else
        return Tag::nativeUserPresetFilesAvailable();
#endif
    }

    static const char *root() noexcept { return Tag::nativeUserPresetRoot(); }
};

} // namespace detail

// Tag requirements:
//   static constexpr const char *providerId;
//   static constexpr const char *providerName;
//   static constexpr const char *vendor;
//   static constexpr const char *targetPluginId;
// Optional #36 integration seam for a genuine native filesystem user root:
//   static bool nativeUserPresetFilesAvailable() noexcept;
//   static const char *nativeUserPresetRoot() noexcept;
//
// The target plug-in ID is deliberately carried by the tag now so #91 can map
// Preset Discovery metadata without introducing a registry or processor object.
template <typename Tag>
class PresetDiscoveryFactoryImpl {
    struct ProviderState {
        const clap_preset_discovery_indexer_t *indexer = nullptr;
        bool initAttempted = false;
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
        if (!state || !state->indexer || state->initAttempted)
            return false;

        state->initAttempted = true;
        const auto *indexer = state->indexer;
        if (!indexer->declare_filetype || !indexer->declare_location)
            return false;

        const bool userFilesAvailable = detail::NativeUserPresetLocation<Tag>::available();
        const char *userRoot = userFilesAvailable
                                   ? detail::NativeUserPresetLocation<Tag>::root()
                                   : nullptr;
        if (userFilesAvailable && (!userRoot || userRoot[0] == '\0'))
            return false;

        // The file extension is the #37 CLAP-facing contract seam. #36 remains
        // authoritative for serialization/schema/storage and may replace this
        // literal if its final production extension changes before integration.
        if (!indexer->declare_filetype(indexer, &presetFiletype))
            return false;
        if (!indexer->declare_location(indexer, &factoryLocation))
            return false;

        if (userFilesAvailable) {
            const clap_preset_discovery_location_t userLocation{
                CLAP_PRESET_DISCOVERY_IS_USER_CONTENT,
                "User presets",
                CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                userRoot,
            };
            if (!indexer->declare_location(indexer, &userLocation))
                return false;
        }

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
        const clap_preset_discovery_provider_t *,
        const char *) noexcept {
        // #89 exposes no provider extension. Returning null is valid both for a
        // normal post-init query and as a defensive response to an invalid early
        // query, even though CLAP forbids hosts from calling this before init().
        return nullptr;
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
    inline static constexpr clap_preset_discovery_filetype_t presetFiletype{
        "webview-gui preset",
        "webview-gui versioned preset",
        "wvpreset",
    };

    inline static constexpr clap_preset_discovery_location_t factoryLocation{
        CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
        "Factory presets",
        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        nullptr,
    };

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
