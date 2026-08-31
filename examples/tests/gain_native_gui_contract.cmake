cmake_minimum_required(VERSION 3.24)

set(GAIN_PLUGIN_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../gain/gain_plugin.cpp")
set(CLAP_WEBVIEW_ADAPTER_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../include/webview-gui/clap-webview-gui.h")

if(NOT EXISTS "${GAIN_PLUGIN_SOURCE}")
    message(FATAL_ERROR "Gain plug-in source is missing: ${GAIN_PLUGIN_SOURCE}")
endif()
if(NOT EXISTS "${CLAP_WEBVIEW_ADAPTER_SOURCE}")
    message(FATAL_ERROR "CLAP WebView adapter source is missing: ${CLAP_WEBVIEW_ADAPTER_SOURCE}")
endif()

file(READ "${GAIN_PLUGIN_SOURCE}" GAIN_PLUGIN_CONTENT)
file(READ "${CLAP_WEBVIEW_ADAPTER_SOURCE}" CLAP_WEBVIEW_ADAPTER_CONTENT)

set(BLOCKED_HOST_ONLY_PREFERENCE [=[    bool guiGetPreferredApi(const char **api, bool *isFloating) noexcept override {
        if (!gui_.extHostWebview || !gui_.extHostWebview->send)
            return false;
        return gui_.getPreferredApi(api, isFloating);
    }]=])
string(FIND "${GAIN_PLUGIN_CONTENT}" "${BLOCKED_HOST_ONLY_PREFERENCE}" BLOCKED_OFFSET)
if(NOT BLOCKED_OFFSET EQUAL -1)
    message(FATAL_ERROR
        "Gain GUI preference is host-WebView-only: native CLAP hosts without clap.webview/3 cannot open the bundled Web UI")
endif()

foreach(REQUIRED_DELEGATION IN ITEMS
        "return gui_.isApiSupported(api, isFloating);"
        "return gui_.getPreferredApi(api, isFloating);")
    string(FIND "${GAIN_PLUGIN_CONTENT}" "${REQUIRED_DELEGATION}" DELEGATION_OFFSET)
    if(DELEGATION_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Gain must delegate GUI API negotiation to ClapWebviewGui: ${REQUIRED_DELEGATION}")
    endif()
endforeach()

foreach(REQUIRED_ADAPTER_TOKEN IN ITEMS
        "hasHostWebviewPath()"
        "preferredNativeApi()"
        "CLAP_WINDOW_API_WEBVIEW"
        "CLAP_WINDOW_API_COCOA"
        "CLAP_WINDOW_API_WIN32"
        "CLAP_WINDOW_API_X11")
    string(FIND "${CLAP_WEBVIEW_ADAPTER_CONTENT}" "${REQUIRED_ADAPTER_TOKEN}" TOKEN_OFFSET)
    if(TOKEN_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Shared ClapWebviewGui negotiation is missing required token: ${REQUIRED_ADAPTER_TOKEN}")
    endif()
endforeach()

string(FIND "${CLAP_WEBVIEW_ADAPTER_CONTENT}" "if (hasHostWebviewPath())" HOST_WEBVIEW_PREFERENCE_OFFSET)
if(HOST_WEBVIEW_PREFERENCE_OFFSET EQUAL -1)
    message(FATAL_ERROR
        "Shared ClapWebviewGui must prefer a complete host-owned clap.webview/3 path before native API fallback")
endif()

message(STATUS "Gain native GUI preference contract satisfied")
