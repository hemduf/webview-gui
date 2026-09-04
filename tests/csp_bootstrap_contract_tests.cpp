#include "webview-gui/_impl/plugin_support.h"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using namespace webview_gui;

    const auto initScript = detail::bridgeBootstrapInitScript();
    assert(initScript.find("<script") == std::string::npos);
    assert(initScript.find("</script") == std::string::npos);
    assert(initScript.find("sessionStorage") != std::string::npos);
    assert(initScript.find("__wg=") != std::string::npos);
    assert(initScript.find("_WebviewGui_receive64(token") != std::string::npos);
    assert(initScript.find("window['_WebviewGui_send_'+token]") != std::string::npos);

    // Native pages use window.parent.postMessage for the same binary bridge API
    // as host-owned WCLAP pages. The document-start shim must forward those
    // binary self-posts directly instead of relying on MessageEvent.source,
    // whose WindowProxy identity is not stable across all embedded WebViews.
    assert(initScript.find("const nativePostMessage=window.postMessage.bind(window)") !=
           std::string::npos);
    assert(initScript.find("window.postMessage=function(data,targetOrigin,transfer)") !=
           std::string::npos);
    assert(initScript.find("if(forwardToNative(data))return") != std::string::npos);

    // Native-to-page delivery must bypass the inbound capture bridge. Otherwise
    // a C++ UI snapshot is immediately reflected back to native and stopped
    // before the plug-in's ordinary window.message listener can observe it.
    assert(initScript.find("source:null") != std::string::npos);
    assert(initScript.find("data:d.buffer,source:window") == std::string::npos);

    const std::string source =
        "<!-- <head>decoy</head> -->"
        "<!doctype html><html><head><title>UI</title></head><body></body></html>";
    std::vector<unsigned char> bytes(source.begin(), source.end());

    assert(detail::applyPluginHTMLHardening(bytes));
    const std::string hardened(bytes.begin(), bytes.end());
    assert(hardened.find("Content-Security-Policy") != std::string::npos);

    // The bridge bootstrap must be a document-start script, not HTML markup.
    assert(hardened.find("sessionStorage") == std::string::npos);
    assert(hardened.find("_WebviewGui_ready") == std::string::npos);
    assert(hardened.find("_WebviewGui_receive64(token") == std::string::npos);

    // A UTF-8 BOM is an encoding signature and must remain at byte offset zero.
    // Moving injected markup ahead of it changes how the browser can decode the page.
    const std::string bomSource =
        "\xEF\xBB\xBF<!doctype html><html><head><title>BOM</title></head><body></body></html>";
    std::vector<unsigned char> bomBytes(bomSource.begin(), bomSource.end());
    assert(detail::applyPluginHTMLHardening(bomBytes));
    assert(bomBytes.size() >= 3);
    assert(bomBytes[0] == 0xEFu);
    assert(bomBytes[1] == 0xBBu);
    assert(bomBytes[2] == 0xBFu);

    return 0;
}
