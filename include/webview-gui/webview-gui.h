#pragma once

#ifdef WEBVIEW_GUI_HEADER_ONLY
#error WEBVIEW_GUI_HEADER_ONLY is unsupported for the plugin-safe profile; use the webview-gui CMake target so qualified private CHOC patches are applied
#endif

#include <functional>
#include <vector>
#include <string>
#include <memory>

namespace webview_gui {

#define WEBVIEW_GUI_IMPL

struct WebviewGui {
	enum class Platform {
		NONE, HWND, COCOA, X11EMBED
	};
	static constexpr Platform NONE = Platform::NONE;
	static constexpr Platform HWND = Platform::HWND;
	static constexpr Platform COCOA = Platform::COCOA;
	static constexpr Platform X11EMBED = Platform::X11EMBED;
	
	struct Resource {
		std::string mediaType;
		std::vector<unsigned char> bytes;
	};
	using ResourceGetter = std::function<bool(const char *path, Resource &resource)>;

	// Thread contract
	// ---------------
	// The thread that calls create()/createUnique()/createShared() becomes this
	// instance's UI/message thread. The instance must be destroyed on that same
	// thread. attach(), setSize(), setVisible() and send() are UI-thread-only;
	// off-thread calls fail/no-op rather than entering CHOC or the host GUI.
	// ResourceGetter and receive callbacks are delivered on that same UI thread.
	//
	// Never call these APIs from a CLAP process()/audio callback. Publish small
	// trivially-copyable state through realtime-handoff.h (or another proven
	// bounded non-blocking SPSC mechanism) and consume it on the UI thread.
	// supports() is a pure platform capability query and does not touch a WebView.
	WEBVIEW_GUI_IMPL static bool supports(Platform p);
	WEBVIEW_GUI_IMPL static WebviewGui * create(Platform platform, const std::string &startUrl);
	// The starting URL may be relative for these:
	WEBVIEW_GUI_IMPL static WebviewGui * create(Platform platform, const std::string &startUrl, const std::string &baseDir);
	WEBVIEW_GUI_IMPL static WebviewGui * create(Platform platform, const std::string &startUrl, ResourceGetter getter);
	WEBVIEW_GUI_IMPL ~WebviewGui();
	
	// Convenience template for creating shared/unique pointers
	using UniquePtr = std::unique_ptr<WebviewGui>;
	template<class... Args>
	static UniquePtr createUnique(Args &&...args) {
		return UniquePtr{create(std::forward<Args>(args)...)};
	}
	using SharedPtr = std::shared_ptr<WebviewGui>;
	template<class... Args>
	static SharedPtr createShared(Args &&...args) {
		return SharedPtr{create(std::forward<Args>(args)...)};
	}
	
	// UI thread only. Attach to the host-owned native parent. Returns false when
	// called from another thread, when the backend cannot actually be embedded,
	// or when the native handle is invalid.
	WEBVIEW_GUI_IMPL bool attach(void *platformNative);

	// Assign and replace receive on the UI thread. CHOC/native bridge delivery
	// invokes it synchronously on that thread; do not perform audio-thread work
	// or unbounded blocking inside the callback.
	std::function<void(const unsigned char *, size_t)> receive;

	// UI thread only. Off-thread calls are ignored and never enter CHOC.
	WEBVIEW_GUI_IMPL void send(const unsigned char *, size_t);
	
	// UI thread only. trySetSize() reports whether the backend accepted/applied
	// the request. setSize() remains as the source-compatible legacy convenience
	// wrapper and intentionally discards that result.
	WEBVIEW_GUI_IMPL bool trySetSize(double width, double height);
	WEBVIEW_GUI_IMPL void setSize(double width, double height);
	WEBVIEW_GUI_IMPL void setVisible(bool visible);
private:
	struct Impl;
	Impl *impl;
	// Can only be created using the static methods
	WEBVIEW_GUI_IMPL WebviewGui(Impl *);
	WebviewGui(const WebviewGui &other) = delete;
};

#undef WEBVIEW_GUI_IMPL

} // namespace

using WebviewGui = ::webview_gui::WebviewGui;
