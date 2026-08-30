#include "webview-gui/webview-gui.h"
#include "webview-gui/_impl/webview-gui.hxx"

#ifndef WEBVIEW_GUI_WEBVIEW_ONLY_BACKEND
#error "WASM/WCLAP must select the WebView-only backend instead of the generic unsupported backend"
#endif

int main()
{
    using webview_gui::WebviewGui;

    if (WebviewGui::supports(WebviewGui::COCOA) ||
        WebviewGui::supports(WebviewGui::HWND) ||
        WebviewGui::supports(WebviewGui::X11EMBED))
        return 1;

    if (WebviewGui::create(WebviewGui::COCOA, "/index.html") != nullptr ||
        WebviewGui::create(WebviewGui::HWND, "/index.html") != nullptr ||
        WebviewGui::create(WebviewGui::X11EMBED, "/index.html") != nullptr)
        return 2;

    return 0;
}
