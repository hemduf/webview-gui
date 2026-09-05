if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR)
    message(FATAL_ERROR "WEBVIEW_GUI_SOURCE_DIR is required")
endif()

set(config_file "${WEBVIEW_GUI_SOURCE_DIR}/clap-validator.toml")
if(EXISTS "${config_file}")
    file(READ "${config_file}" config)
else()
    set(config "")
endif()

foreach(forbidden IN ITEMS
        "preset-discovery-crawl = false"
        "preset-discovery-load = false"
        "preset-discovery-descriptor-consistency = false")
    string(FIND "${config}" "${forbidden}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR
            "#95 requires Preset Discovery validation to be blocking; remove exclusion: ${forbidden}")
    endif()
endforeach()

# Once #95 starts, no validator test may be silently disabled by repository
# configuration. Any future exclusion needs its own reviewed qualification path.
string(REGEX MATCHALL "[A-Za-z0-9_-]+[ \t]*=[ \t]*false" disabled_tests "${config}")
list(LENGTH disabled_tests disabled_count)
if(disabled_count GREATER 0)
    message(FATAL_ERROR
        "#95 forbids advisory clap-validator tests; found ${disabled_count} disabled test(s) in clap-validator.toml")
endif()

message(STATUS "#95 clap-validator preset exclusions are retired")
