cmake_minimum_required(VERSION 3.24)

set(GAIN_PLUGIN_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../gain/gain_plugin.cpp")
if(NOT EXISTS "${GAIN_PLUGIN_SOURCE}")
    message(FATAL_ERROR "Gain plug-in source is missing: ${GAIN_PLUGIN_SOURCE}")
endif()

file(READ "${GAIN_PLUGIN_SOURCE}" GAIN_PLUGIN_CONTENT)

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

foreach(REQUIRED_TOKEN IN ITEMS
        "CLAP_WINDOW_API_COCOA"
        "CLAP_WINDOW_API_WIN32"
        "CLAP_WINDOW_API_X11")
    string(FIND "${GAIN_PLUGIN_CONTENT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
    if(TOKEN_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Gain native GUI preference is missing platform fallback token: ${REQUIRED_TOKEN}")
    endif()
endforeach()

string(FIND "${GAIN_PLUGIN_CONTENT}" "gui_.extHostWebview->send" HOST_WEBVIEW_OFFSET)
if(HOST_WEBVIEW_OFFSET EQUAL -1)
    message(FATAL_ERROR
        "Gain must continue to prefer the host-owned clap.webview/3 path when it is complete")
endif()

message(STATUS "Gain native GUI preference contract satisfied")
