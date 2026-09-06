#include "webview-gui/_impl/standalone_device_bridge.h"

#include <clap/clap.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

using webview_gui::detail::StandaloneDeviceBridge;

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "standalone-device-bridge CSP test failed: %s\n", message);
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
    return extension && std::strcmp(extension, CLAP_WRAPPER_EXT_STANDALONE_DEVICE_CONTROL) == 0
        ? &kDeviceControl
        : nullptr;
}

const void *CLAP_ABI getNoExtension(const clap_host_t *, const char *)
{
    return nullptr;
}

void CLAP_ABI ignoreHostRequest(const clap_host_t *) {}

clap_host_t makeHost()
{
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.name = "standalone bridge CSP test";
    host.vendor = "webview-gui";
    host.url = "https://example.invalid";
    host.version = "1";
    host.get_extension = getExtension;
    host.request_restart = ignoreHostRequest;
    host.request_process = ignoreHostRequest;
    host.request_callback = ignoreHostRequest;
    return host;
}

std::string bytesToString(const WebviewGui::Resource &resource)
{
    return std::string(resource.bytes.begin(), resource.bytes.end());
}

} // namespace

int main()
{
    auto host = makeHost();
    StandaloneDeviceBridge bridge{&host};

    WebviewGui::Resource html;
    html.mediaType = "text/html; charset=utf-8";
    const std::string originalHtml =
        "<!doctype html><html><head>"
        "<meta http-equiv=\"Content-Security-Policy\" "
        "content=\"default-src 'none'; style-src 'self'; script-src 'self'\">"
        "</head><body><div id=\"root\"></div></body></html>";
    html.bytes.assign(originalHtml.begin(), originalHtml.end());

    bridge.injectIntoHtml(html);
    const auto injectedHtml = bytesToString(html);

    require(injectedHtml.find("style-src 'self'") != std::string::npos,
            "plug-in CSP must stay intact");
    require(injectedHtml.find("'unsafe-inline'") == std::string::npos,
            "bridge must not weaken style-src with unsafe-inline");
    require(injectedHtml.find("<style") == std::string::npos,
            "strict CSP rejects the bridge-owned inline style element");
    require(injectedHtml.find(
                "<link id=\"webview-gui-standalone-devices\" rel=\"stylesheet\" "
                "href=\"/__webview_gui/standalone-devices.css\"") != std::string::npos,
            "standalone modal styling must use a same-origin stylesheet resource");

    WebviewGui::Resource style;
    require(bridge.serveResource("/__webview_gui/standalone-devices.css", style),
            "standalone host must serve the bridge-owned stylesheet");
    require(style.mediaType == "text/css; charset=utf-8",
            "bridge stylesheet must use a CSS media type");
    const auto styleText = bytesToString(style);
    require(styleText.find("#wvg-io-button{position:fixed") != std::string::npos,
            "bridge stylesheet must keep the Audio / MIDI launcher fixed");
    require(styleText.find("#wvg-io-panel{position:fixed") != std::string::npos,
            "bridge stylesheet must keep the settings panel out of document flow");
    require(styleText.find("#wvg-io-panel[data-open=\"1\"]{display:flex}") != std::string::npos,
            "bridge stylesheet must preserve modal open state");
    require(styleText.find("<style") == std::string::npos,
            "bridge stylesheet response must contain raw CSS only");

    WebviewGui::Resource unrelated;
    require(!bridge.serveResource("/assets/app.css", unrelated),
            "bridge must not claim plug-in-owned resources");

    auto nonStandaloneHost = makeHost();
    nonStandaloneHost.get_extension = getNoExtension;
    StandaloneDeviceBridge nonStandaloneBridge{&nonStandaloneHost};
    WebviewGui::Resource hiddenStyle;
    require(!nonStandaloneBridge.serveResource("/__webview_gui/standalone-devices.css", hiddenStyle),
            "non-standalone host must not expose the bridge stylesheet");

    std::puts("standalone-device-bridge CSP contract passed");
    return 0;
}
