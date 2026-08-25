#include "webview-gui/_impl/platform/choc_plugin_webview.h"
#include "webview-gui/_impl/platform/linux_plugin_runtime.h"

#if !defined(__linux__)
#error Linux-only test module
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#define WEBVIEW_GUI_TEST_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

struct ModuleState {
    // Bridge callbacks capture this state. Keep it alive until all WebViews and
    // their native script-message handlers have been destroyed.
    std::vector<std::size_t> receivedMessageCounts;
    std::vector<bool> bridgeInstalled;
    bool acceptingMessages = false;
    bool unexpectedMessage = false;
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

template <typename Predicate>
bool waitFor(Predicate&& predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        pumpEvents();
        if (!predicate())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    pumpEvents();
    return predicate();
}

void releaseState()
{
    auto*& state = stateSlot();
    if (!state)
        return;

    state->acceptingMessages = false;

    // GtkXEmbedHost owns GtkPlug objects that reference the CHOC WebKit child.
    // Detach all plugs while both the WebViews and this module's code are still
    // alive, then destroy the WebViews (and their bridge callbacks) before the
    // callback state they capture, then finally release the state itself.
    state->adapters.clear();
    pumpEvents();
    state->views.clear();
    pumpEvents();
    state->receivedMessageCounts.clear();
    state->bridgeInstalled.clear();
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

bool waitForJavascriptBarrier(choc::ui::WebView& view)
{
    struct BarrierState {
        bool completed = false;
        bool failed = false;
    };

    auto barrier = std::make_shared<BarrierState>();
    if (!view.evaluateJavascript(
            "0",
            [barrier](const std::string& error, const choc::value::ValueView&)
            {
                barrier->failed = !error.empty();
                barrier->completed = true;
            }))
        return false;

    return waitFor([&] { return barrier->completed; }) && !barrier->failed;
}

bool installBridgeBinding(ModuleState& state, std::size_t index)
{
    if (index >= state.views.size() || index >= state.bridgeInstalled.size())
        return false;
    if (state.bridgeInstalled[index])
        return true;

    auto& view = state.views[index];
    if (!view || !view->loadedOK() || !view->getViewHandle())
        return false;

    if (!view->bind("__webviewGuiLinuxIsolationMessage",
                    [&state, index](const choc::value::ValueView&)
                    {
                        if (index >= state.receivedMessageCounts.size()
                            || !state.acceptingMessages) {
                            state.unexpectedMessage = true;
                            return choc::value::Value{false};
                        }

                        ++state.receivedMessageCounts[index];
                        return choc::value::Value{true};
                    }))
        return false;

    // bind() queues its installation script. A completion-bearing evaluation
    // behind it serialises WebKit startup and proves the binding exists before
    // this endpoint participates in the isolation test.
    if (!waitForJavascriptBarrier(*view))
        return false;

    state.bridgeInstalled[index] = true;
    return true;
}

bool exchangeMessages(ModuleState& state, std::size_t messagesPerView)
{
    if (state.views.empty() || messagesPerView == 0
        || state.receivedMessageCounts.size() != state.views.size()
        || state.bridgeInstalled.size() != state.views.size()
        || state.unexpectedMessage)
        return false;

    // Keep all 32 editors alive and embedded, but activate two independent JS
    // endpoints per module. That is enough to detect cross-instance/module
    // routing while avoiding 32 simultaneous WebKit content-process startups in
    // the multi-module lifecycle gate.
    const auto activeViews = std::min<std::size_t>(2, state.views.size());
    for (std::size_t i = 0; i < activeViews; ++i) {
        if (!installBridgeBinding(state, i))
            return false;
    }

    const auto before = state.receivedMessageCounts;
    state.acceptingMessages = true;

    bool allQueued = true;
    for (std::size_t i = 0; i < activeViews; ++i) {
        auto& view = state.views[i];
        for (std::size_t message = 0; message < messagesPerView; ++message) {
            if (!view->evaluateJavascript(
                    "if(typeof window.__webviewGuiLinuxIsolationMessage==='function')"
                    "window.__webviewGuiLinuxIsolationMessage();"))
                allQueued = false;
        }
    }

    const auto receivedExactlyExpected = [&]
    {
        if (state.unexpectedMessage)
            return false;
        for (std::size_t i = 0; i < state.receivedMessageCounts.size(); ++i) {
            const auto expected = before[i] + (i < activeViews ? messagesPerView : 0);
            if (state.receivedMessageCounts[i] != expected)
                return false;
        }
        return true;
    };

    const bool messagesOK = allQueued && waitFor(receivedExactlyExpected);
    state.acceptingMessages = false;

    // CHOC responds to each native binding call by queueing JavaScript after the
    // C++ callback returns. Drain every activated view so no module-owned async
    // work can cross releaseState()/dlclose().
    bool drainsOK = true;
    for (std::size_t i = 0; i < activeViews; ++i) {
        if (!state.views[i] || !waitForJavascriptBarrier(*state.views[i]))
            drainsOK = false;
    }

    return messagesOK
        && drainsOK
        && !state.unexpectedMessage
        && receivedExactlyExpected();
}

} // namespace

WEBVIEW_GUI_TEST_EXPORT bool webview_gui_test_retain_linux_webviews(std::size_t count)
{
    releaseState();

    auto* state = new (std::nothrow) ModuleState;
    if (!state)
        return false;

    stateSlot() = state;
    state->receivedMessageCounts.assign(count, 0);
    state->bridgeInstalled.assign(count, false);
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

WEBVIEW_GUI_TEST_EXPORT bool webview_gui_test_exchange_linux_messages(
    std::size_t messagesPerView)
{
    auto* state = stateSlot();
    return state != nullptr && exchangeMessages(*state, messagesPerView);
}

WEBVIEW_GUI_TEST_EXPORT void webview_gui_test_release_linux_webviews()
{
    releaseState();
}
