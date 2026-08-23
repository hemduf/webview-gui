#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(_WIN32)
#error Windows-only test
#endif

#include "webview-gui/webview-gui.h"
#include "webview-gui/_impl/platform/windows_plugin_runtime.h"

#include <windows.h>
#include <objbase.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

namespace {

using RetainWebViewsFn = bool (*)(std::size_t, std::uintptr_t*, std::uintptr_t*, wchar_t*, std::size_t, bool*, bool*);
using FirstHWNDFn = std::uintptr_t (*)();
using ReleaseWebViewsFn = void (*)();

struct RetainedInfo {
    std::uintptr_t module = 0;
    std::uintptr_t windowInstance = 0;
    std::wstring className;
    bool allOwnedByModule = false;
    bool allClassNamesUnique = false;
};

struct Module {
    HMODULE handle = nullptr;
    RetainWebViewsFn retainWebViews = nullptr;
    FirstHWNDFn firstHWND = nullptr;
    ReleaseWebViewsFn releaseWebViews = nullptr;

    explicit Module(const char* path)
    {
        handle = LoadLibraryA(path);
        if (handle) {
            retainWebViews = reinterpret_cast<RetainWebViewsFn>(
                GetProcAddress(handle, "webview_gui_test_retain_windows_webviews"));
            firstHWND = reinterpret_cast<FirstHWNDFn>(
                GetProcAddress(handle, "webview_gui_test_first_windows_hwnd"));
            releaseWebViews = reinterpret_cast<ReleaseWebViewsFn>(
                GetProcAddress(handle, "webview_gui_test_release_windows_webviews"));
        }
    }

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    ~Module() { close(); }

    RetainedInfo retain(std::size_t count) const
    {
        std::array<wchar_t, 256> className{};
        RetainedInfo result;
        if (!retainWebViews
            || !retainWebViews(count,
                               &result.module,
                               &result.windowInstance,
                               className.data(),
                               className.size(),
                               &result.allOwnedByModule,
                               &result.allClassNamesUnique))
            return {};

        result.className = className.data();
        return result;
    }

    HWND firstWindow() const
    {
        return firstHWND ? reinterpret_cast<HWND>(firstHWND()) : nullptr;
    }

    void release() const
    {
        if (releaseWebViews)
            releaseWebViews();
    }

    void close()
    {
        if (handle) {
            FreeLibrary(handle);
            handle = nullptr;
            retainWebViews = nullptr;
            firstHWND = nullptr;
            releaseWebViews = nullptr;
        }
    }
};

struct FakeWebMessageArgs {
    std::wstring source;
    std::wstring message = L"[\"setParameter\",42]";
    int sourceReads = 0;
    int messageReads = 0;

    static HRESULT copyCOMString(const std::wstring& value, LPWSTR* output)
    {
        if (!output) return E_POINTER;
        const auto bytes = (value.size() + 1) * sizeof(wchar_t);
        auto* copy = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        if (!copy) return E_OUTOFMEMORY;
        std::memcpy(copy, value.c_str(), bytes);
        *output = copy;
        return S_OK;
    }

    HRESULT get_Source(LPWSTR* output)
    {
        ++sourceReads;
        return copyCOMString(source, output);
    }

    HRESULT TryGetWebMessageAsString(LPWSTR* output)
    {
        ++messageReads;
        return copyCOMString(message, output);
    }
};

bool windowClassIsRegistered(std::uintptr_t module, const std::wstring& className)
{
    if (module == 0 || className.empty()) return false;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    return GetClassInfoExW(reinterpret_cast<HINSTANCE>(module), className.c_str(), &wc) != 0;
}

HWND createHostWindow()
{
    return CreateWindowExW(0,
                           L"STATIC",
                           L"webview-gui host harness",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT,
                           CW_USEDEFAULT,
                           700,
                           500,
                           nullptr,
                           nullptr,
                           GetModuleHandleW(nullptr),
                           nullptr);
}

bool smokeFocusedWin32Input(HWND child)
{
    if (!IsWindow(child) || !IsWindowVisible(child))
        return false;

    SetFocus(child);
    if (GetFocus() != child)
        return false;

    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(child,
                            WM_KEYDOWN,
                            static_cast<WPARAM>('A'),
                            1,
                            SMTO_ABORTIFHUNG,
                            1000,
                            &result) == 0)
        return false;

    return SendMessageTimeoutW(child,
                               WM_KEYUP,
                               static_cast<WPARAM>('A'),
                               0xC0000001,
                               SMTO_ABORTIFHUNG,
                               1000,
                               &result) != 0;
}

} // namespace

TEST_CASE("public support negotiation exposes only Win32 embedding")
{
    CHECK(WebviewGui::supports(WebviewGui::HWND));
    CHECK_FALSE(WebviewGui::supports(WebviewGui::COCOA));
    CHECK_FALSE(WebviewGui::supports(WebviewGui::X11EMBED));
    CHECK_FALSE(WebviewGui::supports(WebviewGui::NONE));
}

TEST_CASE("WebView2 bridge reads and dispatches only the exact privileged origin")
{
    const std::array<const wchar_t*, 3> trusted = {
        L"https://choc.localhost",
        L"https://choc.localhost/",
        L"https://choc.localhost/index.html?x=1#fragment",
    };

    for (const auto* source : trusted) {
        FakeWebMessageArgs args{source};
        int dispatches = 0;
        std::wstring received;
        const auto hr = webview_gui::detail::dispatchTrustedWindowsWebMessage(
            &args,
            [&](LPCWSTR message) {
                ++dispatches;
                received = message ? message : L"";
            });

        CHECK(hr == S_OK);
        CHECK(args.sourceReads == 1);
        CHECK(args.messageReads == 1);
        CHECK(dispatches == 1);
        CHECK(received == args.message);
    }

    const std::array<const wchar_t*, 9> untrusted = {
        L"https://example.com/",
        L"http://choc.localhost/",
        L"https://choc.localhost.evil/",
        L"https://choc.localhost:443/",
        L"file:///C:/plugin/index.html",
        L"data:text/html,evil",
        L"javascript:alert(1)",
        L"about:blank",
        L"https://example.com/redirected-from-local",
    };

    for (const auto* source : untrusted) {
        FakeWebMessageArgs args{source};
        int dispatches = 0;
        const auto hr = webview_gui::detail::dispatchTrustedWindowsWebMessage(
            &args,
            [&](LPCWSTR) { ++dispatches; });

        CHECK(hr == S_OK);
        CHECK(args.sourceReads == 1);
        CHECK(args.messageReads == 0);
        CHECK(dispatches == 0);
    }
}

TEST_CASE("CHOC Win32 window classes belong to each plug-in DLL, not the host executable")
{
    Module moduleA{MODULE_A_PATH};
    Module moduleB{MODULE_B_PATH};

    REQUIRE(moduleA.handle != nullptr);
    REQUIRE(moduleB.handle != nullptr);
    REQUIRE(moduleA.retainWebViews != nullptr);
    REQUIRE(moduleB.retainWebViews != nullptr);

    const auto a = moduleA.retain(12);
    const auto b = moduleB.retain(12);

    REQUIRE(a.module != 0);
    REQUIRE(b.module != 0);
    REQUIRE_FALSE(a.className.empty());
    REQUIRE_FALSE(b.className.empty());

    const auto executable = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));

    CHECK(a.module == reinterpret_cast<std::uintptr_t>(moduleA.handle));
    CHECK(b.module == reinterpret_cast<std::uintptr_t>(moduleB.handle));
    CHECK(a.module != executable);
    CHECK(b.module != executable);
    CHECK(a.windowInstance == a.module);
    CHECK(b.windowInstance == b.module);
    CHECK(a.allOwnedByModule);
    CHECK(b.allOwnedByModule);
    CHECK(a.allClassNamesUnique);
    CHECK(b.allClassNamesUnique);

    CHECK(windowClassIsRegistered(a.module, a.className));
    CHECK(windowClassIsRegistered(b.module, b.className));

    moduleA.release();
    CHECK_FALSE(windowClassIsRegistered(a.module, a.className));
    CHECK(windowClassIsRegistered(b.module, b.className));

    moduleA.close();

    CHECK(windowClassIsRegistered(b.module, b.className));
    moduleB.release();
    CHECK_FALSE(windowClassIsRegistered(b.module, b.className));
}

TEST_CASE("a real CHOC HWND embeds as a child without damaging the host window")
{
    Module module{MODULE_A_PATH};
    REQUIRE(module.handle != nullptr);
    REQUIRE(module.firstHWND != nullptr);

    const auto retained = module.retain(1);
    REQUIRE(retained.module != 0);

    const auto child = module.firstWindow();
    REQUIRE(child != nullptr);
    REQUIRE(IsWindow(child));

    const auto host = createHostWindow();
    REQUIRE(host != nullptr);
    REQUIRE(IsWindow(host));

    CHECK_FALSE(webview_gui::detail::attachChildWindowToHost(child, nullptr));
    CHECK_FALSE(webview_gui::detail::attachChildWindowToHost(child,
                                                              reinterpret_cast<HWND>(std::uintptr_t{1})));
    CHECK(webview_gui::detail::attachChildWindowToHost(child, host));
    CHECK(GetParent(child) == host);

    const auto style = GetWindowLongPtrW(child, GWL_STYLE);
    CHECK((style & WS_CHILD) != 0);
    CHECK((style & WS_POPUP) == 0);

    CHECK(webview_gui::detail::resizeChildWindow(child, 640, 360));
    RECT childRect{};
    REQUIRE(GetClientRect(child, &childRect));
    CHECK(childRect.right - childRect.left == 640);
    CHECK(childRect.bottom - childRect.top == 360);

    CHECK(webview_gui::detail::setChildWindowVisible(child, false));
    CHECK_FALSE(IsWindowVisible(child));
    CHECK(webview_gui::detail::setChildWindowVisible(child, true));
    CHECK(IsWindowVisible(child));
    CHECK(smokeFocusedWin32Input(child));

    // An invalid reparenting request must fail without disturbing the valid
    // host relationship established above.
    CHECK_FALSE(webview_gui::detail::attachChildWindowToHost(
        child, reinterpret_cast<HWND>(std::uintptr_t{1})));
    CHECK(GetParent(child) == host);

    module.release();
    CHECK_FALSE(IsWindow(child));
    CHECK(IsWindow(host));
    CHECK(DestroyWindow(host));
}

TEST_CASE("Win32 host survives repeated CHOC attach resize focus and destroy cycles")
{
    Module module{MODULE_A_PATH};
    REQUIRE(module.handle != nullptr);

    const auto host = createHostWindow();
    REQUIRE(host != nullptr);

    for (int iteration = 0; iteration < 4; ++iteration) {
        const auto retained = module.retain(1);
        REQUIRE(retained.module != 0);

        const auto child = module.firstWindow();
        REQUIRE(child != nullptr);
        REQUIRE(IsWindow(child));
        REQUIRE(webview_gui::detail::attachChildWindowToHost(child, host));

        const int width = 480 + iteration * 16;
        const int height = 280 + iteration * 12;
        REQUIRE(webview_gui::detail::resizeChildWindow(child, width, height));

        RECT rect{};
        REQUIRE(GetClientRect(child, &rect));
        CHECK(rect.right - rect.left == width);
        CHECK(rect.bottom - rect.top == height);

        REQUIRE(webview_gui::detail::setChildWindowVisible(child, false));
        CHECK_FALSE(IsWindowVisible(child));
        REQUIRE(webview_gui::detail::setChildWindowVisible(child, true));
        CHECK(IsWindowVisible(child));
        CHECK(smokeFocusedWin32Input(child));

        module.release();
        CHECK_FALSE(IsWindow(child));
        CHECK(IsWindow(host));
    }

    CHECK(DestroyWindow(host));
}

TEST_CASE("plug-in-owned COM initialization is balanced after CHOC WebView destruction")
{
    const auto hostInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    REQUIRE((hostInit == S_OK || hostInit == S_FALSE));

    Module module{MODULE_A_PATH};
    REQUIRE(module.handle != nullptr);
    REQUIRE(module.retainWebViews != nullptr);

    const auto retained = module.retain(8);
    REQUIRE(retained.module != 0);
    module.release();
    module.close();

    CoUninitialize();

    const auto mtaProbe = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CHECK(mtaProbe == S_OK);
    if (mtaProbe == S_OK || mtaProbe == S_FALSE)
        CoUninitialize();
}

TEST_CASE("ScopedCOMApartment rejects an incompatible host-owned MTA")
{
    std::atomic<bool> hostReady{false};
    std::atomic<bool> wrapperRejected{false};

    std::thread thread([&] {
        const auto mta = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        hostReady.store(mta == S_OK || mta == S_FALSE, std::memory_order_relaxed);

        if (mta == S_OK || mta == S_FALSE) {
            webview_gui::detail::ScopedCOMApartment apartment;
            wrapperRejected.store(!apartment.ok() && apartment.incompatibleApartment(),
                                  std::memory_order_relaxed);
            CoUninitialize();
        }
    });
    thread.join();

    CHECK(hostReady.load(std::memory_order_relaxed));
    CHECK(wrapperRejected.load(std::memory_order_relaxed));
}
