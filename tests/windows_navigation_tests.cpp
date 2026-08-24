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

    HRESULT get_Uri(LPWSTR* output)
    {
        ++uriReads;
        if (!output) return E_POINTER;
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
        cancelled = value != FALSE;
        return S_OK;
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
