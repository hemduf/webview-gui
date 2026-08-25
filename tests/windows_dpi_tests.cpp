#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(_WIN32)
#error Windows-only test
#endif

#include "webview-gui/_impl/platform/windows_plugin_runtime.h"

#include <windows.h>

namespace {

class ScopedThreadDpiAwareness {
public:
    explicit ScopedThreadDpiAwareness(DPI_AWARENESS_CONTEXT context) noexcept
        : previous(SetThreadDpiAwarenessContext(context))
    {
    }

    ScopedThreadDpiAwareness(const ScopedThreadDpiAwareness&) = delete;
    ScopedThreadDpiAwareness& operator=(const ScopedThreadDpiAwareness&) = delete;

    ~ScopedThreadDpiAwareness()
    {
        if (previous != nullptr)
            SetThreadDpiAwarenessContext(previous);
    }

    [[nodiscard]] bool ok() const noexcept { return previous != nullptr; }

private:
    DPI_AWARENESS_CONTEXT previous = nullptr;
};

class ScopedThreadDpiHosting {
public:
    explicit ScopedThreadDpiHosting(DPI_HOSTING_BEHAVIOR behavior) noexcept
        : previous(SetThreadDpiHostingBehavior(behavior))
    {
    }

    ScopedThreadDpiHosting(const ScopedThreadDpiHosting&) = delete;
    ScopedThreadDpiHosting& operator=(const ScopedThreadDpiHosting&) = delete;

    ~ScopedThreadDpiHosting()
    {
        if (previous != DPI_HOSTING_BEHAVIOR_INVALID)
            SetThreadDpiHostingBehavior(previous);
    }

    [[nodiscard]] bool ok() const noexcept
    {
        return previous != DPI_HOSTING_BEHAVIOR_INVALID;
    }

private:
    DPI_HOSTING_BEHAVIOR previous = DPI_HOSTING_BEHAVIOR_INVALID;
};

HWND createFixtureWindow(const wchar_t* title)
{
    return CreateWindowExW(0,
                           L"STATIC",
                           title,
                           WS_POPUP,
                           0,
                           0,
                           320,
                           180,
                           nullptr,
                           nullptr,
                           GetModuleHandleW(nullptr),
                           nullptr);
}

} // namespace

TEST_CASE("Win32 embedding is explicit about mixed-DPI host compatibility")
{
    HWND child = nullptr;
    {
        ScopedThreadDpiAwareness unaware{DPI_AWARENESS_CONTEXT_UNAWARE};
        REQUIRE_MESSAGE(unaware.ok(), "Windows runner must support thread DPI awareness contexts");
        child = createFixtureWindow(L"webview-gui DPI child");
    }
    REQUIRE(child != nullptr);

    HWND strictHost = nullptr;
    {
        ScopedThreadDpiAwareness perMonitor{DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2};
        REQUIRE_MESSAGE(perMonitor.ok(), "Windows runner must support per-monitor-v2 DPI awareness");
        strictHost = createFixtureWindow(L"webview-gui strict DPI host");
    }
    REQUIRE(strictHost != nullptr);

    const auto childContext = GetWindowDpiAwarenessContext(child);
    const auto strictHostContext = GetWindowDpiAwarenessContext(strictHost);
    REQUIRE(childContext != nullptr);
    REQUIRE(strictHostContext != nullptr);
    REQUIRE_FALSE(AreDpiAwarenessContextsEqual(childContext, strictHostContext));
    CHECK(GetWindowDpiHostingBehavior(strictHost) != DPI_HOSTING_BEHAVIOR_MIXED);

    // A same-process SetParent between different DPI-awareness contexts fails
    // with ERROR_INVALID_STATE on current Windows. The adapter must detect that
    // contract before mutating styles/parentage, rather than depending on an
    // OS-side partial reparent failure.
    SetLastError(ERROR_SUCCESS);
    const auto styleBeforeRejectedAttach = GetWindowLongPtrW(child, GWL_STYLE);
    const bool styleReadable = styleBeforeRejectedAttach != 0 || GetLastError() == ERROR_SUCCESS;
    REQUIRE_MESSAGE(styleReadable, "fixture child style must be readable");
    CHECK_FALSE(webview_gui::detail::windowsDpiHostingAllowsChild(child, strictHost));
    CHECK_FALSE(webview_gui::detail::attachChildWindowToHost(child, strictHost));
    CHECK(GetParent(child) == nullptr);
    CHECK(GetWindowLongPtrW(child, GWL_STYLE) == styleBeforeRejectedAttach);

    REQUIRE(DestroyWindow(strictHost));

    HWND mixedHost = nullptr;
    {
        ScopedThreadDpiAwareness perMonitor{DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2};
        REQUIRE_MESSAGE(perMonitor.ok(), "Windows runner must support per-monitor-v2 DPI awareness");
        ScopedThreadDpiHosting mixed{DPI_HOSTING_BEHAVIOR_MIXED};
        REQUIRE_MESSAGE(mixed.ok(), "Windows runner must support mixed DPI hosting");
        mixedHost = createFixtureWindow(L"webview-gui mixed DPI host");
    }
    REQUIRE(mixedHost != nullptr);
    CHECK(GetWindowDpiHostingBehavior(mixedHost) == DPI_HOSTING_BEHAVIOR_MIXED);

    // A host explicitly created for mixed-DPI plug-in content may accept the
    // independently-scaled CHOC child. This is the supported escape hatch for
    // DAWs whose editor parent and plug-in WebView use different awareness.
    CHECK(webview_gui::detail::windowsDpiHostingAllowsChild(child, mixedHost));
    CHECK(webview_gui::detail::attachChildWindowToHost(child, mixedHost));
    CHECK(GetParent(child) == mixedHost);

    REQUIRE(DestroyWindow(child));
    REQUIRE(DestroyWindow(mixedHost));
}
