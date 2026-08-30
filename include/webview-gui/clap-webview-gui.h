#pragma once

#include "clap/clap.h"
#include "webview-gui.h"
#include "_impl/plugin_support.h"
#include "_impl/callback_registry.h"
#include "_impl/bounded_buffer.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace webview_gui {

static constexpr const char CLAP_EXT_WEBVIEW[] = "clap.webview/3";
static constexpr const char CLAP_WINDOW_API_WEBVIEW[] = "webview";
struct clap_plugin_webview {
    int32_t (CLAP_ABI *get_uri)(const clap_plugin_t *plugin, char *uri, uint32_t uri_capacity);
    bool (CLAP_ABI *get_resource)(const clap_plugin_t *plugin, const char *path, char *mime, uint32_t mime_capacity, const clap_ostream_t *stream);
    bool (CLAP_ABI *receive)(const clap_plugin_t *plugin, const void *buffer, uint32_t size);
};
struct clap_host_webview {
    bool(CLAP_ABI *send)(const clap_host_t *host, const void *buffer, uint32_t size);
};

struct ClapWebviewGui {
    clap_plugin_gui *extPluginGui = nullptr;
    const clap_host_webview *extHostWebview = nullptr;

    ClapWebviewGui(const clap_plugin *plugin=nullptr, const clap_host *host=nullptr)
        : plugin(plugin), host(host) {
        extPluginGui = &pluginGuiProxy;
    }

    ~ClapWebviewGui() {
        assert(!uiThread.isBound() || uiThread.isCurrentThread());
        clearSelf(plugin);
        destroy();
        plugin = nullptr;
        host = nullptr;
        pluginWebview = nullptr;
        hostWebview = nullptr;
        extHostWebview = nullptr;
    }

    void init(const clap_plugin *initPlugin, const clap_host *initHost) {
        if (uiThread.isBound() && !uiThread.isCurrentThread())
            return;
        clearSelf(plugin);
        destroy();
        plugin = initPlugin;
        host = initHost;
        initialiseCurrentIdentity();
    }

    void init() {
        if (uiThread.isBound() && !uiThread.isCurrentThread())
            return;
        clearSelf(plugin);
        destroy();
        initialiseCurrentIdentity();
    }

    [[nodiscard]] bool isOnGuiThread() const noexcept { return uiThread.isCurrentThread(); }

    bool isApiSupported(const char *api, bool is_floating) {
        if (!isOnGuiThread() || !api) return false;
        if (!std::strcmp(api, CLAP_WINDOW_API_WEBVIEW))
            return !is_floating && hasHostWebviewPath();
        if (is_floating) return false;
        return WebviewGui::supports(clapApiToPlatform(api));
    }

    bool getPreferredApi(const char **api, bool *is_floating) {
        if (!isOnGuiThread() || !api || !is_floating) return false;

        if (hasHostWebviewPath()) {
            *api = CLAP_WINDOW_API_WEBVIEW;
            *is_floating = false;
            return true;
        }

        const auto *nativeApi = preferredNativeApi();
        if (!nativeApi)
            return false;

        *api = nativeApi;
        *is_floating = false;
        return true;
    }

    bool create(const char *api, bool is_floating) {
        if (!isOnGuiThread() || !api || guiCreated) return false;
        if (!std::strcmp(api, CLAP_WINDOW_API_WEBVIEW)) {
            if (is_floating || !hasHostWebviewPath())
                return false;
            nativePlatform = WebviewGui::NONE;
            usingHostWebview = true;
            guiCreated = true;
            return true;
        }
        if (is_floating) return false;

        std::string startUrl = getNativeStartUrl();
        if (startUrl.empty()) return false;
        if (!isAbsolute(startUrl.c_str()) && startUrl[0] != '/')
            startUrl = "/" + startUrl;

        const auto platform = clapApiToPlatform(api);
        if (platform == WebviewGui::NONE || !WebviewGui::supports(platform))
            return false;

        WebviewGui *ptr = nullptr;

        if (startUrl.rfind("file:", 0) == 0) {
            size_t pos = 5;
            while (pos < startUrl.size() && startUrl[pos] == '/') ++pos;
            startUrl = startUrl.substr(pos);

            std::string baseDir = startUrl;
            while (!baseDir.empty() && baseDir.back() != '/') baseDir.pop_back();
            if (!baseDir.empty()) baseDir.pop_back();
            startUrl = startUrl.substr(std::min(baseDir.size(), startUrl.size()));
#if defined (_WIN32) || defined (_WIN64)
            for (auto &c : baseDir)
                if (c == '/') c = '\\';
#else
            if (!baseDir.empty() && baseDir[0] != '/') baseDir = "/" + baseDir;
#endif
            ptr = WebviewGui::create(platform, startUrl.c_str(), baseDir);
        } else {
            ptr = WebviewGui::create(platform, startUrl.c_str(), [this](const char *path, WebviewGui::Resource &resource){
                if (!isOnGuiThread() || !pluginWebview || !pluginWebview->get_resource || !plugin)
                    return false;

                char mediaType[256] = {0};
                struct ResourceStream : public clap_ostream {
                    WebviewGui::Resource &resource;
                    bool overflowed = false;

                    explicit ResourceStream(WebviewGui::Resource &resource) : resource(resource) {
                        *(clap_ostream *)this = {/*ctx*/this, write};
                    }

                    static int64_t write(const clap_ostream *stream, const void *buffer, uint64_t length) {
                        if (!stream || !stream->ctx || (!buffer && length != 0)) return -1;
                        auto &self = *(ResourceStream *)stream->ctx;
                        if (length > detail::maxResourceBytes
                            || !detail::appendBoundedBytes(self.resource.bytes,
                                                           buffer,
                                                           static_cast<size_t>(length),
                                                           detail::maxResourceBytes)) {
                            self.overflowed = true;
                            return -1;
                        }
                        return static_cast<int64_t>(length);
                    }
                } resourceStream{resource};

                const bool success = pluginWebview->get_resource(plugin, path, mediaType, 255, &resourceStream);
                if (!success || resourceStream.overflowed || !detail::resourceSizeAllowed(resource.bytes.size())) {
                    resource.bytes.clear();
                    return false;
                }
                resource.mediaType = mediaType;
                return true;
            });
        }

        if (!ptr) return false;

        nativeWebview = std::unique_ptr<WebviewGui>{ptr};
        nativePlatform = platform;
        nativeWebview->receive = [this](const unsigned char *bytes, size_t length){
            if (isOnGuiThread() && detail::messageSizeAllowed(length)
                && pluginWebview && pluginWebview->receive && plugin)
                pluginWebview->receive(plugin, (const void *)bytes, uint32_t(length));
        };
        usingHostWebview = false;
        guiCreated = true;
        return true;
    }

    void destroy() {
        if (!isOnGuiThread()) return;
        if (nativeWebview)
            nativeWebview->receive = {};
        nativeWebview.reset();
        nativePlatform = WebviewGui::NONE;
        usingHostWebview = false;
        guiCreated = false;
    }

    bool setScale(double) { return false; }

    bool getSize(uint32_t *w, uint32_t *h) {
        if (!isOnGuiThread() || !w || !h) return false;
        *w = width;
        *h = height;
        return true;
    }

    bool canResize() { return isOnGuiThread(); }

    bool getResizeHints(clap_gui_resize_hints_t *hints) {
        if (!isOnGuiThread() || !hints) return false;
        *hints = {true, true, false, 0, 0};
        return true;
    }

    bool adjustSize(uint32_t *w, uint32_t *h) {
        return isOnGuiThread() && w != nullptr && h != nullptr;
    }

    bool setSize(uint32_t w, uint32_t h) {
        if (!isOnGuiThread()) return false;
        if (nativeWebview && !nativeWebview->trySetSize(w, h))
            return false;
        width = w;
        height = h;
        return true;
    }

    bool setParent(const clap_window *window) {
        if (!isOnGuiThread() || !window || !window->api)
            return false;

        if (usingHostWebview) {
            return guiCreated
                && std::strcmp(window->api, CLAP_WINDOW_API_WEBVIEW) == 0
                && window->ptr == nullptr;
        }

        if (!nativeWebview)
            return false;

        void *parent = nullptr;
        switch (nativePlatform) {
            case WebviewGui::Platform::COCOA:
                if (std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0) return false;
                parent = window->cocoa;
                break;
            case WebviewGui::Platform::HWND:
                if (std::strcmp(window->api, CLAP_WINDOW_API_WIN32) != 0) return false;
                parent = window->win32;
                break;
            case WebviewGui::Platform::X11EMBED:
                if (std::strcmp(window->api, CLAP_WINDOW_API_X11) != 0) return false;
                parent = reinterpret_cast<void*>(static_cast<std::uintptr_t>(window->x11));
                break;
            default:
                return false;
        }

        return parent != nullptr && nativeWebview->attach(parent);
    }

    bool setTransient(const clap_window *) { return false; }
    void suggestTitle(const char *) {}

    bool show() {
        if (!isOnGuiThread() || !guiCreated) return false;
        if (usingHostWebview)
            return true;
        if (!nativeWebview) return false;
        nativeWebview->setVisible(true);
        return true;
    }

    bool hide() {
        if (!isOnGuiThread() || !guiCreated) return false;
        if (usingHostWebview)
            return true;
        if (!nativeWebview) return false;
        nativeWebview->setVisible(false);
        return true;
    }

    bool send(const void *buffer, size_t length) {
        if (!isOnGuiThread() || (!buffer && length != 0) || !detail::messageSizeAllowed(length)) return false;
        if (nativeWebview) {
            nativeWebview->send((const unsigned char *)buffer, length);
            return true;
        }
        if (guiCreated && usingHostWebview && hostWebview && hostWebview->send && host)
            return hostWebview->send(host, buffer, uint32_t(length));
        return false;
    }

#ifdef WEBVIEW_GUI_TESTING
    [[nodiscard]] bool testHasNativeWebview() const noexcept { return nativeWebview != nullptr; }
    [[nodiscard]] bool testUsesHostWebview() const noexcept { return guiCreated && usingHostWebview; }

    bool testDeliverNativeMessage(const void *buffer, size_t length) {
        if (!nativeWebview || !nativeWebview->receive || (!buffer && length != 0))
            return false;
        nativeWebview->receive(static_cast<const unsigned char *>(buffer), length);
        return true;
    }
#endif

private:
    [[nodiscard]] bool hasHostWebviewPath() const noexcept {
        return plugin != nullptr
            && host != nullptr
            && pluginWebview != nullptr
            && pluginWebview->get_uri != nullptr
            && pluginWebview->get_resource != nullptr
            && pluginWebview->receive != nullptr
            && hostWebview != nullptr
            && hostWebview->send != nullptr;
    }

    void initialiseCurrentIdentity() {
        uiThread.bindToCurrentThread();
        pluginWebview = nullptr;
        hostWebview = nullptr;
        extHostWebview = nullptr;

        if (plugin && plugin->get_extension)
            pluginWebview = (const clap_plugin_webview *)plugin->get_extension(plugin, CLAP_EXT_WEBVIEW);
        if (host && host->get_extension)
            hostWebview = (const clap_host_webview *)host->get_extension(host, CLAP_EXT_WEBVIEW);

        extHostWebview = hostWebview;
        setSelf(plugin);
    }

    uint32_t width = 400, height = 250;
    const clap_plugin *plugin = nullptr;
    const clap_host *host = nullptr;
    detail::ThreadAffinity uiThread;
    std::unique_ptr<WebviewGui> nativeWebview;
    WebviewGui::Platform nativePlatform = WebviewGui::NONE;
    bool guiCreated = false;
    bool usingHostWebview = false;

    inline static detail::CallbackRegistry<ClapWebviewGui> pluginRegistry;

    void setSelf(const void *pluginPtr) { pluginRegistry.set(pluginPtr, this); }
    void clearSelf(const void *pluginPtr) { pluginRegistry.eraseIfMatches(pluginPtr, this); }

    template <typename Callback>
    static bool visitSelf(const void *pluginPtr, Callback&& callback) {
        bool result = false;
        pluginRegistry.visit(pluginPtr, [&](ClapWebviewGui& self) {
            if (self.isOnGuiThread())
                result = callback(self);
        });
        return result;
    }

    template <typename Callback>
    static void visitSelfVoid(const void *pluginPtr, Callback&& callback) {
        pluginRegistry.visit(pluginPtr, [&](ClapWebviewGui& self) {
            if (self.isOnGuiThread())
                callback(self);
        });
    }

    char startUrlBuffer[2048] = {0};
    const char * getNativeStartUrl() {
        if (!isOnGuiThread()) return "";
        if (pluginWebview && pluginWebview->get_uri && plugin) {
            auto uriLength = pluginWebview->get_uri(plugin, startUrlBuffer, 2047);
            if (uriLength >= 2048) {
                std::strcpy(startUrlBuffer, "data:text/html,URI%20too%20long");
            } else if (uriLength <= 0) {
                std::strcpy(startUrlBuffer, "data:text/html,get_uri%20error");
            }
        } else {
            std::strcpy(startUrlBuffer, "data:text/html,no%20plugin%20webview%20ext");
        }
        return startUrlBuffer;
    }

    static bool isAbsolute(const char *uri) {
        if (!uri || *uri == ':') return false;
        while (*uri) {
            const auto c = *(uri++);
            if (c == ':') return true;
            if (c >= 'A' && c <= 'Z') continue;
            if (c >= 'a' && c <= 'z') continue;
            if (c >= '0' && c <= '9') continue;
            if (c == '+' || c == '.' || c == '-') continue;
            return false;
        }
        return false;
    }

    static WebviewGui::Platform clapApiToPlatform(const char *api) {
        if (!api) return WebviewGui::NONE;
        if (!std::strcmp(api, CLAP_WINDOW_API_WIN32)) return WebviewGui::HWND;
        if (!std::strcmp(api, CLAP_WINDOW_API_COCOA)) return WebviewGui::COCOA;
        if (!std::strcmp(api, CLAP_WINDOW_API_X11)) return WebviewGui::X11EMBED;
        return WebviewGui::NONE;
    }

    static const char *preferredNativeApi() {
#if defined(__APPLE__)
        if (WebviewGui::supports(WebviewGui::COCOA))
            return CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32) || defined(_WIN64)
        if (WebviewGui::supports(WebviewGui::HWND))
            return CLAP_WINDOW_API_WIN32;
#elif defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(__wasm__) && !defined(__wasm32__) && !defined(__wasm64__)
        if (WebviewGui::supports(WebviewGui::X11EMBED))
            return CLAP_WINDOW_API_X11;
#endif
        return nullptr;
    }

    const clap_plugin_webview *pluginWebview = nullptr;
    const clap_host_webview *hostWebview = nullptr;

    clap_plugin_gui pluginGuiProxy{
        gui_is_api_supported,
        gui_get_preferred_api,
        gui_create,
        gui_destroy,
        gui_set_scale,
        gui_get_size,
        gui_can_resize,
        gui_get_resize_hints,
        gui_adjust_size,
        gui_set_size,
        gui_set_parent,
        gui_set_transient,
        gui_suggest_title,
        gui_show,
        gui_hide
    };

    static bool gui_is_api_supported(const clap_plugin *plugin, const char *api, bool is_floating) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) {
            return self.isApiSupported(api, is_floating);
        });
    }
    static bool gui_get_preferred_api(const clap_plugin *plugin, const char **api, bool *is_floating) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) {
            return self.getPreferredApi(api, is_floating);
        });
    }
    static bool gui_create(const clap_plugin *plugin, const char *api, bool is_floating) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) {
            return self.create(api, is_floating);
        });
    }
    static void gui_destroy(const clap_plugin *plugin) {
        visitSelfVoid(plugin, [&](ClapWebviewGui& self) { self.destroy(); });
    }
    static bool gui_set_scale(const clap_plugin *plugin, double scale) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) { return self.setScale(scale); });
    }
    static bool gui_get_size(const clap_plugin *plugin, uint32_t *w, uint32_t *h) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) { return self.getSize(w, h); });
    }
    static bool gui_can_resize(const clap_plugin *plugin) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) { return self.canResize(); });
    }
    static bool gui_get_resize_hints(const clap_plugin *plugin, clap_gui_resize_hints_t *hints) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) { return self.getResizeHints(hints); });
    }
    static bool gui_adjust_size(const clap_plugin *plugin, uint32_t *w, uint32_t *h) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) { return self.adjustSize(w, h); });
    }
    static bool gui_set_size(const clap_plugin *plugin, uint32_t w, uint32_t h) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) {
            return self.guiCreated && self.setSize(w, h);
        });
    }
    static bool gui_set_parent(const clap_plugin *plugin, const clap_window *window) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) { return self.setParent(window); });
    }
    static bool gui_set_transient(const clap_plugin *plugin, const clap_window *window) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) { return self.setTransient(window); });
    }
    static void gui_suggest_title(const clap_plugin *plugin, const char *title) {
        visitSelfVoid(plugin, [&](ClapWebviewGui& self) { self.suggestTitle(title); });
    }
    static bool gui_show(const clap_plugin *plugin) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) { return self.show(); });
    }
    static bool gui_hide(const clap_plugin *plugin) {
        return visitSelf(plugin, [&](ClapWebviewGui& self) { return self.hide(); });
    }
};

} // namespace webview_gui
