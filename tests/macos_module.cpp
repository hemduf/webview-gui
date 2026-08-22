#include "choc/gui/choc_WebView.h"

#if !defined(__APPLE__)
#error macOS-only test module
#endif

#include <objc/runtime.h>
#include <cstring>

extern "C" __attribute__((visibility("default"))) const char* webview_gui_test_create_webview_class_name()
{
    static char className[256] = {};
    className[0] = '\0';

    choc::ui::WebView::Options options;
    options.acceptsFirstMouseClick = false;
    options.enableDefaultClipboardKeyShortcutsInSafari = false;
    options.transparentBackground = true;

    choc::ui::WebView view{options};
    if (!view.loadedOK() || !view.getViewHandle())
        return className;

    auto object = reinterpret_cast<id>(view.getViewHandle());
    auto cls = object_getClass(object);
    if (cls) {
        const char* name = class_getName(cls);
        if (name) {
            std::strncpy(className, name, sizeof(className) - 1);
            className[sizeof(className) - 1] = '\0';
        }
    }

    return className;
}
