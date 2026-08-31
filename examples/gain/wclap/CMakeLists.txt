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

# WASI SDK reactors do not link a C++ exception runtime. Keep this profile local
# to the WCLAP build so the native Gain target preserves its normal compiler
# settings while the exact same core sources compile without __cxa_* references.
target_compile_options(webview_gui_example_gain_clap_core PRIVATE
    -msimd128
    -fno-exceptions
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
    COMPILE_TARGETS webview_gui_example_gain_clap_core
)

# Review contract: WebView-only is a per-WCLAP compilation profile. It must be
# attached directly to the Gain core which compiles ClapWebviewGui inline code,
# and it must not leak through the shared webview-gui interface to unrelated
# native consumers in the same configure.
get_target_property(_gain_wclap_core_defs
    webview_gui_example_gain_clap_core COMPILE_DEFINITIONS)
if(NOT _gain_wclap_core_defs OR
   NOT "WEBVIEW_GUI_WEBVIEW_ONLY=1" IN_LIST _gain_wclap_core_defs)
    message(FATAL_ERROR
        "Gain WCLAP core must compile with WEBVIEW_GUI_WEBVIEW_ONLY=1 directly")
endif()

get_target_property(_webview_gui_interface_defs
    webview-gui INTERFACE_COMPILE_DEFINITIONS)
if(_webview_gui_interface_defs AND
   "WEBVIEW_GUI_WEBVIEW_ONLY=1" IN_LIST _webview_gui_interface_defs)
    message(FATAL_ERROR
        "WCLAP profile must not leak through webview-gui to unrelated consumers")
endif()
