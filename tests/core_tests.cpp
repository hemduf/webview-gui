#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "webview-gui/helpers.h"
#include "webview-gui/_impl/plugin_support.h"
#include "webview-gui/_impl/bounded_buffer.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace webview_gui;

TEST_CASE("Base64 helpers preserve arbitrary binary payloads")
{
    for (size_t length = 0; length <= 4096; ++length) {
        CAPTURE(length);

        std::vector<unsigned char> input(length);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<unsigned char>((i * 131u + length * 17u) & 0xffu);

        const auto encoded = helpers::encodeBase64(input.data(), input.size());
        const auto decoded = helpers::decodeBase64(encoded.c_str());
        CHECK(decoded == input);
    }
}

TEST_CASE("ThreadAffinity is unbound until explicitly attached to the GUI thread")
{
    detail::ThreadAffinity affinity;
    CHECK_FALSE(affinity.isBound());
    CHECK(affinity.isCurrentThread());

    affinity.bindToCurrentThread();
    CHECK(affinity.isBound());
    CHECK(affinity.isCurrentThread());
}

TEST_CASE("ThreadAffinity rejects a different worker/audio thread")
{
    detail::ThreadAffinity affinity;
    affinity.bindToCurrentThread();

    std::atomic<bool> workerAccepted{true};
    std::thread worker([&] {
        workerAccepted.store(affinity.isCurrentThread(), std::memory_order_relaxed);
    });
    worker.join();

    CHECK_FALSE(workerAccepted.load(std::memory_order_relaxed));
    CHECK(affinity.isCurrentThread());
}

TEST_CASE("PointerRegistry lookup misses never mutate the registry")
{
    detail::PointerRegistry<int> registry;
    int key = 0;

    CHECK(registry.size() == 0);
    CHECK(registry.find(&key) == nullptr);
    CHECK(registry.size() == 0);
}

TEST_CASE("PointerRegistry keeps instances isolated and removes only the matching owner")
{
    detail::PointerRegistry<int> registry;
    int keyA = 0;
    int keyB = 0;
    int valueA = 11;
    int valueB = 22;

    registry.set(&keyA, &valueA);
    registry.set(&keyB, &valueB);

    CHECK(registry.find(&keyA) == &valueA);
    CHECK(registry.find(&keyB) == &valueB);
    CHECK(registry.size() == 2);

    registry.set(&keyA, &valueB);
    CHECK_FALSE(registry.eraseIfMatches(&keyA, &valueA));
    CHECK(registry.find(&keyA) == &valueB);
    CHECK(registry.eraseIfMatches(&keyA, &valueB));
    CHECK(registry.find(&keyA) == nullptr);
}

TEST_CASE("PointerRegistry tolerates concurrent readers while registrations change")
{
    detail::PointerRegistry<int> registry;
    int key = 0;
    int value = 22;
    std::atomic<bool> stop{false};

    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            (void)registry.find(&key);
            (void)registry.size();
        }
    });

    for (int i = 0; i < 10000; ++i) {
        registry.set(&key, &value);
        CHECK(registry.find(&key) == &value);
        registry.eraseIfMatches(&key, &value);
    }

    stop.store(true, std::memory_order_relaxed);
    reader.join();
}

TEST_CASE("Resource paths remain inside the configured root")
{
    const auto root = std::filesystem::path("/tmp/webview-gui-root");
    std::filesystem::path resolved;

    CHECK(detail::resolveContainedPath(root, "/assets/ui.js", resolved));
    CHECK(resolved == (root / "assets/ui.js").lexically_normal());

    CHECK(detail::resolveContainedPath(root, "images/../images/knob.png", resolved));
    CHECK(resolved == (root / "images/knob.png").lexically_normal());
}

TEST_CASE("Resource path traversal is rejected")
{
    const auto root = std::filesystem::path("/tmp/webview-gui-root");
    std::filesystem::path resolved;

    CHECK_FALSE(detail::resolveContainedPath(root, "../secret.txt", resolved));
    CHECK_FALSE(detail::resolveContainedPath(root, "/../../secret.txt", resolved));
    CHECK_FALSE(detail::resolveContainedPath(root, "assets/../../../secret.txt", resolved));
}

TEST_CASE("Percent-encoded traversal is decoded before containment checks")
{
    std::string decoded;
    CHECK(detail::decodeURLPath("/assets%2Fknob.png", decoded));
    CHECK(decoded == "/assets/knob.png");

    CHECK(detail::decodeURLPath("/%2e%2e/%2e%2e/secret.txt", decoded));
    CHECK(decoded == "/../../secret.txt");

    CHECK_FALSE(detail::decodeURLPath("/%00secret", decoded));
    CHECK_FALSE(detail::decodeURLPath("/%GGsecret", decoded));
}

TEST_CASE("Canonical resource containment rejects symlink escapes")
{
    const auto base = std::filesystem::temp_directory_path() / "webview-gui-security-test";
    const auto root = base / "root";
    const auto outside = base / "outside";

    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(root / "assets");
    std::filesystem::create_directories(outside);

    {
        std::ofstream(root / "assets" / "ui.js") << "ok";
        std::ofstream(outside / "secret.txt") << "secret";
    }

    std::filesystem::path resolved;
    CHECK(detail::resolveContainedExistingPath(root, "/assets/ui.js", resolved));
    CHECK(std::filesystem::equivalent(resolved, root / "assets" / "ui.js"));
    CHECK_FALSE(detail::resolveContainedExistingPath(root, "/%2e%2e/outside/secret.txt", resolved));

#if !defined(_WIN32)
    std::filesystem::create_directory_symlink(outside, root / "escape", ec);
    REQUIRE_FALSE(ec);
    CHECK_FALSE(detail::resolveContainedExistingPath(root, "/escape/secret.txt", resolved));
#endif

    std::filesystem::remove_all(base, ec);
}

TEST_CASE("Plugin-safe URLs reject remote and privileged schemes")
{
    CHECK(detail::isTrustedPluginURL("choc://choc.choc/index.html"));
    CHECK(detail::isTrustedPluginURL("about:blank"));

    CHECK_FALSE(detail::isTrustedPluginURL("https://example.com/plugin-ui"));
    CHECK_FALSE(detail::isTrustedPluginURL("http://127.0.0.1:1234/"));
    CHECK_FALSE(detail::isTrustedPluginURL("file:///tmp/ui.html"));
    CHECK_FALSE(detail::isTrustedPluginURL("javascript:alert(1)"));
    CHECK_FALSE(detail::isTrustedPluginURL("data:text/html,<script>alert(1)</script>"));
}

TEST_CASE("Plugin payload limits reject unbounded messages and resources")
{
    CHECK(detail::messageSizeAllowed(detail::maxMessageBytes));
    CHECK_FALSE(detail::messageSizeAllowed(detail::maxMessageBytes + 1));

    CHECK(detail::resourceSizeAllowed(detail::maxResourceBytes));
    CHECK_FALSE(detail::resourceSizeAllowed(detail::maxResourceBytes + 1));

    const auto maxEncodedMessage = ((detail::maxMessageBytes + 2) / 3) * 4;
    CHECK(detail::base64MessageSizeAllowed(maxEncodedMessage));
    CHECK_FALSE(detail::base64MessageSizeAllowed(maxEncodedMessage + 4));
}

TEST_CASE("Bounded resource appends never grow beyond the configured limit")
{
    std::vector<unsigned char> bytes{1, 2, 3};
    const unsigned char tail[] = {4, 5};

    CHECK(detail::appendBoundedBytes(bytes, tail, sizeof(tail), 5));
    CHECK(bytes == std::vector<unsigned char>({1, 2, 3, 4, 5}));

    const unsigned char overflow[] = {6};
    CHECK_FALSE(detail::appendBoundedBytes(bytes, overflow, sizeof(overflow), 5));
    CHECK(bytes == std::vector<unsigned char>({1, 2, 3, 4, 5}));

    CHECK_FALSE(detail::appendBoundedBytes(bytes, nullptr, 1, 5));
}

TEST_CASE("HTML resources receive a restrictive plugin CSP before page scripts")
{
    const std::string source = "<!doctype html><html><head><script src=\"https://evil.example/x.js\"></script></head><body></body></html>";
    std::vector<unsigned char> html(source.begin(), source.end());

    REQUIRE(detail::applyPluginContentSecurityPolicy(html));

    const std::string hardened(html.begin(), html.end());
    const auto policy = hardened.find("Content-Security-Policy");
    const auto externalScript = hardened.find("https://evil.example/x.js");

    REQUIRE(policy != std::string::npos);
    REQUIRE(externalScript != std::string::npos);
    CHECK(policy < externalScript);
    CHECK(hardened.find("default-src 'self'") != std::string::npos);
    CHECK(hardened.find("connect-src 'self'") != std::string::npos);
    CHECK(hardened.find("frame-src 'none'") != std::string::npos);
    CHECK(hardened.find("object-src 'none'") != std::string::npos);
}
