#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "webview-gui/_impl/plugin_support.h"
#include "webview-gui/_impl/local_url.h"
#include "webview-gui/_impl/origin_policy.h"

#include <set>
#include <string>
#include <vector>

using namespace webview_gui;

TEST_CASE("local plugin URLs are joined with exactly one path separator")
{
    CHECK(detail::joinLocalPluginURL("choc://choc.choc/", "/index.html")
          == "choc://choc.choc/index.html");
    CHECK(detail::joinLocalPluginURL("choc://choc.choc", "index.html")
          == "choc://choc.choc/index.html");
    CHECK(detail::joinLocalPluginURL("https://choc.localhost/", "index.html")
          == "https://choc.localhost/index.html");
    CHECK(detail::joinLocalPluginURL("https://choc.localhost", "/index.html")
          == "https://choc.localhost/index.html");
    CHECK(detail::joinLocalPluginURL("choc://choc.choc/", "")
          == "choc://choc.choc/");
}

TEST_CASE("bridge capabilities are 256-bit lowercase hex and unique")
{
    std::set<std::string> tokens;
    for (int i = 0; i < 16; ++i) {
        const auto token = detail::makeBridgeToken();
        REQUIRE(detail::isBridgeToken(token));
        CHECK(token.size() == 64);
        tokens.insert(token);
    }

    CHECK(tokens.size() == 16);
}

TEST_CASE("bridge capability is carried only in the URL fragment")
{
    const std::string token(64, 'a');
    const auto url = detail::appendBridgeTokenToURL("https://choc.localhost/index.html", token);

    REQUIRE_FALSE(url.empty());
    const auto fragment = url.find('#');
    const auto tokenPosition = url.find(token);
    REQUIRE(fragment != std::string::npos);
    REQUIRE(tokenPosition != std::string::npos);
    CHECK(tokenPosition > fragment);
    CHECK(url.substr(0, fragment) == "https://choc.localhost/index.html");

    const auto withExistingFragment = detail::appendBridgeTokenToURL("choc://choc.choc/ui#tab=main", token);
    CHECK(withExistingFragment.find("&__wg=") != std::string::npos);
}

TEST_CASE("bridge send function is capability-scoped")
{
    const std::string token(64, 'b');
    const auto name = detail::bridgeSendFunctionName(token);
    CHECK(name == "_WebviewGui_send_" + token);
    CHECK(detail::bridgeSendFunctionName("not-a-token").empty());
}

TEST_CASE("bridge capability comparison rejects every mismatched token")
{
    const std::string token(64, 'c');
    auto wrong = token;
    wrong[37] = 'd';

    CHECK(detail::constantTimeTokenEquals(token, token));
    CHECK_FALSE(detail::constantTimeTokenEquals(token, wrong));
    CHECK_FALSE(detail::constantTimeTokenEquals(token, token.substr(1)));
}

TEST_CASE("generic trusted URL policy recognizes all supported local origins")
{
    CHECK(detail::isTrustedPluginURL("choc://choc.choc/index.html"));
    CHECK(detail::isTrustedPluginURL("https://choc.localhost/index.html"));
    CHECK(detail::isTrustedPluginURL("about:blank"));

    CHECK_FALSE(detail::isTrustedPluginURL("https://choc.localhost.evil.example/"));
    CHECK_FALSE(detail::isTrustedPluginURL("https://example.com/"));
    CHECK_FALSE(detail::isTrustedPluginURL("file:///tmp/ui.html"));
}

TEST_CASE("native backend origin policies never accept another backend's local origin")
{
    CHECK(detail::isTrustedAppleLinuxPluginURL("choc://choc.choc/index.html"));
    CHECK(detail::isTrustedAppleLinuxPluginURL("about:blank"));
    CHECK_FALSE(detail::isTrustedAppleLinuxPluginURL("https://choc.localhost/index.html"));
    CHECK_FALSE(detail::isTrustedAppleLinuxPluginURL("https://example.com/"));

    CHECK(detail::isTrustedWindowsPluginURL("https://choc.localhost/index.html"));
    CHECK(detail::isTrustedWindowsPluginURL("about:blank"));
    CHECK_FALSE(detail::isTrustedWindowsPluginURL("choc://choc.choc/index.html"));
    CHECK_FALSE(detail::isTrustedWindowsPluginURL("https://example.com/"));
}

TEST_CASE("HTML hardening injects a generic capability bootstrap without embedding the secret")
{
    const std::string token(64, 'e');
    const std::string source = "<!doctype html><html><head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    CHECK(hardened.find("Content-Security-Policy") != std::string::npos);
    CHECK(hardened.find("sessionStorage") != std::string::npos);
    CHECK(hardened.find("__wg=") != std::string::npos);
    CHECK(hardened.find("_WebviewGui_receive64(token") != std::string::npos);
    CHECK(hardened.find("window['_WebviewGui_send_'+token]") != std::string::npos);

    CHECK(hardened.find(token) == std::string::npos);
}
