#include "webview-gui/_impl/platform/choc_plugin_webview.h"

#if !defined(__APPLE__)
#error macOS-only test module
#endif

#include <objc/runtime.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

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

choc::ui::WebView::Options makePluginOptions()
{
    choc::ui::WebView::Options options;
    options.acceptsFirstMouseClick = false;
    options.enableDefaultClipboardKeyShortcutsInSafari = false;
    options.transparentBackground = true;
    return options;
}

struct RetainedWebViewsState {
    std::vector<std::unique_ptr<choc::ui::WebView>> views;
};

bool retainWebViews(RetainedWebViewsState& state,
                    std::size_t count,
                    char* delegateClass,
                    std::size_t delegateCapacity)
{
    auto& views = state.views;
    views.clear();
    views.reserve(count);

    char firstDelegate[256] = {};

    for (std::size_t i = 0; i < count; ++i) {
        auto view = std::make_unique<choc::ui::WebView>(makePluginOptions());
        if (!view->loadedOK() || !view->getViewHandle()) {
            views.clear();
            return false;
        }

        auto webview = reinterpret_cast<id>(view->getViewHandle());
        auto webviewClass = object_getClass(webview);
        if (webviewClass != objc_getClass("WKWebView")) {
            views.clear();
            return false;
        }

        auto delegate = choc::objc::call<id>(webview, "navigationDelegate");
        char currentDelegate[256] = {};
        copyClassName(delegate, currentDelegate, sizeof(currentDelegate));
        if (currentDelegate[0] == '\0') {
            views.clear();
            return false;
        }

        if (i == 0) {
            std::strncpy(firstDelegate, currentDelegate, sizeof(firstDelegate) - 1);
        } else if (std::strcmp(firstDelegate, currentDelegate) != 0) {
            views.clear();
            return false;
        }

        views.push_back(std::move(view));
    }

    if (delegateClass && delegateCapacity > 0) {
        std::strncpy(delegateClass, firstDelegate, delegateCapacity - 1);
        delegateClass[delegateCapacity - 1] = '\0';
    }

    return count == 0 || firstDelegate[0] != '\0';
}

} // namespace

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_create_runtime_class_names(
    char* webviewClass,
    std::size_t webviewCapacity,
    char* delegateClass,
    std::size_t delegateCapacity)
{
    choc::ui::WebView view{makePluginOptions()};
    if (!view.loadedOK() || !view.getViewHandle())
        return false;

    auto webview = reinterpret_cast<id>(view.getViewHandle());
    auto delegate = choc::objc::call<id>(webview, "navigationDelegate");

    copyClassName(webview, webviewClass, webviewCapacity);
    copyClassName(delegate, delegateClass, delegateCapacity);
    return webviewClass && webviewClass[0] != '\0'
        && delegateClass && delegateClass[0] != '\0';
}

extern "C" __attribute__((visibility("default"))) void* webview_gui_test_create_retained_state()
{
    return new (std::nothrow) RetainedWebViewsState{};
}

extern "C" __attribute__((visibility("default"))) void webview_gui_test_destroy_retained_state(
    void* opaqueState)
{
    delete static_cast<RetainedWebViewsState*>(opaqueState);
}

extern "C" __attribute__((visibility("default"))) bool webview_gui_test_retain_webviews(
    void* opaqueState,
    std::size_t count,
    char* delegateClass,
    std::size_t delegateCapacity)
{
    auto* state = static_cast<RetainedWebViewsState*>(opaqueState);
    return state != nullptr
        && retainWebViews(*state, count, delegateClass, delegateCapacity);
}

extern "C" __attribute__((visibility("default"))) void webview_gui_test_release_webviews(
    void* opaqueState)
{
    if (auto* state = static_cast<RetainedWebViewsState*>(opaqueState))
        state->views.clear();
}
