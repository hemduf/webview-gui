#include "webview-gui/_impl/platform/choc_plugin_webview.h"

#if !defined(__APPLE__)
#error macOS-only test module
#endif

#include <objc/runtime.h>
#include <cstring>
#include <cstddef>

namespace {

void copyClassName(id object, char* output, std::size_t capacity)
{
    if (!output || capacity == 0) return;
    output[0] = '\0';
    if (!object) return;

    auto cls = object_getClass(object);
    const char* name = cls ? class_getName(cls) : nullptr;
    if (!name) return;

    std::strncpy(output, name, capacity - 1);
    output[capacity - 1] = '\0';
}

} // namespace

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_create_runtime_class_names(
    char* webviewClass,
    std::size_t webviewCapacity,
    char* delegateClass,
    std::size_t delegateCapacity)
{
    choc::ui::WebView::Options options;
    options.acceptsFirstMouseClick = false;
    options.enableDefaultClipboardKeyShortcutsInSafari = false;
    options.transparentBackground = true;

    choc::ui::WebView view{options};
    if (!view.loadedOK() || !view.getViewHandle())
        return false;

    auto webview = reinterpret_cast<id>(view.getViewHandle());
    auto delegate = choc::objc::call<id>(webview, "navigationDelegate");

    copyClassName(webview, webviewClass, webviewCapacity);
    copyClassName(delegate, delegateClass, delegateCapacity);
    return webviewClass && webviewClass[0] != '\0'
        && delegateClass && delegateClass[0] != '\0';
}
