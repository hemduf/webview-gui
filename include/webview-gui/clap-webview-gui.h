#pragma once

#include "clap/clap.h"
#include "webview-gui.h"
#include "_impl/plugin_support.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>

namespace webview_gui {

// Copy the definitions here, in case you're stuck on an older CLAP version
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

	// This is only the real host extension. We deliberately do not manufacture a
	// host-pointer keyed proxy, because clap_host_t* is not a per-instance ID.
	const clap_host_webview *extHostWebview = nullptr;

	ClapWebviewGui(const clap_plugin *plugin=nullptr, const clap_host *host=nullptr)
		: plugin(plugin), host(host) {
		setSelf(plugin);
		extPluginGui = &pluginGuiProxy;
	}

	~ClapWebviewGui() {
		assert(!uiThread.isBound() || uiThread.isCurrentThread());
		destroy();
		clearSelf(plugin);
		plugin = nullptr;
		host = nullptr;
		pluginWebview = nullptr;
		hostWebview = nullptr;
		extHostWebview = nullptr;
	}
	
	// Call from plugin.init() on the host's CLAP main thread.
	void init(const clap_plugin *initPlugin, const clap_host *initHost) {
		clearSelf(plugin);
		plugin = initPlugin;
		host = initHost;
		setSelf(plugin);
		init();
	}

	void init() {
		uiThread.bindToCurrentThread();
		pluginWebview = nullptr;
		hostWebview = nullptr;
		extHostWebview = nullptr;

		if (plugin && plugin->get_extension)
			pluginWebview = (const clap_plugin_webview *)plugin->get_extension(plugin, CLAP_EXT_WEBVIEW);
		if (host && host->get_extension)
			hostWebview = (const clap_host_webview *)host->get_extension(host, CLAP_EXT_WEBVIEW);

		extHostWebview = hostWebview;
	}

	[[nodiscard]] bool isOnGuiThread() const noexcept {
		return uiThread.isCurrentThread();
	}
	
	bool isApiSupported(const char *api, bool is_floating) {
		if (!isOnGuiThread() || !api) return false;
		if (!std::strcmp(api, CLAP_WINDOW_API_WEBVIEW)) return true;
		if (is_floating) return false;
		return WebviewGui::supports(clapApiToPlatform(api));
	}
	
	bool getPreferredApi(const char **api, bool *is_floating) {
		if (!isOnGuiThread() || !api || !is_floating) return false;
		*api = CLAP_WINDOW_API_WEBVIEW;
		*is_floating = false;
		return true;
	}
	
	bool create(const char *api, bool is_floating) {
		if (!isOnGuiThread() || !api) return false;
		if (!std::strcmp(api, CLAP_WINDOW_API_WEBVIEW)) return true;
		if (is_floating) return false;
		
		std::string startUrl = getNativeStartUrl();
		if (startUrl.empty()) return false;
		if (!isAbsolute(startUrl.c_str()) && startUrl[0] != '/')
			startUrl = "/" + startUrl;

		auto platform = clapApiToPlatform(api);
		if (platform == WebviewGui::NONE) return false;

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
			for (auto &c : baseDir) {
				if (c == '/') c = '\\';
			}
#else
			if (!baseDir.empty() && baseDir[0] != '/') baseDir = "/" + baseDir;
#endif
			ptr = WebviewGui::create(platform, startUrl.c_str(), baseDir);
		} else {
			ptr = WebviewGui::create(platform, startUrl.c_str(), [this](const char *path, WebviewGui::Resource &resource){
				if (!isOnGuiThread() || !pluginWebview || !pluginWebview->get_resource || !plugin) return false;
				
				char mediaType[256] = {0};
				struct ResourceStream : public clap_ostream {
					WebviewGui::Resource &resource;
					
					ResourceStream(WebviewGui::Resource &resource) : resource(resource) {
						*(clap_ostream *)this = {/*ctx*/this, write};
					}
					static int64_t write(const clap_ostream *stream, const void *buffer, uint64_t length) {
						if (!stream || !stream->ctx || (!buffer && length != 0)) return -1;
						auto *byteBuffer = (const unsigned char *)buffer;
						auto &self = *(ResourceStream *)stream->ctx;
						self.resource.bytes.insert(self.resource.bytes.end(), byteBuffer, byteBuffer + length);
						return int64_t(length);
					};
				} resourceStream{resource};
				bool success = pluginWebview->get_resource(plugin, path, mediaType, 255, &resourceStream);
				if (success) resource.mediaType = mediaType;
				return success;
			});
		}
		if (!ptr) return false;

		nativeWebview = std::unique_ptr<WebviewGui>{ptr};
		nativeWebview->receive = [this](const unsigned char *bytes, size_t length){
			if (isOnGuiThread() && pluginWebview && pluginWebview->receive && plugin)
				pluginWebview->receive(plugin, (const void *)bytes, uint32_t(length));
		};
		return true;
	}

	void destroy() {
		if (!isOnGuiThread()) return;
		if (nativeWebview)
			nativeWebview->receive = {};
		nativeWebview.reset();
	}
	
	bool setScale(double) {
		return isOnGuiThread();
	}
	
	bool getSize(uint32_t *w, uint32_t *h) {
		if (!isOnGuiThread() || !w || !h) return false;
		*w = width;
		*h = height;
		return true;
	}
	
	bool canResize() {
		return isOnGuiThread();
	}
	
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
		width = w;
		height = h;
		if (nativeWebview) nativeWebview->setSize(w, h);
		return true;
	}
	
	bool setParent(const clap_window *window) {
		if (!isOnGuiThread()) return false;
		if (nativeWebview && window && window->ptr) {
			nativeWebview->attach(window->ptr);
			return true;
		}
		return false;
	}
	
	bool setTransient(const clap_window *) {
		return false;
	}
	
	void suggestTitle(const char *) {}
	
	bool show() {
		if (!isOnGuiThread()) return false;
		if (nativeWebview) {
			nativeWebview->setVisible(true);
			return true;
		}
		return false;
	}
	
	bool hide() {
		if (!isOnGuiThread()) return false;
		if (nativeWebview) {
			nativeWebview->setVisible(false);
			return true;
		}
		return false;
	}

	bool send(const void *buffer, size_t length) {
		if (!isOnGuiThread() || (!buffer && length != 0)) return false;
		if (nativeWebview) {
			nativeWebview->send((const unsigned char *)buffer, length);
			return true;
		}
		if (hostWebview && hostWebview->send && host)
			return hostWebview->send(host, buffer, uint32_t(length));
		return false;
	}
	
private:
	uint32_t width = 400, height = 250;
	const clap_plugin *plugin = nullptr;
	const clap_host *host = nullptr;

	detail::ThreadAffinity uiThread;
	std::unique_ptr<WebviewGui> nativeWebview;

	inline static detail::PointerRegistry<ClapWebviewGui> pluginRegistry;

	static ClapWebviewGui * getSelf(const void *pluginPtr) {
		return pluginRegistry.find(pluginPtr);
	}
	void setSelf(const void *pluginPtr) {
		pluginRegistry.set(pluginPtr, this);
	}
	void clearSelf(const void *pluginPtr) {
		pluginRegistry.eraseIfMatches(pluginPtr, this);
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
			auto c = *(uri++);
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
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->isApiSupported(api, is_floating) : false;
	}
	static bool gui_get_preferred_api(const clap_plugin *plugin, const char **api, bool *is_floating) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->getPreferredApi(api, is_floating) : false;
	}
	static bool gui_create(const clap_plugin *plugin, const char *api, bool is_floating) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->create(api, is_floating) : false;
	}
	static void gui_destroy(const clap_plugin *plugin) {
		if (auto *self = getSelf(plugin); self && self->isOnGuiThread()) self->destroy();
	}
	static bool gui_set_scale(const clap_plugin *plugin, double scale) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->setScale(scale) : false;
	}
	static bool gui_get_size(const clap_plugin *plugin, uint32_t *w, uint32_t *h) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->getSize(w, h) : false;
	}
	static bool gui_can_resize(const clap_plugin *plugin) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->canResize() : false;
	}
	static bool gui_get_resize_hints(const clap_plugin *plugin, clap_gui_resize_hints_t *hints) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->getResizeHints(hints) : false;
	}
	static bool gui_adjust_size(const clap_plugin *plugin, uint32_t *w, uint32_t *h) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->adjustSize(w, h) : false;
	}
	static bool gui_set_size(const clap_plugin *plugin, uint32_t w, uint32_t h) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->setSize(w, h) : false;
	}
	static bool gui_set_parent(const clap_plugin *plugin, const clap_window *window) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->setParent(window) : false;
	}
	static bool gui_set_transient(const clap_plugin *plugin, const clap_window *window) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->setTransient(window) : false;
	}
	static void gui_suggest_title(const clap_plugin *plugin, const char *title) {
		if (auto *self = getSelf(plugin); self && self->isOnGuiThread()) self->suggestTitle(title);
	}
	static bool gui_show(const clap_plugin *plugin) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->show() : false;
	}
	static bool gui_hide(const clap_plugin *plugin) {
		auto *self = getSelf(plugin);
		return self && self->isOnGuiThread() ? self->hide() : false;
	}
};

} // namespace webview_gui
