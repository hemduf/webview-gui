#pragma once

#include "polysynth_wclap_proxy.h"
#include "../polysynth_preset_state.h"
#include "../../common/preset_browser_runtime.h"
#include "../../common/preset_browser_storage.h"
#include "../../common/preset_clap_contract.h"
#include "../../common/presets/preset_factory_catalog.h"

#include <clap/ext/params.h>
#include <clap/ext/preset-load.h>
#include <clap/ext/state-context.h>
#include <clap/ext/state.h>
#include <clap/factory/preset-discovery.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if !defined(__wasi__)
#include <filesystem>
#endif
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace webview_gui::examples::polysynth::wclap {
namespace preset_browser_detail {

using BaseProxy = detail::PolySynthWclapPluginProxy;

struct PolySynthPresetBrowserProxy;

// The browser proxy contains non-standard-layout controller/storage objects, so
// never recover it with container-of/reinterpret_cast from clap_plugin_t. The
// already-qualified BaseProxy remains standard-layout; a derived state object
// carries the explicit owner back-pointer without touching the audio-thread
// event representation or requiring a global registry/lock.
struct BrowserProxyState final : detail::ProxyState {
    explicit BrowserProxyState(const clap_host_t *host) noexcept : detail::ProxyState(host) {}
    PolySynthPresetBrowserProxy *owner = nullptr;
};

inline bool isGlobalPersistentValueEvent(const clap_event_header_t *header) noexcept {
    if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
        header->type != CLAP_EVENT_PARAM_VALUE ||
        header->size < sizeof(clap_event_param_value_t))
        return false;
    const auto &event = *reinterpret_cast<const clap_event_param_value_t *>(header);
    return parameterSpecForId(event.param_id) && std::isfinite(event.value) &&
           event.note_id == -1 && event.port_index == -1 &&
           event.channel == -1 && event.key == -1;
}

inline bool containsPersistentValueEvent(const clap_input_events_t *events) noexcept {
    if (!events || !events->size || !events->get)
        return false;
    const auto count = events->size(events);
    for (std::uint32_t index = 0u; index < count; ++index) {
        if (isGlobalPersistentValueEvent(events->get(events, index)))
            return true;
    }
    return false;
}

inline bool isPersistentUiValueMessage(const void *buffer, std::uint32_t size) noexcept {
    if (!buffer || size != 24u)
        return false;
    const auto *bytes = static_cast<const std::uint8_t *>(buffer);
    return bytes[0] == 'W' && bytes[1] == 'V' && bytes[2] == 'P' && bytes[3] == '1' &&
           bytes[4] == 2u;
}

struct PolySynthPresetBrowserProxy {
    BaseProxy base;
    presets::PresetBrowserStorage browserStorage;
    presets::PresetBrowserController browserController;
    presets::PresetBrowserRuntime browserRuntime;
    std::atomic<bool> presetDirtyPending{false};

    PolySynthPresetBrowserProxy(const clap_plugin_t *inner, BrowserProxyState *state) noexcept
        : base(inner, state),
          browserStorage(kPolySynthPluginId),
          browserController(presets::polySynthFactoryPresetCatalog(),
                            std::string{kPolySynthPluginId},
                            browserStorage.get()),
          browserRuntime(browserController) {
        state->owner = this;
        base.plugin.destroy = proxyDestroy;
        base.plugin.process = inner->process ? proxyProcess : nullptr;
        base.plugin.get_extension = proxyGetExtension;
    }

    static PolySynthPresetBrowserProxy *from(const clap_plugin_t *outer) noexcept {
        auto *baseProxy = BaseProxy::from(outer);
        if (!baseProxy || !baseProxy->state)
            return nullptr;
        auto *state = static_cast<BrowserProxyState *>(baseProxy->state);
        return state->owner;
    }

    static void CLAP_ABI proxyDestroy(const clap_plugin_t *outer) {
        auto *self = from(outer);
        if (!self)
            return;
        auto *state = static_cast<BrowserProxyState *>(self->base.state);
        const auto *inner = self->base.innerPlugin;
        self->base.state = nullptr;
        self->base.innerPlugin = nullptr;
        self->base.innerParams = nullptr;
        self->base.initialized = false;
        self->base.active = false;
        if (state) {
            state->owner = nullptr;
            if (state->guiCreated) {
                state->uiQueue.closeOpenGestures();
                state->gui.destroy();
            }
            state->guiCreated = false;
        }
        if (inner && inner->destroy)
            inner->destroy(inner);
        delete state;
        delete self;
    }

    static clap_process_status CLAP_ABI proxyProcess(const clap_plugin_t *outer,
                                                      const clap_process_t *process) {
        auto *self = from(outer);
        if (self && process && containsPersistentValueEvent(process->in_events))
            self->presetDirtyPending.store(true, std::memory_order_release);
        return BaseProxy::proxyProcess(outer, process);
    }

    static const void *CLAP_ABI proxyGetExtension(const clap_plugin_t *outer,
                                                   const char *id) {
        auto *self = from(outer);
        if (!self || !id)
            return nullptr;

        if (std::strcmp(id, ::webview_gui::CLAP_EXT_WEBVIEW) == 0)
            return &webviewProxy;

        if (!self->base.initialized)
            return BaseProxy::proxyGetExtension(outer, id);

        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &paramsProxy;

        const auto *innerExtension = self->base.innerPlugin && self->base.innerPlugin->get_extension
                                         ? self->base.innerPlugin->get_extension(self->base.innerPlugin, id)
                                         : nullptr;
        if (!innerExtension)
            return BaseProxy::proxyGetExtension(outer, id);

        if (presets::classifyPresetClapId(id) == presets::ClapPresetSurface::PluginExtension)
            return &presetLoadProxy;
        if (std::strcmp(id, CLAP_EXT_STATE) == 0)
            return &stateProxy;
        if (std::strcmp(id, CLAP_EXT_STATE_CONTEXT) == 0)
            return &stateContextProxy;
        return BaseProxy::proxyGetExtension(outer, id);
    }

    static std::uint32_t CLAP_ABI paramsCount(const clap_plugin_t *outer) {
        return BaseProxy::paramsCount(outer);
    }
    static bool CLAP_ABI paramsGetInfo(const clap_plugin_t *outer,
                                       std::uint32_t index,
                                       clap_param_info_t *info) {
        return BaseProxy::paramsGetInfo(outer, index, info);
    }
    static bool CLAP_ABI paramsGetValue(const clap_plugin_t *outer,
                                        clap_id paramId,
                                        double *value) {
        return BaseProxy::paramsGetValue(outer, paramId, value);
    }
    static bool CLAP_ABI paramsValueToText(const clap_plugin_t *outer,
                                           clap_id paramId,
                                           double value,
                                           char *display,
                                           std::uint32_t size) {
        return BaseProxy::paramsValueToText(outer, paramId, value, display, size);
    }
    static bool CLAP_ABI paramsTextToValue(const clap_plugin_t *outer,
                                           clap_id paramId,
                                           const char *display,
                                           double *value) {
        return BaseProxy::paramsTextToValue(outer, paramId, display, value);
    }
    static void CLAP_ABI paramsFlush(const clap_plugin_t *outer,
                                     const clap_input_events_t *in,
                                     const clap_output_events_t *out) {
        auto *self = from(outer);
        if (self && containsPersistentValueEvent(in))
            self->presetDirtyPending.store(true, std::memory_order_release);
        BaseProxy::paramsFlush(outer, in, out);
    }

    static bool CLAP_ABI stateSave(const clap_plugin_t *outer, const clap_ostream_t *stream) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerState() : nullptr;
        return extension && extension->save && extension->save(self->base.innerPlugin, stream);
    }
    static bool CLAP_ABI stateLoad(const clap_plugin_t *outer, const clap_istream_t *stream) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerState() : nullptr;
        if (!extension || !extension->load || !extension->load(self->base.innerPlugin, stream))
            return false;
        self->clearBrowserAfterStateRestore();
        return true;
    }
    static bool CLAP_ABI stateContextSave(const clap_plugin_t *outer,
                                          const clap_ostream_t *stream,
                                          std::uint32_t contextType) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerStateContext() : nullptr;
        return extension && extension->save &&
               extension->save(self->base.innerPlugin, stream, contextType);
    }
    static bool CLAP_ABI stateContextLoad(const clap_plugin_t *outer,
                                          const clap_istream_t *stream,
                                          std::uint32_t contextType) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerStateContext() : nullptr;
        if (!extension || !extension->load ||
            !extension->load(self->base.innerPlugin, stream, contextType))
            return false;
        self->clearBrowserAfterStateRestore();
        return true;
    }

    static bool CLAP_ABI presetLoadFromLocation(const clap_plugin_t *outer,
                                                std::uint32_t locationKind,
                                                const char *location,
                                                const char *loadKey) {
        auto *self = from(outer);
        const auto *extension = self ? self->innerPresetLoad() : nullptr;
        if (!extension || !extension->from_location ||
            !extension->from_location(self->base.innerPlugin,
                                      locationKind,
                                      location,
                                      loadKey))
            return false;
        self->syncBrowserIdentityAfterHostPresetLoad(locationKind, location, loadKey);
        return true;
    }

    static int32_t CLAP_ABI webviewGetUri(const clap_plugin_t *outer,
                                          char *uri,
                                          std::uint32_t uriCapacity) {
        return BaseProxy::webviewGetUri(outer, uri, uriCapacity);
    }

    static bool CLAP_ABI webviewGetResource(const clap_plugin_t *outer,
                                             const char *path,
                                             char *mime,
                                             std::uint32_t mimeCapacity,
                                             const clap_ostream_t *stream) {
        return BaseProxy::webviewGetResource(outer, path, mime, mimeCapacity, stream);
    }

    static bool CLAP_ABI webviewReceive(const clap_plugin_t *outer,
                                        const void *buffer,
                                        std::uint32_t size) {
        auto *self = from(outer);
        if (!self || !self->base.initialized || !self->base.state ||
            !self->base.state->guiCreated || !self->base.state->gui.isOnGuiThread() || !buffer)
            return false;

        self->consumePendingDirty();
        auto apply = [self](presets::PresetBrowserContentKind kind,
                            std::string_view identity,
                            const presets::PresetDocument &) {
            return self->applyBrowserSelection(kind, identity);
        };
        auto capture = [self](std::string_view displayName)
            -> std::optional<presets::PresetDocument> {
            return self->captureBrowserDocument(displayName);
        };
        auto reset = [self]() { return self->loadFactoryByKey("polysynth:init"); };
        auto send = [self](const void *data, std::size_t dataSize) {
            return self->base.state && self->base.state->guiCreated &&
                   self->base.state->gui.send(data, dataSize);
        };
        const auto browserResult = self->browserRuntime.receive(buffer,
                                                                size,
                                                                apply,
                                                                capture,
                                                                reset,
                                                                send);
        if (browserResult.handled) {
            if (browserResult.ok())
                self->presetDirtyPending.store(false, std::memory_order_release);
            return browserResult.ok();
        }

        const bool accepted = BaseProxy::webviewReceive(outer, buffer, size);
        if (accepted && isPersistentUiValueMessage(buffer, size))
            self->presetDirtyPending.store(true, std::memory_order_release);
        return accepted;
    }

    [[nodiscard]] const clap_plugin_preset_load_t *innerPresetLoad() const noexcept {
        if (!base.innerPlugin || !base.innerPlugin->get_extension)
            return nullptr;
        auto *extension = static_cast<const clap_plugin_preset_load_t *>(
            base.innerPlugin->get_extension(base.innerPlugin, CLAP_EXT_PRESET_LOAD));
        if (!extension) {
            extension = static_cast<const clap_plugin_preset_load_t *>(
                base.innerPlugin->get_extension(base.innerPlugin, CLAP_EXT_PRESET_LOAD_COMPAT));
        }
        return extension;
    }

    [[nodiscard]] const clap_plugin_state_t *innerState() const noexcept {
        return base.innerPlugin && base.innerPlugin->get_extension
                   ? static_cast<const clap_plugin_state_t *>(
                         base.innerPlugin->get_extension(base.innerPlugin, CLAP_EXT_STATE))
                   : nullptr;
    }

    [[nodiscard]] const clap_plugin_state_context_t *innerStateContext() const noexcept {
        return base.innerPlugin && base.innerPlugin->get_extension
                   ? static_cast<const clap_plugin_state_context_t *>(
                         base.innerPlugin->get_extension(base.innerPlugin, CLAP_EXT_STATE_CONTEXT))
                   : nullptr;
    }

    void consumePendingDirty() noexcept {
        if (presetDirtyPending.exchange(false, std::memory_order_acq_rel))
            browserController.markPersistentEdit();
    }

    void clearBrowserAfterStateRestore() noexcept {
        presetDirtyPending.store(false, std::memory_order_release);
        browserController.clearIdentityAfterStateRestore();
    }

    bool loadFactoryByKey(std::string_view identity) {
        const auto *extension = innerPresetLoad();
        if (!extension || !extension->from_location || identity.empty())
            return false;
        const std::string key{identity};
        return extension->from_location(base.innerPlugin,
                                        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                        nullptr,
                                        key.c_str());
    }

    bool applyBrowserSelection(presets::PresetBrowserContentKind kind,
                               std::string_view identity) {
        if (kind == presets::PresetBrowserContentKind::Factory)
            return loadFactoryByKey(identity);
        if (kind != presets::PresetBrowserContentKind::User)
            return false;
#if defined(__wasi__)
        (void)identity;
        return false;
#else
        const auto root = browserStorage.get()->nativeFileRoot();
        const auto *extension = innerPresetLoad();
        if (!root || !extension || !extension->from_location || identity.empty())
            return false;
        const auto path = std::filesystem::u8path(*root) /
                          std::filesystem::u8path(std::string{identity});
        const auto pathText = path.generic_string();
        return extension->from_location(base.innerPlugin,
                                        CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                        pathText.c_str(),
                                        nullptr);
#endif
    }

    [[nodiscard]] std::optional<presets::PresetDocument> captureBrowserDocument(
        std::string_view displayName) const {
        if (!base.innerParams || !base.innerPlugin)
            return std::nullopt;
        auto snapshot = defaultParameterSnapshot();
        for (const auto &spec : kParameterSpecs) {
            double value = 0.0;
            if (!base.innerParams->get_value(base.innerPlugin, spec.id, &value) ||
                !std::isfinite(value) || !setParameterSnapshotValue(snapshot, spec.id, value))
                return std::nullopt;
        }
        presets::PresetMetadata metadata;
        metadata.name.assign(displayName.data(), displayName.size());
        metadata.creator = "webview-gui";
        metadata.tags = {"user"};
        metadata.features = {"instrument", "synthesizer"};
        return capturePolySynthPreset(snapshot, std::move(metadata));
    }

    void syncBrowserIdentityAfterHostPresetLoad(std::uint32_t locationKind,
                                                const char *location,
                                                const char *loadKey) {
        presetDirtyPending.store(false, std::memory_order_release);
        if (!browserController.refresh().ok()) {
            browserController.clearIdentityAfterStateRestore();
            return;
        }
        if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN &&
            !location && loadKey && loadKey[0] != '\0') {
            if (!browserController.markLoaded(presets::PresetBrowserContentKind::Factory, loadKey))
                browserController.clearIdentityAfterStateRestore();
            return;
        }
#if !defined(__wasi__)
        if (locationKind == CLAP_PRESET_DISCOVERY_LOCATION_FILE &&
            location && location[0] != '\0' && !loadKey) {
            const auto identity = std::filesystem::u8path(location).filename().generic_string();
            if (!browserController.markLoaded(presets::PresetBrowserContentKind::User, identity))
                browserController.clearIdentityAfterStateRestore();
            return;
        }
#else
        (void)location;
        (void)loadKey;
#endif
        browserController.clearIdentityAfterStateRestore();
    }

    inline static const clap_plugin_params_t paramsProxy{
        paramsCount,
        paramsGetInfo,
        paramsGetValue,
        paramsValueToText,
        paramsTextToValue,
        paramsFlush,
    };

    inline static const clap_plugin_state_t stateProxy{
        stateSave,
        stateLoad,
    };

    inline static const clap_plugin_state_context_t stateContextProxy{
        stateContextSave,
        stateContextLoad,
    };

    inline static const clap_plugin_preset_load_t presetLoadProxy{
        presetLoadFromLocation,
    };

    inline static const ::webview_gui::clap_plugin_webview webviewProxy{
        webviewGetUri,
        webviewGetResource,
        webviewReceive,
    };
};

} // namespace preset_browser_detail

inline const clap_plugin_t *wrapPolySynthPresetBrowserPlugin(const clap_plugin_t *inner,
                                                              const clap_host_t *host) noexcept {
    if (!inner || !host || !inner->init || !inner->destroy || !inner->get_extension ||
        !inner->process)
        return nullptr;
    auto *state = new (std::nothrow) preset_browser_detail::BrowserProxyState(host);
    if (!state)
        return nullptr;
    auto *proxy = new (std::nothrow) preset_browser_detail::PolySynthPresetBrowserProxy(inner, state);
    if (!proxy) {
        delete state;
        return nullptr;
    }
    return &proxy->base.plugin;
}

} // namespace webview_gui::examples::polysynth::wclap
