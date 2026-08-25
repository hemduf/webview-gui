#include "webview-gui/_impl/platform/choc_plugin_webview.h"
#include "webview-gui/_impl/platform/linux_plugin_runtime.h"

#if !defined(__linux__)
#error Linux-only test module
#endif

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

#define WEBVIEW_GUI_TEST_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

struct ModuleState {
    std::vector<std::unique_ptr<choc::ui::WebView>> views;
    std::vector<std::unique_ptr<webview_gui::detail::GtkXEmbedHost>> adapters;
};

ModuleState*& stateSlot()
{
    // Keep only a trivial pointer in DSO static storage. The pointed-to C++
    // state is explicitly destroyed by the host before dlclose().
    static ModuleState* state = nullptr;
    return state;
}

void pumpEvents()
{
    while (g_main_context_pending(nullptr))
        g_main_context_iteration(nullptr, FALSE);
}

void releaseState()
{
    auto*& state = stateSlot();
    if (!state)
        return;

    // GtkXEmbedHost owns GtkPlug objects that reference the CHOC WebKit child.
    // Detach all plugs while both the WebViews and this module's code are still
    // alive, then destroy the WebViews, then finally release the state itself.
    state->adapters.clear();
    pumpEvents();
    state->views.clear();
    pumpEvents();
    delete state;
    state = nullptr;
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
    state->adapters.reserve(count);

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

WEBVIEW_GUI_TEST_EXPORT bool webview_gui_test_exercise_linux_host_lifecycle(
    const std::uintptr_t* hostXids,
    std::size_t hostCount,
    std::size_t passes)
{
    auto* state = stateSlot();
    if (!state || !hostXids || passes == 0 || hostCount != state->views.size())
        return false;

    if (state->adapters.empty()) {
        for (std::size_t i = 0; i < state->views.size(); ++i) {
            auto& view = state->views[i];
            if (!view || !view->loadedOK() || !view->getViewHandle() || hostXids[i] == 0)
                return false;

            auto adapter = std::make_unique<webview_gui::detail::GtkXEmbedHost>();
            auto* child = static_cast<GtkWidget*>(view->getViewHandle());
            if (!adapter->attach(child, hostXids[i]) || !adapter->plugWidget())
                return false;

            // The dedicated XEmbed tests assert the asynchronous GtkSocket /
            // GtkPlug negotiation and final child allocation. This multi-module
            // stress test intentionally uses synchronous GTK ownership
            // invariants so 32 simultaneous editor windows do not serialize on
            // per-editor Xvfb round-trip timeouts.
            if (!gtk_widget_get_realized(adapter->plugWidget())
                || gtk_widget_get_parent(child) != adapter->plugWidget())
                return false;

            state->adapters.push_back(std::move(adapter));
        }
        pumpEvents();
    }

    if (state->adapters.size() != state->views.size())
        return false;

    for (std::size_t pass = 0; pass < passes; ++pass) {
        for (std::size_t i = 0; i < state->views.size(); ++i) {
            auto& view = state->views[i];
            auto& adapter = state->adapters[i];
            if (!view || !adapter || !view->loadedOK() || !view->getViewHandle()
                || !adapter->plugWidget())
                return false;

            auto* child = static_cast<GtkWidget*>(view->getViewHandle());
            const int width = 480 + static_cast<int>((i + pass) % 17) * 8;
            const int height = 280 + static_cast<int>((i + pass) % 13) * 6;
            if (!adapter->resize(width, height))
                return false;

            int requestedWidth = 0;
            int requestedHeight = 0;
            gtk_widget_get_size_request(child, &requestedWidth, &requestedHeight);
            if (requestedWidth != width || requestedHeight != height)
                return false;

            if (!adapter->setVisible(false)
                || gtk_widget_get_visible(adapter->plugWidget()))
                return false;
            if (!adapter->setVisible(true)
                || !gtk_widget_get_visible(adapter->plugWidget()))
                return false;
        }
        pumpEvents();
    }

    return true;
}

WEBVIEW_GUI_TEST_EXPORT void webview_gui_test_release_linux_webviews()
{
    releaseState();
}
