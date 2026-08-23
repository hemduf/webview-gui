#include "webview-gui/webview-gui.h"

#ifndef WEBVIEW_GUI_TEST_MODULE_VARIANT
#define WEBVIEW_GUI_TEST_MODULE_VARIANT 0
#endif

#if defined(_WIN32)
#define WEBVIEW_GUI_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define WEBVIEW_GUI_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Model a CLAP/VST3/AU wrapper entry point: this is intentionally the only
// repository-owned symbol that the qualification module is allowed to export.
WEBVIEW_GUI_TEST_EXPORT int webview_gui_plugin_test_entry()
{
    // Reference a non-inline webview-gui function so the private static archive
    // is actually pulled into this module before its export surface is scanned.
    return webview_gui::WebviewGui::supports(webview_gui::WebviewGui::NONE)
        ? 100 + WEBVIEW_GUI_TEST_MODULE_VARIANT
        : WEBVIEW_GUI_TEST_MODULE_VARIANT;
}
