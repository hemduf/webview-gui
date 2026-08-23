#include "../../helpers.h"
#include "../plugin_support.h"

#include "choc/platform/choc_Platform.h"

#if !CHOC_APPLE && !CHOC_WINDOWS && !CHOC_LINUX
#	include "./not-supported.h"
#else
#	include "./choc_plugin_webview.h"
#	include "choc/memory/choc_Base64.h"

#	include <cassert>
#	include <filesystem>
#	include <fstream>
#	include <memory>
#	include <optional>

namespace webview_gui {

#	if CHOC_APPLE
} // close namespace
#		include <CoreFoundation/CFBundle.h>
namespace webview_gui {

struct WebviewGui::Impl {
	~Impl() {
		assert(!uiThread.isBound() || uiThread.isCurrentThread());
		using namespace choc::objc;
		if (webview) {
			id subview = (id)webview->getViewHandle();
			call<void>(subview, "removeFromSuperview");
		}
	}

	[[nodiscard]] bool isOnGuiThread() const noexcept { return uiThread.isCurrentThread(); }
	void init(const choc::ui::WebView::Options &options) {
		uiThread.bindToCurrentThread();
		webview = std::make_unique<choc::ui::WebView>(options);
	}
	bool attach(void *nativeView) {
		if (!isOnGuiThread() || !webview || !nativeView) return false;
		using namespace choc::objc;
		id parent = (id)nativeView;
		id subview = (id)webview->getViewHandle();
		call<void>(parent, "addSubview:", subview);
		return call<id>(subview, "superview") == parent;
	}
	void setSize(double width, double height) {
		if (!isOnGuiThread() || !webview) return;
		using namespace choc::objc;
		CGRect rect{{0, 0}, {CGFloat(width), CGFloat(height)}};
		id subview = (id)webview->getViewHandle();
		call<void>(subview, "setFrame:", rect);
	}
	void setVisible(bool visible) {
		if (!isOnGuiThread() || !webview) return;
		using namespace choc::objc;
		id subview = (id)webview->getViewHandle();
		call<void>(subview, "setHidden:", (BOOL) (!visible));
	}

	detail::ThreadAffinity uiThread;
	std::string bridgeToken = detail::makeBridgeToken();
	WebviewGui *main = nullptr;
	std::unique_ptr<choc::ui::WebView> webview;
};
#	elif CHOC_WINDOWS
struct WebviewGui::Impl {
	~Impl() {
		assert(!uiThread.isBound() || uiThread.isCurrentThread());
		webview.reset();
		comApartment.reset();
	}
	[[nodiscard]] bool isOnGuiThread() const noexcept { return uiThread.isCurrentThread(); }
	void init(const choc::ui::WebView::Options &options) {
		uiThread.bindToCurrentThread();
		comApartment = std::make_unique<detail::ScopedCOMApartment>();
		if (!comApartment->ok()) {
			comApartment.reset();
			return;
		}
		webview = std::make_unique<choc::ui::WebView>(options);
		if (!webview->loadedOK()) {
			webview.reset();
			comApartment.reset();
		}
	}
	bool attach(void *nativeParent) {
		if (!isOnGuiThread() || !webview || !nativeParent) return false;
		return detail::attachChildWindowToHost(static_cast<::HWND>(webview->getViewHandle()),
										 static_cast<::HWND>(nativeParent));
	}
	void setSize(double width, double height) {
		if (!isOnGuiThread() || !webview) return;
		detail::resizeChildWindow(static_cast<::HWND>(webview->getViewHandle()),
								 static_cast<int>(width), static_cast<int>(height));
	}
	void setVisible(bool visible) {
		if (!isOnGuiThread() || !webview) return;
		detail::setChildWindowVisible(static_cast<::HWND>(webview->getViewHandle()), visible);
	}

	detail::ThreadAffinity uiThread;
	std::unique_ptr<detail::ScopedCOMApartment> comApartment;
	std::string bridgeToken = detail::makeBridgeToken();
	WebviewGui *main = nullptr;
	std::unique_ptr<choc::ui::WebView> webview;
};
#	else
struct WebviewGui::Impl {
	~Impl() { assert(!uiThread.isBound() || uiThread.isCurrentThread()); }
	[[nodiscard]] bool isOnGuiThread() const noexcept { return uiThread.isCurrentThread(); }
	void init(const choc::ui::WebView::Options &options) {
		uiThread.bindToCurrentThread();
		webview = std::make_unique<choc::ui::WebView>(options);
	}
	bool attach(void *) { return false; }
	void setSize(double, double) {}
	void setVisible(bool) {}

	detail::ThreadAffinity uiThread;
	std::string bridgeToken = detail::makeBridgeToken();
	WebviewGui *main = nullptr;
	std::unique_ptr<choc::ui::WebView> webview;
};
#	endif

WebviewGui * WebviewGui::create(WebviewGui::Platform p, const std::string &startPath, WebviewGui::ResourceGetter getter) {
	if (!supports(p) || !detail::isSafePluginStartPath(startPath)) return nullptr;

	auto *impl = new WebviewGui::Impl();
	if (!detail::isBridgeToken(impl->bridgeToken)) {
		delete impl;
		return nullptr;
	}

	choc::ui::WebView::Options options;
	options.acceptsFirstMouseClick = false;
	options.enableDefaultClipboardKeyShortcutsInSafari = false;
	options.transparentBackground = true;
#	if CHOC_WINDOWS
	options.customSchemeURI = "https://choc.localhost/";
	const std::string trustedOrigin = "https://choc.localhost";
#	else
	options.customSchemeURI = "choc://choc.choc/";
	const std::string trustedOrigin = "choc://choc.choc";
#	endif

	auto startUri = detail::appendBridgeTokenToURL(options.customSchemeURI + startPath, impl->bridgeToken);
	if (startUri.empty()) {
		delete impl;
		return nullptr;
	}

	options.fetchResource = [getter, impl](const std::string &path) {
		using ChocResource = choc::ui::WebView::Options::Resource;
		std::optional<ChocResource> chocResource;
		if (!impl->isOnGuiThread()) return chocResource;

		Resource resource;
		if (!getter || !getter(path.c_str(), resource)) return chocResource;
		if (!detail::resourceSizeAllowed(resource.bytes.size())) return chocResource;
		if (resource.mediaType.empty()) resource.mediaType = helpers::guessMediaType(path.c_str());

		if (detail::startsWithASCIIInsensitive(resource.mediaType, "text/html")) {
			if (!detail::applyPluginHTMLHardening(resource.bytes))
				return chocResource;
		}

		chocResource.emplace();
		chocResource->data = std::move(resource.bytes);
		chocResource->mimeType = std::move(resource.mediaType);
		return chocResource;
	};

	options.webviewIsReady = [startUri, trustedOrigin, impl](choc::ui::WebView &wv){
		if (!impl->isOnGuiThread()) return;

		// Defense in depth only: bridge authorization does not rely on this script.
		// It promptly removes accidentally navigated remote content from the view.
		const auto guardScript = std::string("(()=>{const u=location.href;if(u==='about:blank'||u.toLowerCase().startsWith('")
			+ trustedOrigin
			+ "'))return;window.stop();location.replace('about:blank');})()";
		wv.addInitScript(guardScript);

		wv.bind("_WebviewGui_receive64", [impl](const choc::value::ValueView& args){
			if (!impl->isOnGuiThread()) return choc::value::Value{false};
			if (!args.isArray() || args.size() != 2) return choc::value::Value{false};

			const auto suppliedToken = args[0].getString();
			if (!detail::constantTimeTokenEquals(impl->bridgeToken, suppliedToken))
				return choc::value::Value{false};

			const auto base64 = args[1].getString();
			if (!detail::base64MessageSizeAllowed(base64.size()))
				return choc::value::Value{false};

			std::vector<unsigned char> bytes;
			choc::base64::decodeToContainer(bytes, base64);
			if (!detail::messageSizeAllowed(bytes.size()))
				return choc::value::Value{false};

			auto *gui = impl->main;
			if (gui && gui->receive)
				gui->receive(bytes.data(), bytes.size());

			return choc::value::Value{true};
		});

		wv.navigate(startUri);
	};

	impl->init(options);
	if (!impl->webview || !impl->webview->loadedOK()) {
		delete impl;
		return nullptr;
	}

	return new WebviewGui(impl);
}

WebviewGui * WebviewGui::create(WebviewGui::Platform p, const std::string &startUrl) {
	if (!detail::isSafePluginStartPath(startUrl)) return nullptr;
	return create(p, startUrl, [](const char *, Resource &){ return false; });
}

WebviewGui * WebviewGui::create(WebviewGui::Platform p, const std::string &startPath, const std::string &baseDir) {
	if (baseDir.empty() || !detail::isSafePluginStartPath(startPath)) return nullptr;
	return create(p, startPath, [baseDir](const char *path, Resource &resource){
		std::filesystem::path resolved;
		if (!detail::resolveContainedExistingPath(std::filesystem::path(baseDir),
											  path ? std::string_view(path) : std::string_view{}, resolved))
			return false;

		std::ifstream fileStream{resolved, std::ios::binary | std::ios::ate};
		if (!fileStream) return false;
		const auto end = fileStream.tellg();
		if (end < 0) return false;
		const auto length = static_cast<size_t>(end);
		if (!detail::resourceSizeAllowed(length)) return false;
		resource.bytes.resize(length);
		fileStream.seekg(0);
		fileStream.read(reinterpret_cast<char *>(resource.bytes.data()), static_cast<std::streamsize>(length));
		return bool(fileStream);
	});
}

WebviewGui::WebviewGui(WebviewGui::Impl *impl) : impl(impl) { impl->main = this; }

WebviewGui::~WebviewGui() {
	if (impl) {
		assert(impl->isOnGuiThread());
		impl->main = nullptr;
	}
	delete impl;
}

bool WebviewGui::supports(WebviewGui::Platform p) {
#	if CHOC_APPLE
	return p == COCOA;
#	else
	(void)p;
	return false;
#	endif
}

bool WebviewGui::attach(void *platformNative) {
	return impl && impl->isOnGuiThread() && impl->attach(platformNative);
}

void WebviewGui::send(const unsigned char *bytes, size_t length) {
	if (!impl || !impl->isOnGuiThread() || !impl->webview
		|| (!bytes && length != 0) || !detail::messageSizeAllowed(length)) return;

	const auto functionName = detail::bridgeSendFunctionName(impl->bridgeToken);
	if (functionName.empty()) return;
	const auto base64 = choc::base64::encodeToString(bytes, length);
	impl->webview->evaluateJavascript("if(typeof window['" + functionName
		+ "']==='function')window['" + functionName + "'](\"" + base64 + "\");");
}

void WebviewGui::setSize(double width, double height) {
	if (impl && impl->isOnGuiThread()) impl->setSize(width, height);
}

void WebviewGui::setVisible(bool visible) {
	if (impl && impl->isOnGuiThread()) impl->setVisible(visible);
}

} // namespace

#endif