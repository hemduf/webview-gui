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

TEST_CASE("mixed DPI hosting rejects a per-monitor child under a legacy host")
{
    ScopedWindow perMonitorChild;
    {
        ScopedThreadDpiAwareness perMonitor{DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2};
        REQUIRE_MESSAGE(perMonitor.ok(), "Windows runner must support per-monitor-v2 DPI awareness");
        perMonitorChild.reset(createFixtureWindow(L"webview-gui per-monitor DPI child"));
    }
    REQUIRE(perMonitorChild.get() != nullptr);

    ScopedWindow legacyMixedHost;
    {
        ScopedThreadDpiAwareness unaware{DPI_AWARENESS_CONTEXT_UNAWARE};
        REQUIRE_MESSAGE(unaware.ok(), "Windows runner must support unaware DPI contexts");
        ScopedThreadDpiHosting mixed{DPI_HOSTING_BEHAVIOR_MIXED};
        REQUIRE_MESSAGE(mixed.ok(), "Windows runner must support mixed DPI hosting");
        legacyMixedHost.reset(createFixtureWindow(L"webview-gui legacy mixed DPI host"));
    }
    REQUIRE(legacyMixedHost.get() != nullptr);

    const auto childContext = GetWindowDpiAwarenessContext(perMonitorChild.get());
    const auto hostContext = GetWindowDpiAwarenessContext(legacyMixedHost.get());
    REQUIRE(childContext != nullptr);
    REQUIRE(hostContext != nullptr);
    REQUIRE_FALSE(AreDpiAwarenessContextsEqual(childContext, hostContext));
    REQUIRE(GetWindowDpiHostingBehavior(legacyMixedHost.get()) == DPI_HOSTING_BEHAVIOR_MIXED);

    SetLastError(ERROR_SUCCESS);
    const auto styleBeforeRejectedAttach = GetWindowLongPtrW(perMonitorChild.get(), GWL_STYLE);
    const bool styleReadable = styleBeforeRejectedAttach != 0 || GetLastError() == ERROR_SUCCESS;
    REQUIRE_MESSAGE(styleReadable, "fixture child style must be readable");

    // Windows mixed-DPI hosting is directional: a per-monitor-aware host may
    // host legacy children, but a legacy host may not host a per-monitor child.
    // Reject this before SetParent so the child cannot be left half-mutated.
    CHECK_FALSE(webview_gui::detail::windowsDpiHostingAllowsChild(
        perMonitorChild.get(), legacyMixedHost.get()));
    CHECK_FALSE(webview_gui::detail::attachChildWindowToHost(
        perMonitorChild.get(), legacyMixedHost.get()));
    CHECK(GetParent(perMonitorChild.get()) == nullptr);
    CHECK(GetWindowLongPtrW(perMonitorChild.get(), GWL_STYLE) == styleBeforeRejectedAttach);
}

namespace {

struct FakeChildAttachOps {
    LONG_PTR style = static_cast<LONG_PTR>(WS_POPUP | WS_VISIBLE);
    HWND currentParent = nullptr;
    bool valid = true;
    bool visible = true;
    int failStyleWrites = 0;
    int failParentWrites = 0;
    int failFrameWrites = 0;
    int styleWrites = 0;
    int parentWrites = 0;
    int frameWrites = 0;
    DWORD lastError = ERROR_SUCCESS;

    bool isWindow(HWND) const noexcept { return valid; }
    bool dpiHostingAllowsChild(HWND, HWND) const noexcept { return true; }

    void setLastError(DWORD error) noexcept { lastError = error; }
    DWORD getLastError() const noexcept { return lastError; }

    LONG_PTR getWindowLongPtr(HWND, int index) noexcept
    {
        if (index != GWL_STYLE) {
            lastError = ERROR_INVALID_INDEX;
            return 0;
        }
        lastError = ERROR_SUCCESS;
        return style;
    }

    LONG_PTR setWindowLongPtr(HWND, int index, LONG_PTR value) noexcept
    {
        ++styleWrites;
        if (index != GWL_STYLE) {
            lastError = ERROR_INVALID_INDEX;
            return 0;
        }
        if (failStyleWrites > 0) {
            --failStyleWrites;
            lastError = ERROR_ACCESS_DENIED;
            return 0;
        }
        const auto previous = style;
        style = value;
        lastError = ERROR_SUCCESS;
        return previous;
    }

    HWND getParent(HWND) noexcept
    {
        lastError = ERROR_SUCCESS;
        return currentParent;
    }

    HWND setParent(HWND, HWND parent) noexcept
    {
        ++parentWrites;
        if (failParentWrites > 0) {
            --failParentWrites;
            lastError = ERROR_ACCESS_DENIED;
            return nullptr;
        }
        const auto previous = currentParent;
        currentParent = parent;
        lastError = ERROR_SUCCESS;
        return previous;
    }

    BOOL setWindowPos(HWND, HWND, int, int, int, int, UINT) noexcept
    {
        ++frameWrites;
        if (failFrameWrites > 0) {
            --failFrameWrites;
            lastError = ERROR_ACCESS_DENIED;
            return FALSE;
        }
        lastError = ERROR_SUCCESS;
        return TRUE;
    }
};

struct FakeWindowSnapshot {
    LONG_PTR style = 0;
    HWND parent = nullptr;
    bool valid = false;
    bool visible = false;
};

FakeWindowSnapshot snapshot(const FakeChildAttachOps& ops)
{
    return {ops.style, ops.currentParent, ops.valid, ops.visible};
}

void checkEquivalent(const FakeChildAttachOps& ops, const FakeWindowSnapshot& before)
{
    CHECK(ops.style == before.style);
    CHECK(ops.currentParent == before.parent);
    CHECK(ops.valid == before.valid);
    CHECK(ops.visible == before.visible);
}

HWND fakeHandle(std::uintptr_t value)
{
    return reinterpret_cast<HWND>(value);
}

} // namespace

TEST_CASE("atomic Win32 child attach leaves state unchanged when style mutation fails")
{
    FakeChildAttachOps ops;
    ops.failStyleWrites = 1;
    const auto before = snapshot(ops);

    CHECK_FALSE(webview_gui::detail::attachChildWindowToHostWithOps(
        fakeHandle(0x101), fakeHandle(0x202), ops));
    checkEquivalent(ops, before);
    CHECK(ops.styleWrites == 1);
    CHECK(ops.parentWrites == 0);
    CHECK(ops.frameWrites == 0);
}

TEST_CASE("atomic Win32 child attach rolls style back when SetParent fails")
{
    FakeChildAttachOps ops;
    ops.failParentWrites = 1;
    const auto before = snapshot(ops);

    CHECK_FALSE(webview_gui::detail::attachChildWindowToHostWithOps(
        fakeHandle(0x101), fakeHandle(0x202), ops));
    checkEquivalent(ops, before);
    CHECK(ops.styleWrites == 2);
    CHECK(ops.parentWrites == 2);
    CHECK(ops.frameWrites == 1);
}

TEST_CASE("atomic Win32 child attach restores original parent and style when frame update fails")
{
    FakeChildAttachOps ops;
    ops.style = static_cast<LONG_PTR>(WS_CHILD | WS_VISIBLE);
    ops.currentParent = fakeHandle(0x303);
    ops.failFrameWrites = 1;
    const auto before = snapshot(ops);

    CHECK_FALSE(webview_gui::detail::attachChildWindowToHostWithOps(
        fakeHandle(0x101), fakeHandle(0x202), ops));
    checkEquivalent(ops, before);
    CHECK(ops.styleWrites == 2);
    CHECK(ops.parentWrites == 2);
    CHECK(ops.frameWrites == 2);
}
