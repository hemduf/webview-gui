#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "webview-gui/_impl/plugin_support.h"
#include "webview-gui/_impl/local_url.h"
#include "webview-gui/_impl/origin_policy.h"
#include "webview-gui/_impl/resource_security.h"
#include "webview-gui/_impl/secure_random.h"

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

TEST_CASE("plugin-safe resource CORS never uses wildcard origin")
{
    CHECK(detail::appleLinuxPluginResourceAllowOrigin == "choc://choc.choc");
    CHECK(detail::windowsPluginResourceAllowOrigin == "https://choc.localhost");
    CHECK(detail::appleLinuxPluginResourceAllowOrigin != "*");
    CHECK(detail::windowsPluginResourceAllowOrigin != "*");
}

TEST_CASE("bridge capabilities use the OS CSPRNG and are 256-bit lowercase hex")
{
    std::set<std::string> tokens;
    for (int i = 0; i < 16; ++i) {
        const auto token = detail::makeSecureBridgeToken();
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

TEST_CASE("bridge bootstrap is a document-start script without HTML markup")
{
    const auto bootstrap = detail::bridgeBootstrapInitScript();
    CHECK(bootstrap.find("<script") == std::string::npos);
    CHECK(bootstrap.find("</script") == std::string::npos);
    CHECK(bootstrap.find("sessionStorage") != std::string::npos);
    CHECK(bootstrap.find("__wg=") != std::string::npos);
    CHECK(bootstrap.find("_WebviewGui_receive64(token") != std::string::npos);
    CHECK(bootstrap.find("window['_WebviewGui_send_'+token]") != std::string::npos);
}

TEST_CASE("HTML hardening injects CSP without embedding bridge bootstrap")
{
    const std::string source = "<!doctype html><html><head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    CHECK(hardened.find("Content-Security-Policy") != std::string::npos);
    CHECK(hardened.find("sessionStorage") == std::string::npos);
    CHECK(hardened.find("__wg=") == std::string::npos);
    CHECK(hardened.find("_WebviewGui_receive64(token") == std::string::npos);
}

TEST_CASE("HTML hardening ignores decoy head tags inside comments")
{
    const std::string source =
        "<!-- <head>comment decoy</head> -->"
        "<!doctype html><html><head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto commentEnd = hardened.find("-->");
    const auto title = hardened.find("<title>UI</title>");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(commentEnd != std::string::npos);
    REQUIRE(title != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp > commentEnd);
    CHECK(csp < title);
    CHECK(hardened.substr(0, commentEnd + 3).find("Content-Security-Policy") == std::string::npos);
}

TEST_CASE("HTML hardening inserts before script when the browser would imply head")
{
    const std::string source =
        "<!doctype html><html><script>const decoy = '<head>';</script>"
        "<head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto scriptStart = hardened.find("<script>");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(scriptStart != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp < scriptStart);
}

TEST_CASE("HTML hardening does not depend on fake raw-text closing syntax")
{
    const std::string source =
        "<!doctype html><html><script>const decoy = '</script=> <head><title>decoy</title></head>';"
        "</script><head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto scriptStart = hardened.find("<script>");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(scriptStart != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp < scriptStart);
}

TEST_CASE("HTML hardening does not need to emulate script double-escape states")
{
    const std::string source =
        "<!doctype html><html><script><!--<script></script>"
        "<head><title>decoy</title></head>--></script>"
        "<head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto scriptStart = hardened.find("<script>");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(scriptStart != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp < scriptStart);
}

TEST_CASE("HTML hardening synthesizes head before non-whitespace content implies it")
{
    const std::string source =
        "<!doctype html><html>attacker<head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto attacker = hardened.find("attacker");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(attacker != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp < attacker);
}

TEST_CASE("HTML hardening inserts before foreign content can imply head")
{
    const std::string source =
        "<!doctype html><html><svg><head><title>decoy</title></head></svg>"
        "<head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto svg = hardened.find("<svg>");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(svg != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp < svg);
}

TEST_CASE("HTML hardening fails closed on unterminated inert markup")
{
    const std::string source = "<!-- <head>decoy<html><head><title>UI</title></head>";
    std::vector<unsigned char> bytes(source.begin(), source.end());
    const auto original = bytes;

    CHECK_FALSE(detail::applyPluginHTMLHardening(bytes));
    CHECK(bytes == original);
}

TEST_CASE("HTML hardening inserts before template content can imply head")
{
    const std::string source =
        "<!doctype html><html><template><head><title>decoy</title></head></template>"
        "<head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto templateStart = hardened.find("<template>");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(templateStart != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp < templateStart);
}

TEST_CASE("HTML hardening inserts before header rather than treating it as head")
{
    const std::string source =
        "<!doctype html><html><header>banner</header><head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto headerStart = hardened.find("<header>");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(headerStart != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp < headerStart);
}

TEST_CASE("HTML hardening inserts before plaintext enters terminal text state")
{
    const std::string source =
        "<!doctype html><html><plaintext>decoy</plaintext>"
        "<head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto plaintextStart = hardened.find("<plaintext>");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(plaintextStart != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp < plaintextStart);
}

TEST_CASE("HTML hardening treats head-prefixed unknown tags as implied-head content")
{
    const std::string source =
        "<!doctype html><html><head.fake><title>decoy</title></head.fake>"
        "<head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    REQUIRE(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());

    const auto decoyStart = hardened.find("<head.fake>");
    const auto csp = hardened.find("Content-Security-Policy");
    REQUIRE(decoyStart != std::string::npos);
    REQUIRE(csp != std::string::npos);
    CHECK(csp < decoyStart);
}
