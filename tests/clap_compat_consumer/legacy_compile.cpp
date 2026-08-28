#include "webview-gui/clap-webview-gui.h"

#include <cstring>
#include <type_traits>

static_assert(std::is_same<webview_gui::clap_plugin_webview,
                           clap_plugin_webview_t>::value,
              "fallback plugin WebView ABI must expose the canonical typedef");
static_assert(std::is_same<webview_gui::clap_host_webview,
                           clap_host_webview_t>::value,
              "fallback host WebView ABI must expose the canonical typedef");

int main()
{
    return std::strcmp(webview_gui::CLAP_EXT_WEBVIEW, "clap.webview/3") == 0
               && std::strcmp(webview_gui::CLAP_WINDOW_API_WEBVIEW, "webview") == 0
           ? 0
           : 1;
}
