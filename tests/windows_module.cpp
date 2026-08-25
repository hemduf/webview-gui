#include "webview-gui/_impl/platform/windows_plugin_runtime.h"
#include "webview-gui/_impl/platform/choc_plugin_webview.h"

#if !defined(_WIN32)
#error Windows-only test module
#endif

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr auto bridgeStartupTimeout = std::chrono::seconds(20);

struct BridgeState {
    std::vector<std::size_t> receivedMessageCounts;
    std::vector<bool> bridgeInstalled;
    bool acceptingMessages = false;
    bool unexpectedMessage = false;
};

std::vector<std::unique_ptr<choc::ui::WebView>>& retainedViews()
{
    static std::vector<std::unique_ptr<choc::ui::WebView>> views;
    return views;
}

std::unique_ptr<webview_gui::detail::ScopedCOMApartment>& apartment()
{
    static std::unique_ptr<webview_gui::detail::ScopedCOMApartment> value;
    return value;
}

BridgeState& bridgeState()
{
    static BridgeState state;
    return state;
}

void pumpMessages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

template <typename Predicate>
bool waitFor(Predicate&& predicate,
             std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        pumpMessages();
        if (!predicate())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    pumpMessages();
    return predicate();
}

choc::ui::WebView::Options makeMessageWebViewOptions()
{
    choc::ui::WebView::Options options;
    options.fetchResource = [](const std::string& path)
        -> std::optional<choc::ui::WebView::Options::Resource>
    {
        if (path == "/" || path == "/index.html")
            return choc::ui::WebView::Options::Resource{
                "<!doctype html><html><body>webview-gui windows module</body></html>",
                "text/html"};
        return std::nullopt;
    };
    return options;
}

void resetBridgeState(std::size_t count)
{
    auto& state = bridgeState();
    state.receivedMessageCounts.assign(count, 0);
    state.bridgeInstalled.assign(count, false);
    state.acceptingMessages = false;
    state.unexpectedMessage = false;
}

void releaseRetainedState()
{
    auto& state = bridgeState();
    state.acceptingMessages = false;

    // Native bridge callbacks capture BridgeState. Destroy every WebView and its
    // WebView2 handlers before clearing the callback state or releasing COM.
    retainedViews().clear();
    state.receivedMessageCounts.clear();
    state.bridgeInstalled.clear();
    state.unexpectedMessage = false;
    apartment().reset();
}

bool waitForJavascriptBarrier(
    choc::ui::WebView& view,
    std::chrono::milliseconds timeout = std::chrono::seconds(5))
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

    return waitFor([&] { return barrier->completed; }, timeout) && !barrier->failed;
}

bool waitForTrustedDocument(choc::ui::WebView& view)
{
    const auto deadline = std::chrono::steady_clock::now() + bridgeStartupTimeout;

    while (std::chrono::steady_clock::now() < deadline) {
        struct ProbeState {
            bool completed = false;
            bool trusted = false;
        };

        auto probe = std::make_shared<ProbeState>();
        if (!view.evaluateJavascript(
                "if(window.location.origin!=='https://choc.localhost')"
                "throw new Error('webview-gui-not-ready'); 0;",
                [probe](const std::string& error, const choc::value::ValueView&)
                {
                    probe->trusted = error.empty();
                    probe->completed = true;
                })) {
            pumpMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Never issue a second ExecuteScript while a prior completion callback
        // may still be owned by WebView2. Keeping at most one probe in flight is
        // important for DLL unload: every callback object has code in this DSO.
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return false;

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if (!waitFor([&] { return probe->completed; }, remaining))
            return false;
        if (probe->trusted)
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

bool installBridgeBinding(BridgeState& state, std::size_t index)
{
    auto& views = retainedViews();
    if (index >= views.size() || index >= state.bridgeInstalled.size())
        return false;
    if (state.bridgeInstalled[index])
        return true;

    auto& view = views[index];
    if (!view || !view->loadedOK() || !view->getViewHandle())
        return false;
    if (!waitFor([&] { return view->isReady(); }, bridgeStartupTimeout)
        || !waitForTrustedDocument(*view))
        return false;

    if (!view->bind("__webviewGuiWindowsIsolationMessage",
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

    // bind() queues the JavaScript wrapper. A completion-bearing evaluation
    // behind it proves that the wrapper is installed in the trusted local page.
    if (!waitForJavascriptBarrier(*view, bridgeStartupTimeout))
        return false;

    state.bridgeInstalled[index] = true;
    return true;
}

bool exchangeMessages(std::size_t messagesPerView)
{
    auto& views = retainedViews();
    auto& state = bridgeState();
    if (views.empty() || messagesPerView == 0
        || state.receivedMessageCounts.size() != views.size()
        || state.bridgeInstalled.size() != views.size()
        || state.unexpectedMessage)
        return false;

    // Keep all sixteen editors in each DLL alive, but activate two independent
    // JS endpoints per module. Exact per-view counters catch instance or module
    // cross-routing while keeping the 200-cycle qualification runtime bounded.
    const auto activeViews = (std::min<std::size_t>)(2, views.size());
    for (std::size_t i = 0; i < activeViews; ++i) {
        if (!installBridgeBinding(state, i))
            return false;
    }

    const auto before = state.receivedMessageCounts;
    state.acceptingMessages = true;

    bool allQueued = true;
    for (std::size_t i = 0; i < activeViews; ++i) {
        auto& view = views[i];
        for (std::size_t message = 0; message < messagesPerView; ++message) {
            if (!view->evaluateJavascript(
                    "if(typeof window.__webviewGuiWindowsIsolationMessage==='function')"
                    "window.__webviewGuiWindowsIsolationMessage();"))
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

    // CHOC posts a JavaScript response after each native binding callback.
    // Drain every activated view before any DLL teardown can occur.
    bool drainsOK = true;
    for (std::size_t i = 0; i < activeViews; ++i) {
        if (!views[i] || !waitForJavascriptBarrier(*views[i]))
            drainsOK = false;
    }

    return messagesOK
        && drainsOK
        && !state.unexpectedMessage
        && receivedExactlyExpected();
}

void copyWide(const std::wstring& source, wchar_t* output, std::size_t capacity)
{
    if (!output || capacity == 0) return;
    const auto count = (std::min)(capacity - 1, source.size());
    std::wmemcpy(output, source.data(), count);
    output[count] = L'\0';
}

} // namespace

extern "C" __declspec(dllexport) bool webview_gui_test_retain_windows_webviews(
    std::size_t count,
    std::uintptr_t* moduleHandleOut,
    std::uintptr_t* firstWindowInstanceOut,
    wchar_t* firstClassName,
    std::size_t firstClassCapacity,
    bool* allOwnedByModule,
    bool* allClassNamesUnique)
{
    releaseRetainedState();

    apartment() = std::make_unique<webview_gui::detail::ScopedCOMApartment>();
    if (!apartment()->ok()) {
        apartment().reset();
        return false;
    }

    const auto module = webview_gui::detail::windowsPluginModuleHandle();
    if (!module) {
        apartment().reset();
        return false;
    }

    auto& views = retainedViews();
    resetBridgeState(count);

    std::set<std::wstring> classNames;
    bool owned = true;
    std::wstring firstClass;
    std::uintptr_t firstWindowInstance = 0;

    views.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto view = i < 2
            ? std::make_unique<choc::ui::WebView>(makeMessageWebViewOptions())
            : std::make_unique<choc::ui::WebView>();
        if (!view->loadedOK() || !view->getViewHandle()) {
            releaseRetainedState();
            return false;
        }

        auto hwnd = static_cast<HWND>(view->getViewHandle());
        const auto windowInstance = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        owned = owned && windowInstance == module;

        wchar_t className[256] = {};
        if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) <= 0) {
            releaseRetainedState();
            return false;
        }

        classNames.emplace(className);
        if (i == 0) {
            firstClass = className;
            firstWindowInstance = reinterpret_cast<std::uintptr_t>(windowInstance);
        }

        views.push_back(std::move(view));
    }

    if (moduleHandleOut)
        *moduleHandleOut = reinterpret_cast<std::uintptr_t>(module);
    if (firstWindowInstanceOut)
        *firstWindowInstanceOut = firstWindowInstance;
    copyWide(firstClass, firstClassName, firstClassCapacity);
    if (allOwnedByModule)
        *allOwnedByModule = owned;
    if (allClassNamesUnique)
        *allClassNamesUnique = classNames.size() == count;

    return count == 0 || (!firstClass.empty() && owned && classNames.size() == count);
}

extern "C" __declspec(dllexport) bool webview_gui_test_create_destroy_windows_webviews(
    std::size_t count)
{
    webview_gui::detail::ScopedCOMApartment localApartment;
    if (!localApartment.ok())
        return false;

    std::vector<std::unique_ptr<choc::ui::WebView>> views;
    views.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        auto view = std::make_unique<choc::ui::WebView>();
        if (!view->loadedOK() || !view->getViewHandle())
            return false;
        views.push_back(std::move(view));
    }

    // `views` is destroyed before localApartment because locals are unwound in
    // reverse declaration order, keeping every CHOC destruction on the same STA
    // that constructed it.
    return true;
}

extern "C" __declspec(dllexport) std::uintptr_t webview_gui_test_first_windows_hwnd()
{
    const auto& views = retainedViews();
    if (views.empty() || !views.front() || !views.front()->getViewHandle())
        return 0;

    return reinterpret_cast<std::uintptr_t>(views.front()->getViewHandle());
}

extern "C" __declspec(dllexport) bool webview_gui_test_exercise_windows_host_lifecycle(
    std::uintptr_t hostHandle,
    std::size_t passes)
{
    const auto host = reinterpret_cast<HWND>(hostHandle);
    auto& views = retainedViews();
    if (!IsWindow(host) || views.empty() || passes == 0)
        return false;

    for (std::size_t pass = 0; pass < passes; ++pass) {
        for (std::size_t i = 0; i < views.size(); ++i) {
            auto& view = views[i];
            if (!view || !view->loadedOK() || !view->getViewHandle())
                return false;

            const auto child = static_cast<HWND>(view->getViewHandle());
            if (!IsWindow(child))
                return false;

            if (GetParent(child) != host
                && !webview_gui::detail::attachChildWindowToHost(child, host))
                return false;
            if (GetParent(child) != host)
                return false;

            const int width = 480 + static_cast<int>((i + pass) % 17) * 8;
            const int height = 280 + static_cast<int>((i + pass) % 13) * 6;
            if (!webview_gui::detail::resizeChildWindow(child, width, height))
                return false;

            RECT rect{};
            if (!GetClientRect(child, &rect)
                || rect.right - rect.left != width
                || rect.bottom - rect.top != height)
                return false;

            if (!webview_gui::detail::setChildWindowVisible(child, false)
                || IsWindowVisible(child))
                return false;
            if (!webview_gui::detail::setChildWindowVisible(child, true)
                || !IsWindowVisible(child))
                return false;
        }
    }

    return true;
}

extern "C" __declspec(dllexport) bool webview_gui_test_exchange_windows_messages(
    std::size_t messagesPerView)
{
    return exchangeMessages(messagesPerView);
}

extern "C" __declspec(dllexport) void webview_gui_test_release_windows_webviews()
{
    releaseRetainedState();
}
