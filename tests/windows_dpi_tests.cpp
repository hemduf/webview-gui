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

class ScopedWindow {
public:
    ScopedWindow() = default;
    explicit ScopedWindow(HWND window) noexcept : handle(window) {}

    ScopedWindow(const ScopedWindow&) = delete;
    ScopedWindow& operator=(const ScopedWindow&) = delete;

    ~ScopedWindow()
    {
        destroy();
    }

    void reset(HWND window = nullptr) noexcept
    {
        destroy();
        handle = window;
    }

    [[nodiscard]] HWND get() const noexcept { return handle; }

    bool destroy() noexcept
    {
        if (!handle)
            return true;

        if (!IsWindow(handle)) {
            handle = nullptr;
            return true;
        }

        if (DestroyWindow(handle) == 0)
            return false;

        handle = nullptr;
        return true;
    }

private:
    HWND handle = nullptr;
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
    ScopedWindow child;
    {
        ScopedThreadDpiAwareness unaware{DPI_AWARENESS_CONTEXT_UNAWARE};
        REQUIRE_MESSAGE(unaware.ok(), "Windows runner must support thread DPI awareness contexts");
        child.reset(createFixtureWindow(L"webview-gui DPI child"));
    }
    REQUIRE(child.get() != nullptr);

    ScopedWindow strictHost;
    {
        ScopedThreadDpiAwareness perMonitor{DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2};
        REQUIRE_MESSAGE(perMonitor.ok(), "Windows runner must support per-monitor-v2 DPI awareness");
        strictHost.reset(createFixtureWindow(L"webview-gui strict DPI host"));
    }
    REQUIRE(strictHost.get() != nullptr);

    const auto childContext = GetWindowDpiAwarenessContext(child.get());
    const auto strictHostContext = GetWindowDpiAwarenessContext(strictHost.get());
    REQUIRE(childContext != nullptr);
    REQUIRE(strictHostContext != nullptr);
    REQUIRE_FALSE(AreDpiAwarenessContextsEqual(childContext, strictHostContext));
    CHECK(GetWindowDpiHostingBehavior(strictHost.get()) != DPI_HOSTING_BEHAVIOR_MIXED);

    // A same-process SetParent between different DPI-awareness contexts fails
    // with ERROR_INVALID_STATE on current Windows. The adapter must detect that
    // contract before mutating styles/parentage, rather than depending on an
    // OS-side partial reparent failure.
    SetLastError(ERROR_SUCCESS);
    const auto styleBeforeRejectedAttach = GetWindowLongPtrW(child.get(), GWL_STYLE);
    const bool styleReadable = styleBeforeRejectedAttach != 0 || GetLastError() == ERROR_SUCCESS;
    REQUIRE_MESSAGE(styleReadable, "fixture child style must be readable");
    CHECK_FALSE(webview_gui::detail::windowsDpiHostingAllowsChild(child.get(), strictHost.get()));
    CHECK_FALSE(webview_gui::detail::attachChildWindowToHost(child.get(), strictHost.get()));
    CHECK(GetParent(child.get()) == nullptr);
    CHECK(GetWindowLongPtrW(child.get(), GWL_STYLE) == styleBeforeRejectedAttach);

    REQUIRE(strictHost.destroy());

    ScopedWindow mixedHost;
    {
        ScopedThreadDpiAwareness perMonitor{DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2};
        REQUIRE_MESSAGE(perMonitor.ok(), "Windows runner must support per-monitor-v2 DPI awareness");
        ScopedThreadDpiHosting mixed{DPI_HOSTING_BEHAVIOR_MIXED};
        REQUIRE_MESSAGE(mixed.ok(), "Windows runner must support mixed DPI hosting");
        mixedHost.reset(createFixtureWindow(L"webview-gui mixed DPI host"));
    }
    REQUIRE(mixedHost.get() != nullptr);
    CHECK(GetWindowDpiHostingBehavior(mixedHost.get()) == DPI_HOSTING_BEHAVIOR_MIXED);

    // A host explicitly created for mixed-DPI plug-in content may accept the
    // independently-scaled CHOC child. This is the supported escape hatch for
    // DAWs whose editor parent and plug-in WebView use different awareness.
    CHECK(webview_gui::detail::windowsDpiHostingAllowsChild(child.get(), mixedHost.get()));
    CHECK(webview_gui::detail::attachChildWindowToHost(child.get(), mixedHost.get()));
    CHECK(GetParent(child.get()) == mixedHost.get());

    REQUIRE(child.destroy());
    REQUIRE(mixedHost.destroy());
}
