#pragma once

#if !defined(_WIN32)
#error windows_plugin_runtime.h is Windows-only
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <atomic>
#include <cstdint>
#include <utility>

namespace webview_gui::detail {

static void windowsPluginModuleAnchor() {}

inline HMODULE windowsPluginModuleHandle() noexcept
{
    HMODULE module = nullptr;
    const auto address = reinterpret_cast<LPCWSTR>(&windowsPluginModuleAnchor);

    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           address,
                           &module) != 0)
        return module;

    return nullptr;
}

inline DWORD nextWindowsClassToken() noexcept
{
    static std::atomic<DWORD> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

inline bool isTrustedWindowsBridgeSource(LPCWSTR source) noexcept
{
    if (!source)
        return false;

    constexpr wchar_t trustedOrigin[] = L"https://choc.localhost";
    constexpr int trustedLength = static_cast<int>((sizeof(trustedOrigin) / sizeof(wchar_t)) - 1);
    const int sourceLength = lstrlenW(source);

    if (sourceLength < trustedLength
        || CompareStringOrdinal(source,
                                trustedLength,
                                trustedOrigin,
                                trustedLength,
                                TRUE) != CSTR_EQUAL)
        return false;

    const wchar_t next = source[trustedLength];
    return next == L'\0' || next == L'/' || next == L'?' || next == L'#';
}

inline bool isAllowedWindowsPluginNavigation(LPCWSTR uri) noexcept
{
    if (isTrustedWindowsBridgeSource(uri))
        return true;

    if (!uri)
        return false;

    constexpr wchar_t aboutBlank[] = L"about:blank";
    constexpr int aboutBlankLength = static_cast<int>((sizeof(aboutBlank) / sizeof(wchar_t)) - 1);

    return lstrlenW(uri) == aboutBlankLength
        && CompareStringOrdinal(uri,
                                aboutBlankLength,
                                aboutBlank,
                                aboutBlankLength,
                                TRUE) == CSTR_EQUAL;
}

inline bool hasWindowsWebScheme(LPCWSTR uri) noexcept
{
    if (!uri)
        return false;

    constexpr wchar_t http[] = L"http://";
    constexpr wchar_t https[] = L"https://";
    const int uriLength = lstrlenW(uri);
    return (uriLength >= 7 && CompareStringOrdinal(uri, 7, http, 7, TRUE) == CSTR_EQUAL)
        || (uriLength >= 8 && CompareStringOrdinal(uri, 8, https, 8, TRUE) == CSTR_EQUAL);
}

inline void openWindowsExternalURL(LPCWSTR uri) noexcept
{
    if (hasWindowsWebScheme(uri))
        ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
}

// WebView2 reports the source document separately from the web message. Read
// and validate that source before touching the message payload so a remote,
// redirected, file:, data:, javascript:, or about:blank document cannot reach
// CHOC's native invokeBinding path. EventArgs is templated to keep this small
// policy independently testable with a fake COM-style object.
template <typename EventArgs, typename Dispatch>
HRESULT dispatchTrustedWindowsWebMessage(EventArgs* args, Dispatch&& dispatch)
{
    if (!args)
        return E_POINTER;

    LPWSTR source = nullptr;
    const auto sourceResult = args->get_Source(&source);
    const bool trusted = SUCCEEDED(sourceResult) && isTrustedWindowsBridgeSource(source);
    if (source)
        CoTaskMemFree(source);

    // Fail closed but report the event as handled. Returning a COM failure from
    // an untrusted page can trigger WebView2 host error paths even though the
    // correct security action is simply to drop the bridge message.
    if (!trusted)
        return S_OK;

    LPWSTR message = nullptr;
    const auto messageResult = args->TryGetWebMessageAsString(&message);
    if (FAILED(messageResult) || !message) {
        if (message)
            CoTaskMemFree(message);
        return S_OK;
    }

    std::forward<Dispatch>(dispatch)(message);
    CoTaskMemFree(message);
    return S_OK;
}

// Keep the privileged WebView on its exact local origin, while allowing only
// WebView2's inert about:blank bootstrap document. Every other top-level
// navigation is cancelled before commit. The bootstrap page is deliberately
// not accepted by isTrustedWindowsBridgeSource(), so it never gets bridge
// privileges. A direct user navigation to http(s) may be routed to the system
// browser, but redirects, script-driven navigation, and non-web schemes are
// only cancelled.
template <typename EventArgs, typename OpenExternal>
HRESULT handleWindowsPluginNavigation(EventArgs* args, OpenExternal&& openExternal)
{
    if (!args)
        return E_POINTER;

    LPWSTR uri = nullptr;
    const auto uriResult = args->get_Uri(&uri);
    if (FAILED(uriResult) || !uri) {
        if (uri)
            CoTaskMemFree(uri);
        args->put_Cancel(TRUE);
        return S_OK;
    }

    if (isAllowedWindowsPluginNavigation(uri)) {
        CoTaskMemFree(uri);
        return S_OK;
    }

    BOOL userInitiated = FALSE;
    BOOL redirected = TRUE;
    const bool mayOpenExternally =
        SUCCEEDED(args->get_IsUserInitiated(&userInitiated))
        && SUCCEEDED(args->get_IsRedirected(&redirected))
        && userInitiated != FALSE
        && redirected == FALSE
        && hasWindowsWebScheme(uri);

    const auto cancelResult = args->put_Cancel(TRUE);
    if (SUCCEEDED(cancelResult) && mayOpenExternally)
        std::forward<OpenExternal>(openExternal)(uri);

    CoTaskMemFree(uri);
    return SUCCEEDED(cancelResult) ? S_OK : cancelResult;
}

// CHOC's WebView currently calls CoInitialize(nullptr) internally without a
// matching CoUninitialize(). The plug-in wrapper owns COM lifetime explicitly,
// so the intercepted CHOC call is intentionally a no-op.
inline HRESULT suppressedCHOCWebViewCoInitialize(void*) noexcept
{
    return S_OK;
}

class ScopedCOMApartment {
public:
    ScopedCOMApartment() noexcept
        : result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
          ownsReference(result == S_OK || result == S_FALSE)
    {
    }

    ScopedCOMApartment(const ScopedCOMApartment&) = delete;
    ScopedCOMApartment& operator=(const ScopedCOMApartment&) = delete;

    ~ScopedCOMApartment()
    {
        if (ownsReference)
            CoUninitialize();
    }

    [[nodiscard]] bool ok() const noexcept
    {
        return result == S_OK || result == S_FALSE;
    }

    [[nodiscard]] bool incompatibleApartment() const noexcept
    {
        return result == RPC_E_CHANGED_MODE;
    }

    [[nodiscard]] HRESULT status() const noexcept { return result; }

private:
    HRESULT result = E_FAIL;
    bool ownsReference = false;
};

inline bool attachChildWindowToHost(HWND child, HWND parent) noexcept
{
    if (!IsWindow(child) || !IsWindow(parent) || child == parent)
        return false;

    SetLastError(ERROR_SUCCESS);
    const auto oldStyle = GetWindowLongPtrW(child, GWL_STYLE);
    if (oldStyle == 0 && GetLastError() != ERROR_SUCCESS)
        return false;

    const auto childStyle = (oldStyle & ~static_cast<LONG_PTR>(WS_POPUP))
                          | static_cast<LONG_PTR>(WS_CHILD)
                          | static_cast<LONG_PTR>(WS_CLIPCHILDREN)
                          | static_cast<LONG_PTR>(WS_CLIPSIBLINGS);

    SetLastError(ERROR_SUCCESS);
    const auto previousStyle = SetWindowLongPtrW(child, GWL_STYLE, childStyle);
    if (previousStyle == 0 && GetLastError() != ERROR_SUCCESS)
        return false;

    SetLastError(ERROR_SUCCESS);
    const auto previousParent = SetParent(child, parent);
    if (previousParent == nullptr && GetLastError() != ERROR_SUCCESS)
        return false;

    return SetWindowPos(child, nullptr, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
                            | SWP_NOACTIVATE | SWP_FRAMECHANGED) != 0;
}

inline bool resizeChildWindow(HWND child, int width, int height) noexcept
{
    if (!IsWindow(child) || width < 0 || height < 0)
        return false;

    return SetWindowPos(child, nullptr, 0, 0, width, height,
                        SWP_NOZORDER | SWP_NOACTIVATE) != 0;
}

inline bool setChildWindowVisible(HWND child, bool visible) noexcept
{
    if (!IsWindow(child))
        return false;

    ShowWindow(child, visible ? SW_SHOW : SW_HIDE);
    if (visible)
        InvalidateRect(child, nullptr, FALSE);
    return true;
}

} // namespace webview_gui::detail