cmake_minimum_required(VERSION 3.24)
project(webview_gui_gain_wclap LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBVIEW_GUI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH
    "Path to the webview-gui repository root" FORCE)
set(WEBVIEW_GUI_EXAMPLES_BUILD_GAIN ON CACHE BOOL "" FORCE)
set(WEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH OFF CACHE BOOL "" FORCE)
set(WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS OFF CACHE BOOL "" FORCE)

# Reuse the current Gain core, CLAP SDK/helper pins and embedded HTML/JS editor
# from the normal example tree. WCLAP owns only the reactor entry/package layer.
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_BINARY_DIR}/current-examples"
    EXCLUDE_FROM_ALL
)

add_executable(webview_gui_example_gain_wclap
    "${CMAKE_CURRENT_LIST_DIR}/../gain_entry.cpp"
)
target_link_libraries(webview_gui_example_gain_wclap PRIVATE
    webview_gui_example_gain_clap_core
    webview-gui
)
target_compile_features(webview_gui_example_gain_wclap PRIVATE cxx_std_17)
set_target_properties(webview_gui_example_gain_wclap PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

include("${WEBVIEW_GUI_SOURCE_DIR}/cmake/WebviewGuiWclap.cmake")
webview_gui_configure_wclap_target(webview_gui_example_gain_wclap
    OUTPUT_NAME WebviewGuiGain
)
