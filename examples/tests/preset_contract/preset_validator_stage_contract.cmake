if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR)
    message(FATAL_ERROR "WEBVIEW_GUI_SOURCE_DIR is required")
endif()

set(config_file "${WEBVIEW_GUI_SOURCE_DIR}/clap-validator.toml")
if(NOT EXISTS "${config_file}")
    message(FATAL_ERROR
        "#91 requires an explicit staged clap-validator exception until #95")
endif()

file(READ "${config_file}" config)
foreach(required IN ITEMS
        "#95 MUST remove these exclusions"
        "preset-discovery-crawl = false"
        "preset-discovery-load = false"
        "preset-discovery-descriptor-consistency = false")
    string(FIND "${config}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Missing staged validator contract marker: ${required}")
    endif()
endforeach()

# Keep the exception narrowly scoped. Other validator tests must remain enabled
# by default, and #95 is responsible for deleting this staged exception rather
# than broadening it.
string(REGEX MATCHALL "[A-Za-z0-9_-]+[ \t]*=[ \t]*false" disabled_tests "${config}")
list(LENGTH disabled_tests disabled_count)
if(NOT disabled_count EQUAL 3)
    message(FATAL_ERROR
        "Only the three known pinned-validator Preset Discovery bugs may be disabled before #95; found ${disabled_count}")
endif()
