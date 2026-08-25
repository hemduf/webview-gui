#include "webview-gui/_impl/platform/choc_plugin_webview.h"

#if !defined(__linux__)
#error Linux-only test module
#endif

#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

#define WEBVIEW_GUI_TEST_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

struct ModuleState {
    std::vector<std::unique_ptr<choc::ui::WebView>> views;
};

ModuleState*& stateSlot()
{
    // Keep only a trivial pointer in DSO static storage. The pointed-to C++
    // state is explicitly destroyed by the host before dlclose().
    static ModuleState* state = nullptr;
    return state;
}

void releaseState()
{
    delete stateSlot();
    stateSlot() = nullptr;
}

choc::ui::WebView::Options makeWebViewOptions()
{
    choc::ui::WebView::Options options;
    options.customSchemeURI = "choc://choc.choc/";
    options.fetchResource = [](const std::string& path)
        -> std::optional<choc::ui::WebView::Options::Resource>
    {
        if (path == "/" || path == "/index.html")
            return choc::ui::WebView::Options::Resource{
                "<!doctype html><html><body>webview-gui linux module</body></html>",
                "text/html"};
        return std::nullopt;
    };
    return options;
}

} // namespace

WEBVIEW_GUI_TEST_EXPORT bool webview_gui_test_retain_linux_webviews(std::size_t count)
{
    releaseState();

    auto* state = new (std::nothrow) ModuleState;
    if (!state)
        return false;

    stateSlot() = state;
    state->views.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        auto view = std::make_unique<choc::ui::WebView>(makeWebViewOptions());
        if (!view->loadedOK() || !view->getViewHandle()) {
            releaseState();
            return false;
        }
        state->views.push_back(std::move(view));
    }

    return state->views.size() == count;
}

WEBVIEW_GUI_TEST_EXPORT void webview_gui_test_release_linux_webviews()
{
    releaseState();
}
