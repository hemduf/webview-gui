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

    return 0;
}
