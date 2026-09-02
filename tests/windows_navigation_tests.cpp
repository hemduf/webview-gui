#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(_WIN32)
#error Windows-only test
#endif

#include "webview-gui/_impl/platform/windows_plugin_runtime.h"

#include <windows.h>
#include <objbase.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

struct FakeNavigationArgs {
    std::wstring uri;
    BOOL userInitiated = TRUE;
    BOOL redirected = FALSE;
    bool cancelled = false;
    int uriReads = 0;
    int cancelWrites = 0;
    HRESULT uriResult = S_OK;
    bool nullUri = false;
    HRESULT cancelResult = S_OK;

    HRESULT get_Uri(LPWSTR* output)
    {
        ++uriReads;
        if (!output) return E_POINTER;
        *output = nullptr;
        if (FAILED(uriResult)) return uriResult;
        if (nullUri) return S_OK;
        const auto bytes = (uri.size() + 1) * sizeof(wchar_t);
        auto* copy = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        if (!copy) return E_OUTOFMEMORY;
        std::memcpy(copy, uri.c_str(), bytes);
        *output = copy;
        return S_OK;
    }

    HRESULT get_IsUserInitiated(BOOL* output)
    {
        if (!output) return E_POINTER;
        *output = userInitiated;
        return S_OK;
    }

    HRESULT get_IsRedirected(BOOL* output)
    {
        if (!output) return E_POINTER;
        *output = redirected;
        return S_OK;
    }

    HRESULT put_Cancel(BOOL value)
    {
        ++cancelWrites;
        if (SUCCEEDED(cancelResult))
            cancelled = value != FALSE;
        return cancelResult;
    }
};

struct FakePermissionArgs {
    int permissionKind = 7;
    std::wstring uri = L"https://choc.localhost/index.html";
    int state = 0;
    int kindReads = 0;
    int uriReads = 0;
    int stateWrites = 0;
    HRESULT kindResult = S_OK;
    HRESULT uriResult = S_OK;
    HRESULT stateResult = S_OK;
    bool nullUri = false;

    HRESULT get_PermissionKind(int* output)
    {
        ++kindReads;
        if (!output) return E_POINTER;
        if (FAILED(kindResult)) return kindResult;
        *output = permissionKind;
        return S_OK;
    }

    HRESULT get_Uri(LPWSTR* output)
    {
        ++uriReads;
        if (!output) return E_POINTER;
        *output = nullptr;
        if (FAILED(uriResult)) return uriResult;
        if (nullUri) return S_OK;
        const auto bytes = (uri.size() + 1) * sizeof(wchar_t);
        auto* copy = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        if (!copy) return E_OUTOFMEMORY;
        std::memcpy(copy, uri.c_str(), bytes);
        *output = copy;
        return S_OK;
    }

    HRESULT put_State(int value)
    {
        ++stateWrites;
        if (SUCCEEDED(stateResult))
            state = value;
        return stateResult;
    }
};

} // namespace

TEST_CASE("Windows navigation policy keeps privileged local pages in-process")
{
    FakeNavigationArgs args{L"https://choc.localhost/index.html"};
    std::vector<std::wstring> external;

    CHECK(webview_gui::detail::handleWindowsPluginNavigation(
              &args,
              [&](LPCWSTR uri) { external.emplace_back(uri ? uri : L""); }) == S_OK);
    CHECK(args.uriReads == 1);
    CHECK(args.cancelWrites == 0);
    CHECK_FALSE(args.cancelled);
    CHECK(external.empty());
}

TEST_CASE("Windows navigation policy allows inert about:blank bootstrap without granting bridge trust")
{
    FakeNavigationArgs args{L"about:blank", FALSE, FALSE};
    int externalLaunches = 0;

    CHECK(webview_gui::detail::handleWindowsPluginNavigation(
              &args,
              [&](LPCWSTR) { ++externalLaunches; }) == S_OK);
    CHECK(args.uriReads == 1);
    CHECK(args.cancelWrites == 0);
    CHECK_FALSE(args.cancelled);
    CHECK(externalLaunches == 0);
    CHECK_FALSE(webview_gui::detail::isTrustedWindowsBridgeSource(L"about:blank"));
}

TEST_CASE("short malformed Windows URIs are rejected without prefix over-read assumptions")
{
    CHECK_FALSE(webview_gui::detail::isTrustedWindowsBridgeSource(L"x"));
    CHECK_FALSE(webview_gui::detail::isAllowedWindowsPluginNavigation(L"x"));
    CHECK_FALSE(webview_gui::detail::hasWindowsWebScheme(L"x"));

    FakeNavigationArgs args{L"x", TRUE, FALSE};
    int externalLaunches = 0;
    CHECK(webview_gui::detail::handleWindowsPluginNavigation(
              &args,
              [&](LPCWSTR) { ++externalLaunches; }) == S_OK);
    CHECK(args.cancelWrites == 1);
    CHECK(args.cancelled);
    CHECK(externalLaunches == 0);
}

TEST_CASE("Windows navigation policy cancels remote and dangerous schemes")
{
    const wchar_t* blocked[] = {
        L"https://example.com/",
        L"http://choc.localhost/",
        L"https://choc.localhost.evil/",
        L"https://choc.localhost:443/",
        L"file:///C:/plugin/index.html",
        L"data:text/html,evil",
        L"javascript:alert(1)",
    };

    for (const auto* uri : blocked) {
        FakeNavigationArgs args{uri};
        std::vector<std::wstring> external;
        CHECK(webview_gui::detail::handleWindowsPluginNavigation(
                  &args,
                  [&](LPCWSTR value) { external.emplace_back(value ? value : L""); }) == S_OK);
        CHECK(args.cancelWrites == 1);
        CHECK(args.cancelled);

        const std::wstring value = uri;
        const bool browserSafe = value.rfind(L"https://", 0) == 0 || value.rfind(L"http://", 0) == 0;
        CHECK(external.size() == (browserSafe ? 1u : 0u));
    }
}

TEST_CASE("redirected remote navigation is cancelled without launching an external browser")
{
    FakeNavigationArgs args{L"https://example.com/redirect", TRUE, TRUE};
    int externalLaunches = 0;

    CHECK(webview_gui::detail::handleWindowsPluginNavigation(
              &args,
              [&](LPCWSTR) { ++externalLaunches; }) == S_OK);
    CHECK(args.cancelled);
    CHECK(externalLaunches == 0);
}

TEST_CASE("non-user-initiated remote navigation is cancelled without external launch")
{
    FakeNavigationArgs args{L"https://example.com/script", FALSE, FALSE};
    int externalLaunches = 0;

    CHECK(webview_gui::detail::handleWindowsPluginNavigation(
              &args,
              [&](LPCWSTR) { ++externalLaunches; }) == S_OK);
    CHECK(args.cancelled);
    CHECK(externalLaunches == 0);
}

TEST_CASE("URI read failure remains fail-closed when cancellation succeeds")
{
    FakeNavigationArgs args;
    args.uriResult = E_FAIL;
    int externalLaunches = 0;

    CHECK(webview_gui::detail::handleWindowsPluginNavigation(
              &args,
              [&](LPCWSTR) { ++externalLaunches; }) == S_OK);
    CHECK(args.uriReads == 1);
    CHECK(args.cancelWrites == 1);
    CHECK(args.cancelled);
    CHECK(externalLaunches == 0);
}

TEST_CASE("URI read failure propagates cancellation failure")
{
    FakeNavigationArgs args;
    args.uriResult = E_FAIL;
    args.cancelResult = E_ACCESSDENIED;
    int externalLaunches = 0;

    CHECK(webview_gui::detail::handleWindowsPluginNavigation(
              &args,
              [&](LPCWSTR) { ++externalLaunches; }) == E_ACCESSDENIED);
    CHECK(args.uriReads == 1);
    CHECK(args.cancelWrites == 1);
    CHECK_FALSE(args.cancelled);
    CHECK(externalLaunches == 0);
}

TEST_CASE("null URI propagates cancellation failure")
{
    FakeNavigationArgs args;
    args.nullUri = true;
    args.cancelResult = E_ABORT;
    int externalLaunches = 0;

    CHECK(webview_gui::detail::handleWindowsPluginNavigation(
              &args,
              [&](LPCWSTR) { ++externalLaunches; }) == E_ABORT);
    CHECK(args.uriReads == 1);
    CHECK(args.cancelWrites == 1);
    CHECK_FALSE(args.cancelled);
    CHECK(externalLaunches == 0);
}

TEST_CASE("Windows clipboard read is granted only to the exact privileged origin")
{
    constexpr int clipboardRead = 7;
    constexpr int allow = 1;
    constexpr int deny = 2;

    FakePermissionArgs trusted;
    CHECK(webview_gui::detail::handleWindowsPluginPermission(
              &trusted, clipboardRead, allow, deny) == S_OK);
    CHECK(trusted.kindReads == 1);
    CHECK(trusted.uriReads == 1);
    CHECK(trusted.stateWrites == 1);
    CHECK(trusted.state == allow);

    const wchar_t* untrusted[] = {
        L"about:blank",
        L"https://example.com/",
        L"https://choc.localhost.evil/",
        L"file:///C:/plugin/index.html",
    };

    for (const auto* uri : untrusted) {
        FakePermissionArgs args;
        args.uri = uri;
        CHECK(webview_gui::detail::handleWindowsPluginPermission(
                  &args, clipboardRead, allow, deny) == S_OK);
        CHECK(args.kindReads == 1);
        CHECK(args.uriReads == 1);
        CHECK(args.stateWrites == 1);
        CHECK(args.state == deny);
    }
}

TEST_CASE("Windows clipboard permission fails closed when its requesting URI cannot be read")
{
    constexpr int clipboardRead = 7;
    constexpr int allow = 1;
    constexpr int deny = 2;

    FakePermissionArgs failed;
    failed.uriResult = E_FAIL;
    CHECK(webview_gui::detail::handleWindowsPluginPermission(
              &failed, clipboardRead, allow, deny) == S_OK);
    CHECK(failed.stateWrites == 1);
    CHECK(failed.state == deny);

    FakePermissionArgs nullUri;
    nullUri.nullUri = true;
    CHECK(webview_gui::detail::handleWindowsPluginPermission(
              &nullUri, clipboardRead, allow, deny) == S_OK);
    CHECK(nullUri.stateWrites == 1);
    CHECK(nullUri.state == deny);
}

TEST_CASE("Windows permission policy leaves unrelated permission kinds at WebView2 defaults")
{
    constexpr int clipboardRead = 7;
    constexpr int allow = 1;
    constexpr int deny = 2;

    FakePermissionArgs args;
    args.permissionKind = 3;
    CHECK(webview_gui::detail::handleWindowsPluginPermission(
              &args, clipboardRead, allow, deny) == S_OK);
    CHECK(args.kindReads == 1);
    CHECK(args.uriReads == 0);
    CHECK(args.stateWrites == 0);
}

TEST_CASE("Windows clipboard permission propagates state write failure")
{
    constexpr int clipboardRead = 7;
    constexpr int allow = 1;
    constexpr int deny = 2;

    FakePermissionArgs args;
    args.stateResult = E_ACCESSDENIED;
    CHECK(webview_gui::detail::handleWindowsPluginPermission(
              &args, clipboardRead, allow, deny) == E_ACCESSDENIED);
    CHECK(args.stateWrites == 1);
}
