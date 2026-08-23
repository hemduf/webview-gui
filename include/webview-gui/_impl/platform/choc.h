#include "../../helpers.h"
#include "../plugin_support.h"

#include "choc/platform/choc_Platform.h"

#if !CHOC_APPLE && !CHOC_WINDOWS && !CHOC_LINUX
#	include "./not-supported.h"
#else
#	include "./choc_plugin_webview.h"
#	include "choc/memory/choc_Base64.h"

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
		using namespace choc::objc;
		if (webview) {
			id subview = (id)webview->getViewHandle();
			call<void>(subview, "removeFromSuperview");
		}
	}
	
	void init(const choc::ui::WebView::Options &options) {
		webview = std::make_unique<choc::ui::WebView>(options);
	}
	
	void attach(void *nativeView) {
		if (!webview || !nativeView) return;
		using namespace choc::objc;
		id parent = (id)nativeView;
		id subview = (id)webview->getViewHandle();
		call<void>(parent, "addSubview:", subview);
	}

	void setSize(double width, double height) {
		if (!webview) return;
		using namespace choc::objc;
		CGRect rect{{0, 0}, {CGFloat(width), CGFloat(height)}};
		id subview = (id)webview->getViewHandle();
		call<void>(subview, "setFrame:", rect);
	}

	void setVisible(bool visible) {
		if (!webview) return;
		using namespace choc::objc;
		id subview = (id)webview->getViewHandle();
		call<void>(subview, "setHidden:", getNSNumberBool(!visible));
	}

	WebviewGui *main = nullptr;
	std::unique_ptr<choc::ui::WebView> webview;
};
#	else
struct WebviewGui::Impl {
	void init(const choc::ui::WebView::Options &options) {
		webview = std::make_unique<choc::ui::WebView>(options);
	}

	// Native embedding for Windows/X11 is intentionally not claimed as supported
	// until #11 implements and qualifies the parent/resize/focus glue.
	void attach(void *) {}
	void setSize(double, double) {}
	void setVisible(bool) {}

	WebviewGui *main = nullptr;
	std::unique_ptr<choc::ui::WebView> webview;
};
#	endif

WebviewGui * WebviewGui::create(WebviewGui::Platform p, const std::string &startPath, WebviewGui::ResourceGetter getter) {
	if (!supports(p)) return nullptr;

	auto *impl = new WebviewGui::Impl();
	
	choc::ui::WebView::Options options;
	// The plugin-safe macOS CHOC adapter intentionally uses vanilla WKWebView.
	// These two features are normally implemented by CHOC's dynamic WKWebView
	// subclass, which cannot safely outlive an unloadable plugin image.
	options.acceptsFirstMouseClick = false;
	options.enableDefaultClipboardKeyShortcutsInSafari = false;
	options.transparentBackground = true;
#	if CHOC_WINDOWS
	options.customSchemeURI = "https://choc.localhost/";
#	else
	options.customSchemeURI = "choc://choc.choc/";
#	endif
	auto startUri = options.customSchemeURI + startPath;

	options.fetchResource = [getter](const std::string &path) {
		using ChocResource = choc::ui::WebView::Options::Resource;
		std::optional<ChocResource> chocResource;
		Resource resource;
		if (getter && getter(path.c_str(), resource)) {
			chocResource.emplace();
			chocResource->data = std::move(resource.bytes);
			chocResource->mimeType = resource.mediaType.empty()
				? helpers::guessMediaType(path.c_str())
				: std::move(resource.mediaType);
		}
		return chocResource;
	};

	options.webviewIsReady = [startUri, impl](choc::ui::WebView &wv){
		wv.addInitScript(R"jsCode(
			if (!Uint8Array.prototype.toBase64) {
				Uint8Array.prototype.toBase64 = function() {
					let binaryString = "";
					for (let i = 0; i < this.length; ++i)
						binaryString += String.fromCharCode(this[i]);
					return btoa(binaryString);
				};
			}
			if (!Uint8Array.fromBase64) {
				Uint8Array.fromBase64 = b64 => {
					const binaryString = atob(b64);
					const array = new Uint8Array(binaryString.length);
					for (let i = 0; i < binaryString.length; ++i)
						array[i] = binaryString.charCodeAt(i);
					return array;
				};
			}
			window.addEventListener('message', e => {
				if (e.source == window) {
					e.stopImmediatePropagation();
					let data = e.data;
					if (data instanceof ArrayBuffer) data = new Uint8Array(data);
					_WebviewGui_receive64(new Uint8Array(data).toBase64());
				}
			}, {capture: true});
			function _WebviewGui_send64(b64) {
				window.dispatchEvent(new MessageEvent('message', {
					data: Uint8Array.fromBase64(b64).buffer
				}));
			}
		)jsCode");

		wv.bind("_WebviewGui_receive64", [impl](const choc::value::ValueView& args){
			auto *gui = impl->main;
			if (gui && gui->receive && args.isArray() && args.size() == 1) {
				auto base64 = args[0].getString();
				std::vector<unsigned char> bytes;
				choc::base64::decodeToContainer(bytes, base64);
				gui->receive(bytes.data(), bytes.size());
			}
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
	return create(p, startUrl, [](const char *, Resource &){ return false; });
}

WebviewGui * WebviewGui::create(WebviewGui::Platform p, const std::string &startPath, const std::string &baseDir) {
	if (baseDir.empty()) return nullptr;

	return create(p, startPath, [baseDir](const char *path, Resource &resource){
		std::filesystem::path resolved;
		if (!detail::resolveContainedPath(std::filesystem::path(baseDir),
										 std::filesystem::path(path ? path : ""), resolved))
			return false;

		std::ifstream fileStream{resolved, std::ios::binary | std::ios::ate};
		if (!fileStream) return false;

		const auto end = fileStream.tellg();
		if (end < 0) return false;
		const auto length = static_cast<size_t>(end);
		resource.bytes.resize(length);
		fileStream.seekg(0);
		fileStream.read(reinterpret_cast<char *>(resource.bytes.data()), static_cast<std::streamsize>(length));
		return bool(fileStream);
	});
}

WebviewGui::WebviewGui(WebviewGui::Impl *impl) : impl(impl) {
	impl->main = this;
}

WebviewGui::~WebviewGui() {
	if (impl) impl->main = nullptr;
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

void WebviewGui::attach(void *platformNative) {
	if (impl) impl->attach(platformNative);
}

void WebviewGui::send(const unsigned char *bytes, size_t length) {
	if (!impl || !impl->webview || (!bytes && length != 0)) return;
	auto base64 = choc::base64::encodeToString(bytes, length);
	impl->webview->evaluateJavascript("_WebviewGui_send64(\"" + base64 + "\");");
}

void WebviewGui::setSize(double width, double height) {
	if (impl) impl->setSize(width, height);
}

void WebviewGui::setVisible(bool visible) {
	if (impl) impl->setVisible(visible);
}

} // namespace

#endif
