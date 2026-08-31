#pragma once

#include "clap-webview-gui.h"

#include <clap/clap.h>

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace webview_gui {
namespace detail {

// Compatibility boundary for historical WCLAP bridges which query only the
// draft clap.webview/3 plug-in extension before calling clap_plugin.init().
//
// The host-facing clap_plugin_t is a compatibility table, while every delegated
// inner callback retains the original clap_plugin_t pointer identity expected by
// the wrapped implementation. Only get_extension(clap.webview/3) is intercepted
// before init; every other extension ID is delegated to the original
// implementation, preserving its lifecycle checks. After successful init the
// real WebView extension is resolved once and the stable compatibility table
// forwards to it.
struct LegacyWclapWebviewPluginProxy {
    clap_plugin_t plugin{};

    const clap_plugin_t *innerPlugin = nullptr;
    const clap_plugin_webview *realWebview = nullptr;
    bool initialized = false;

    explicit LegacyWclapWebviewPluginProxy(const clap_plugin_t *inner) noexcept
        : plugin(*inner), innerPlugin(inner) {
        plugin.init = proxyInit;
        plugin.destroy = proxyDestroy;
        plugin.activate = inner->activate ? proxyActivate : nullptr;
        plugin.deactivate = inner->deactivate ? proxyDeactivate : nullptr;
        plugin.start_processing = inner->start_processing ? proxyStartProcessing : nullptr;
        plugin.stop_processing = inner->stop_processing ? proxyStopProcessing : nullptr;
        plugin.reset = inner->reset ? proxyReset : nullptr;
        plugin.process = inner->process ? proxyProcess : nullptr;
        plugin.get_extension = proxyGetExtension;
        plugin.on_main_thread = inner->on_main_thread ? proxyOnMainThread : nullptr;
    }

    static LegacyWclapWebviewPluginProxy *from(const clap_plugin_t *plugin) noexcept {
        return reinterpret_cast<LegacyWclapWebviewPluginProxy *>(
            const_cast<clap_plugin_t *>(plugin));
    }

    static bool CLAP_ABI proxyInit(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        const auto *inner = self ? self->innerPlugin : nullptr;
        if (!inner || !inner->init || self->initialized)
            return false;

        if (!inner->init(inner))
            return false;

        // clap_plugin.get_extension() is legal after the original init returns.
        // Resolve once so WebView callbacks never perform repeated extension
        // discovery and the compatibility pointer remains stable for the bridge.
        self->realWebview = inner->get_extension
                                ? static_cast<const clap_plugin_webview *>(
                                      inner->get_extension(inner, CLAP_EXT_WEBVIEW))
                                : nullptr;
        self->initialized = true;
        return true;
    }

    static void CLAP_ABI proxyDestroy(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        if (!self)
            return;

        const auto *inner = self->innerPlugin;
        self->initialized = false;
        self->realWebview = nullptr;
        self->innerPlugin = nullptr;
        if (inner && inner->destroy)
            inner->destroy(inner);
        delete self;
    }

    static bool CLAP_ABI proxyActivate(const clap_plugin_t *plugin,
                                       double sampleRate,
                                       uint32_t minFramesCount,
                                       uint32_t maxFramesCount) {
        auto *self = from(plugin);
        const auto *inner = self ? self->innerPlugin : nullptr;
        return inner && inner->activate &&
               inner->activate(inner, sampleRate, minFramesCount, maxFramesCount);
    }

    static void CLAP_ABI proxyDeactivate(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        const auto *inner = self ? self->innerPlugin : nullptr;
        if (inner && inner->deactivate)
            inner->deactivate(inner);
    }

    static bool CLAP_ABI proxyStartProcessing(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        const auto *inner = self ? self->innerPlugin : nullptr;
        return inner && inner->start_processing && inner->start_processing(inner);
    }

    static void CLAP_ABI proxyStopProcessing(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        const auto *inner = self ? self->innerPlugin : nullptr;
        if (inner && inner->stop_processing)
            inner->stop_processing(inner);
    }

    static void CLAP_ABI proxyReset(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        const auto *inner = self ? self->innerPlugin : nullptr;
        if (inner && inner->reset)
            inner->reset(inner);
    }

    static clap_process_status CLAP_ABI proxyProcess(const clap_plugin_t *plugin,
                                                      const clap_process_t *process) {
        auto *self = from(plugin);
        const auto *inner = self ? self->innerPlugin : nullptr;
        return inner && inner->process ? inner->process(inner, process) : CLAP_PROCESS_ERROR;
    }

    static const void *CLAP_ABI proxyGetExtension(const clap_plugin_t *plugin,
                                                   const char *id) {
        auto *self = from(plugin);
        if (!self || !id)
            return nullptr;

        if (std::strcmp(id, CLAP_EXT_WEBVIEW) == 0)
            return &webviewProxy;

        // Deliberately do not broaden the compatibility exception. A non-WebView
        // pre-init request reaches the original strict implementation unchanged.
        const auto *inner = self->innerPlugin;
        return inner && inner->get_extension ? inner->get_extension(inner, id) : nullptr;
    }

    static void CLAP_ABI proxyOnMainThread(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        const auto *inner = self ? self->innerPlugin : nullptr;
        if (inner && inner->on_main_thread)
            inner->on_main_thread(inner);
    }

    static int32_t CLAP_ABI proxyGetUri(const clap_plugin_t *plugin,
                                        char *uri,
                                        uint32_t uriCapacity) {
        auto *self = from(plugin);
        if (!self || !self->initialized || !self->innerPlugin || !self->realWebview ||
            !self->realWebview->get_uri)
            return -1;
        return self->realWebview->get_uri(self->innerPlugin, uri, uriCapacity);
    }

    static bool CLAP_ABI proxyGetResource(const clap_plugin_t *plugin,
                                          const char *path,
                                          char *mime,
                                          uint32_t mimeCapacity,
                                          const clap_ostream_t *stream) {
        auto *self = from(plugin);
        return self && self->initialized && self->innerPlugin && self->realWebview &&
               self->realWebview->get_resource &&
               self->realWebview->get_resource(self->innerPlugin,
                                                path,
                                                mime,
                                                mimeCapacity,
                                                stream);
    }

    static bool CLAP_ABI proxyReceive(const clap_plugin_t *plugin,
                                      const void *buffer,
                                      uint32_t size) {
        auto *self = from(plugin);
        return self && self->initialized && self->innerPlugin && self->realWebview &&
               self->realWebview->receive &&
               self->realWebview->receive(self->innerPlugin, buffer, size);
    }

    inline static const clap_plugin_webview webviewProxy{
        proxyGetUri,
        proxyGetResource,
        proxyReceive,
    };
};

static_assert(std::is_standard_layout_v<LegacyWclapWebviewPluginProxy>,
              "Legacy WCLAP proxy must remain pointer-interconvertible with clap_plugin_t");
static_assert(offsetof(LegacyWclapWebviewPluginProxy, plugin) == 0,
              "Legacy WCLAP proxy requires clap_plugin_t as its first member");

} // namespace detail

// On success, the wrapper assumes ownership of the original plug-in through its
// destroy callback. On nullptr, ownership remains with the caller. Use this only
// for a plug-in known to implement clap.webview/3.
inline const clap_plugin_t *wrapLegacyWclapWebviewPlugin(
    const clap_plugin_t *plugin) noexcept {
    if (!plugin || !plugin->init || !plugin->destroy || !plugin->get_extension)
        return nullptr;

    auto *proxy = new (std::nothrow) detail::LegacyWclapWebviewPluginProxy(plugin);
    return proxy ? &proxy->plugin : nullptr;
}

} // namespace webview_gui
