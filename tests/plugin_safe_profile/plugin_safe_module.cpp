#include "webview-gui/webview-gui.h"

#ifndef WEBVIEW_GUI_TEST_MODULE_VARIANT
#define WEBVIEW_GUI_TEST_MODULE_VARIANT 0
#endif
#ifndef WEBVIEW_GUI_TEST_WEBVIEW_REVISION
#define WEBVIEW_GUI_TEST_WEBVIEW_REVISION "unknown"
#endif
#ifndef WEBVIEW_GUI_TEST_CHOC_REVISION
#define WEBVIEW_GUI_TEST_CHOC_REVISION "unknown"
#endif

#if defined(_WIN32)
#define WEBVIEW_GUI_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define WEBVIEW_GUI_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

struct WebviewGuiPluginTestInfo {
    unsigned int abiVersion;
    int variant;
    int supportsNone;
    const char* webviewRevision;
    const char* chocRevision;
};

// Model a CLAP/VST3/AU wrapper entry point: this is intentionally the only
// repository-owned symbol that the qualification module is allowed to export.
// Returning immutable module-local metadata lets the host prove that two loaded
// modules were compiled from different pinned source/CHOC revisions without
// exporting any webview-gui implementation detail.
WEBVIEW_GUI_TEST_EXPORT const WebviewGuiPluginTestInfo* webview_gui_plugin_test_entry()
{
    static const WebviewGuiPluginTestInfo info {
        1u,
        WEBVIEW_GUI_TEST_MODULE_VARIANT,
        webview_gui::WebviewGui::supports(webview_gui::WebviewGui::NONE) ? 1 : 0,
        WEBVIEW_GUI_TEST_WEBVIEW_REVISION,
        WEBVIEW_GUI_TEST_CHOC_REVISION,
    };
    return &info;
}
