#pragma once

// Explicit backend marker for WCLAP/WebAssembly builds. The browser/WebView is
// owned by the host through clap.webview/3; the WASM module must not instantiate
// or link any native Cocoa/HWND/X11 WebView backend.
#define WEBVIEW_GUI_WEBVIEW_ONLY_BACKEND 1

namespace webview_gui {

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

WebviewGui * WebviewGui::create(Platform, const std::string &, ResourceGetter, const std::string &) {
    return nullptr;
}

WebviewGui::WebviewGui(WebviewGui::Impl *) {}
WebviewGui::~WebviewGui() {}
bool WebviewGui::attach(void *) { return false; }
void WebviewGui::send(const unsigned char *, size_t) {}
bool WebviewGui::trySetSize(double, double) { return false; }
void WebviewGui::setSize(double width, double height) { (void) trySetSize(width, height); }
void WebviewGui::setVisible(bool) {}

} // namespace webview_gui
