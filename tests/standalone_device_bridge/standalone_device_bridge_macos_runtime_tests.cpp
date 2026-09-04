#include "webview-gui/_impl/standalone_device_bridge.h"
#include "webview-gui/webview-gui.h"

#include <CoreFoundation/CoreFoundation.h>
#include <clap/clap.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using webview_gui::detail::StandaloneDeviceBridge;

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "standalone-device-bridge macOS runtime test failed: %s\n", message);
    std::exit(1);
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

const clap_wrapper_host_standalone_device_control kDeviceControl{};

const void *CLAP_ABI getExtension(const clap_host_t *, const char *extension)
{
    if (!extension)
        return nullptr;
    return std::strcmp(extension, CLAP_WRAPPER_EXT_STANDALONE_DEVICE_CONTROL) == 0
        ? &kDeviceControl
        : nullptr;
}

void CLAP_ABI ignoreHostRequest(const clap_host_t *) {}

clap_host_t makeHost()
{
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.name = "standalone WKWebView bridge test";
    host.vendor = "webview-gui";
    host.url = "https://example.invalid";
    host.version = "1";
    host.get_extension = getExtension;
    host.request_restart = ignoreHostRequest;
    host.request_process = ignoreHostRequest;
    host.request_callback = ignoreHostRequest;
    return host;
}

void pumpMainRunLoop(double seconds = 0.01)
{
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, true);
}

void pumpFor(std::chrono::milliseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline)
        pumpMainRunLoop();
}

std::vector<unsigned char> makeSnapshot()
{
    static constexpr const char json[] =
        "{\"type\":\"standalone-devices\","
        "\"audioApis\":[],"
        "\"audioInputs\":[],"
        "\"audioOutputs\":[],"
        "\"midiInputs\":[],"
        "\"midiOutputs\":[],"
        "\"sampleRates\":[48000],"
        "\"bufferSizes\":[256],"
        "\"config\":{"
        "\"audioInputId\":0,"
        "\"audioOutputId\":0,"
        "\"audioInputEnabled\":false,"
        "\"audioOutputEnabled\":true,"
        "\"pluginHasInput\":false,"
        "\"pluginHasOutput\":true,"
        "\"inputChannels\":0,"
        "\"outputChannels\":2,"
        "\"sampleRate\":48000,"
        "\"bufferSize\":256}}";

    std::vector<unsigned char> result{'W', 'V', 'D', 'J'};
    result.insert(result.end(), json, json + sizeof(json) - 1);
    return result;
}

} // namespace

int main()
{
    auto host = makeHost();
    StandaloneDeviceBridge bridge{&host};
    require(bridge.available(), "fake standalone extension is not visible");

    auto gui = WebviewGui::createUnique(
        WebviewGui::COCOA,
        "/index.html",
        [&](const char *path, WebviewGui::Resource &resource)
        {
            if (!path)
                return false;

            if (std::strcmp(path, "/index.html") == 0) {
                static constexpr const char html[] =
                    "<!doctype html><html><head><title>standalone-bridge</title></head>"
                    "<body><main>editor</main></body></html>";
                resource.mediaType = "text/html; charset=utf-8";
                resource.bytes.assign(html, html + sizeof(html) - 1);
                bridge.injectIntoHtml(resource);
                return true;
            }

            return bridge.provideResource(path, resource);
        });

    require(gui != nullptr, "WKWebView creation failed");

    std::atomic<int> queryCount{0};
    const auto snapshot = makeSnapshot();

    // The page can begin loading before receive is assigned. The production
    // bridge must therefore retry WVDQ long enough for this callback to become
    // available, then stop probing once WVDJ has been accepted by the script.
    gui->receive = [&](const unsigned char *bytes, std::size_t size)
    {
        if (size != 4 || std::memcmp(bytes, "WVDQ", 4) != 0)
            return;
        queryCount.fetch_add(1, std::memory_order_relaxed);
        gui->send(snapshot.data(), snapshot.size());
    };

    pumpFor(std::chrono::milliseconds{1400});

    const auto queries = queryCount.load(std::memory_order_relaxed);
    require(queries > 0, "injected JavaScript never reached the native bridge");
    require(queries <= 6,
            "WVDJ was not accepted by the JavaScript receiver; bounded probe kept retrying");

    std::puts("standalone-device-bridge WKWebView runtime smoke passed");
    return 0;
}
