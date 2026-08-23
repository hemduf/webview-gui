if(NOT DEFINED MODULE OR NOT EXISTS "${MODULE}")
    message(FATAL_ERROR "MODULE must name the built plug-in qualification module")
endif()

if(PLATFORM STREQUAL "Windows")
    execute_process(
        COMMAND dumpbin /NOLOGO /EXPORTS "${MODULE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE exports
        ERROR_VARIABLE errors
    )
elseif(PLATFORM STREQUAL "Darwin")
    execute_process(
        COMMAND "${NM}" -gU "${MODULE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE exports
        ERROR_VARIABLE errors
    )
else()
    execute_process(
        COMMAND "${NM}" -D --defined-only "${MODULE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE exports
        ERROR_VARIABLE errors
    )
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "symbol scan failed (${result}): ${errors}")
endif()

if(NOT exports MATCHES "webview_gui_plugin_test_entry")
    message(FATAL_ERROR "expected plug-in ABI entry point was not exported:\n${exports}")
endif()

# Remove the one deliberately exported test ABI symbol, then reject any CHOC or
# webview-gui implementation names that escaped the plug-in boundary.
string(REPLACE "webview_gui_plugin_test_entry" "<allowed-plugin-entry>" filtered "${exports}")
string(TOLOWER "${filtered}" filtered_lower)
if(filtered_lower MATCHES "choc" OR filtered_lower MATCHES "webview_gui")
    message(FATAL_ERROR "private CHOC/webview-gui symbols escaped the module:\n${exports}")
endif()
