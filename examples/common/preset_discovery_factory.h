#pragma once

#include "preset_clap_contract.h"

#include <clap/factory/preset-discovery.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>

namespace webview_gui::examples::presets {

namespace detail {

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
    static_assert(noexcept(Tag::nativeUserPresetFilesAvailable()),
                  "#36 native user availability seam must be noexcept");
    static_assert(noexcept(Tag::nativeUserPresetRoot()),
                  "#36 native user root seam must be noexcept");
    static_assert(std::is_convertible_v<decltype(Tag::nativeUserPresetFilesAvailable()), bool>,
                  "#36 native user availability seam must return a bool-compatible value");
    static_assert(std::is_convertible_v<decltype(Tag::nativeUserPresetRoot()), const char *>,
                  "#36 native user root seam must return a C string path");

    static bool available() noexcept {
#if defined(__wasi__)
        return false;
#else
        return static_cast<bool>(Tag::nativeUserPresetFilesAvailable());
#endif
    }

    static const char *root() noexcept { return Tag::nativeUserPresetRoot(); }
};

template <typename Tag, typename = void>
struct ProductionCatalogFactory {
    inline static constexpr bool available = false;
    static std::unique_ptr<PresetCatalog> create() noexcept { return {}; }
};

template <typename Tag>
struct ProductionCatalogFactory<Tag,
                                std::void_t<decltype(Tag::createPresetCatalog())>> {
    inline static constexpr bool available = true;
    static_assert(noexcept(Tag::createPresetCatalog()),
                  "production preset catalog factory must be noexcept");
    static_assert(std::is_same_v<decltype(Tag::createPresetCatalog()),
                                 std::unique_ptr<PresetCatalog>>,
                  "production preset catalog factory must return unique_ptr<PresetCatalog>");

    static std::unique_ptr<PresetCatalog> create() noexcept {
        return Tag::createPresetCatalog();
    }
};

class ClapMetadataSink final : public PresetMetadataSink {
public:
    explicit ClapMetadataSink(
        const clap_preset_discovery_metadata_receiver_t *receiver) noexcept
        : receiver_(receiver) {}

    bool beginPreset(std::string_view name, std::string_view loadKey) noexcept override {
        if (failed_ || cancelled_ || !receiver_ || !receiver_->begin_preset)
            return false;
        try {
            std::string nameText{name};
            std::string loadKeyText{loadKey};
            const char *loadKeyPtr = loadKey.empty() ? nullptr : loadKeyText.c_str();
            const bool accepted = receiver_->begin_preset(receiver_,
                                                          nameText.c_str(),
                                                          loadKeyPtr);
            if (!accepted)
                cancelled_ = true;
            return accepted;
        } catch (...) {
            failed_ = true;
            return false;
        }
    }

    void setTargetPlugin(std::string_view pluginId) noexcept override {
        if (failed_ || cancelled_ || !receiver_ || !receiver_->add_plugin_id)
            return;
        try {
            std::string idText{pluginId};
            const clap_universal_plugin_id_t universalId{"clap", idText.c_str()};
            receiver_->add_plugin_id(receiver_, &universalId);
        } catch (...) {
            failed_ = true;
        }
    }

    void setFlags(std::uint32_t flags) noexcept override {
        if (failed_ || cancelled_ || !receiver_ || !receiver_->set_flags)
            return;
        try {
            receiver_->set_flags(receiver_, flags);
        } catch (...) {
            failed_ = true;
        }
    }

    void addCreator(std::string_view creator) noexcept override {
        callString(receiver_ ? receiver_->add_creator : nullptr, creator);
    }

    void setDescription(std::string_view description) noexcept override {
        callString(receiver_ ? receiver_->set_description : nullptr, description);
    }

    void addFeature(std::string_view feature) noexcept override {
        callString(receiver_ ? receiver_->add_feature : nullptr, feature);
    }

    void setTimestamps(clap_timestamp creation,
                       clap_timestamp modification) noexcept override {
        if (failed_ || cancelled_ || !receiver_ || !receiver_->set_timestamps)
            return;
        try {
            receiver_->set_timestamps(receiver_, creation, modification);
        } catch (...) {
            failed_ = true;
        }
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }

private:
    using StringCallback = void(CLAP_ABI *)(
        const clap_preset_discovery_metadata_receiver_t *, const char *);

    void callString(StringCallback callback, std::string_view text) noexcept {
        if (failed_ || cancelled_ || !receiver_ || !callback)
            return;
        try {
            std::string copy{text};
            callback(receiver_, copy.c_str());
        } catch (...) {
            failed_ = true;
        }
    }

    const clap_preset_discovery_metadata_receiver_t *receiver_ = nullptr;
    bool failed_ = false;
    bool cancelled_ = false;
};

inline void notifyMetadataError(
    const clap_preset_discovery_metadata_receiver_t *receiver,
    const PresetResult &result) noexcept {
    if (!receiver || !receiver->on_error)
        return;
    try {
        std::string message = result.message.empty()
                                  ? std::string{"preset metadata extraction failed"}
                                  : std::string{result.message};
        receiver->on_error(receiver, result.osError, message.c_str());
    } catch (...) {
    }
}

} // namespace detail

template <typename Tag>
class PresetDiscoveryFactoryImpl {
    struct ProviderState {
        const clap_preset_discovery_indexer_t *indexer = nullptr;
        bool initAttempted = false;
        bool initialized = false;
        std::unique_ptr<PresetCatalog> catalog;
        std::string nativeUserRoot;
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

        if constexpr (detail::ProductionCatalogFactory<Tag>::available) {
            state->catalog = detail::ProductionCatalogFactory<Tag>::create();
            if (!state->catalog || state->catalog->fileExtension() != presetFiletype.file_extension)
                return false;
        }

        bool userFilesAvailable = false;
        const char *userRoot = nullptr;
        if (state->catalog) {
            std::string_view location;
            if (state->catalog->nativeUserLocation(location)) {
#if !defined(__wasi__)
                if (location.empty())
                    return false;
                try {
                    state->nativeUserRoot.assign(location.data(), location.size());
                } catch (...) {
                    return false;
                }
                userFilesAvailable = true;
                userRoot = state->nativeUserRoot.c_str();
#endif
            }
        } else {
            userFilesAvailable = detail::NativeUserPresetLocation<Tag>::available();
            userRoot = userFilesAvailable ? detail::NativeUserPresetLocation<Tag>::root() : nullptr;
        }

        if (userFilesAvailable && (!userRoot || userRoot[0] == '\0'))
            return false;

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
        std::uint32_t locationKind,
        const char *location,
        const clap_preset_discovery_metadata_receiver_t *receiver) noexcept {
        auto *state = stateFrom(provider);
        if (!state || !state->initialized || !state->catalog || !receiver ||
            !receiver->begin_preset || !receiver->add_plugin_id)
            return false;

        detail::ClapMetadataSink sink{receiver};
        PresetResult result;
        if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
            if (location != nullptr)
                result = PresetResult::error("PLUGIN preset location must be null");
            else
                result = state->catalog->enumerateFactoryMetadata(sink);
        } else if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_FILE) {
            if (!location || location[0] == '\0')
                result = PresetResult::error("FILE preset location is missing");
            else
                result = state->catalog->metadataForFile(location, sink);
        } else {
            result = PresetResult::unsupported("unsupported preset discovery location kind");
        }

        // Adapter faults (allocation/callback exceptions) are not host-requested
        // cancellation. Report them even if the catalog saw beginPreset()==false.
        if (sink.failed()) {
            detail::notifyMetadataError(receiver,
                PresetResult::error("metadata receiver adapter failed"));
            return false;
        }
        if (result.status == PresetResultStatus::Cancelled || sink.cancelled())
            return true;
        if (!result.succeeded()) {
            detail::notifyMetadataError(receiver, result);
            return false;
        }
        return true;
    }

    static const void *CLAP_ABI providerGetExtension(
        const clap_preset_discovery_provider_t *,
        const char *) noexcept {
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
