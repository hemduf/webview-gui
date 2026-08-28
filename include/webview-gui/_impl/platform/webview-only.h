#pragma once

namespace webview_gui {

// Deliberate WebView-only backend used by WCLAP/WebAssembly builds.
//
// A WCLAP cannot create or attach Cocoa/HWND/X11 views inside the WASM module.
// The CLAP adapter instead negotiates CLAP_WINDOW_API_WEBVIEW with the host and
// exchanges resources/messages through clap.webview/3. Keeping these native
// operations as explicit no-ops prevents CHOC or OS GUI dependencies from being
// pulled into the module while making the supported WASM mode distinguishable
// from an unknown/unsupported native platform.
struct WebviewGui::Impl {};

bool WebviewGui::supports(Platform) {
    return false;
}

WebviewGui * WebviewGui::create(Platform, const std::string &) {
    return nullptr;
}

WebviewGui * WebviewGui::create(Platform, const std::string &, const std::string &) {
    return nullptr;
}

WebviewGui * WebviewGui::create(Platform, const std::string &, ResourceGetter) {
    return nullptr;
}

WebviewGui::WebviewGui(WebviewGui::Impl *) {}
WebviewGui::~WebviewGui() {}
bool WebviewGui::attach(void *) { return false; }
void WebviewGui::send(const unsigned char *, size_t) {}
void WebviewGui::setSize(double, double) {}
void WebviewGui::setVisible(bool) {}

} // namespace webview_gui
