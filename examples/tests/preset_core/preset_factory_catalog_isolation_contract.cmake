if(NOT DEFINED CATALOG_HEADER)
    message(FATAL_ERROR "CATALOG_HEADER is required")
endif()

if(NOT EXISTS "${CATALOG_HEADER}")
    message(FATAL_ERROR "Factory preset catalog header missing: ${CATALOG_HEADER}")
endif()

file(READ "${CATALOG_HEADER}" catalog_source)

foreach(forbidden
    "#include <clap/"
    "gain_plugin"
    "gain_processor"
    "polysynth_plugin"
    "polysynth_parameter_voice_engine"
    "WebView"
    "webview-gui/webview"
    "std::filesystem")
    string(FIND "${catalog_source}" "${forbidden}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR
            "Factory preset catalog must remain format-neutral and processor/WebView/filesystem-free; found '${forbidden}'")
    endif()
endforeach()
