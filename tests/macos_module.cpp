#include "choc/gui/choc_WebView.h"

#if !defined(__APPLE__)
#error macOS-only test module
#endif

#include <objc/runtime.h>
#include <string>

extern "C" __attribute__((visibility("default"))) const char* webview_gui_test_create_webview_class_name()
{
    static thread_local std::string className;
    className.clear();

    choc::ui::WebView::Options options;
    options.acceptsFirstMouseClick = false;
    options.enableDefaultClipboardKeyShortcutsInSafari = false;
    options.transparentBackground = true;

    choc::ui::WebView view{options};
    if (!view.loadedOK() || !view.getViewHandle())
        return "";

    auto object = reinterpret_cast<id>(view.getViewHandle());
    auto cls = object_getClass(object);
    if (cls)
        className = class_getName(cls);

    return className.c_str();
}
