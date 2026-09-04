cmake_minimum_required(VERSION 3.24)
project(webview_gui_polysynth_wclap LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBVIEW_GUI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH
    "Path to the webview-gui repository root" FORCE)
set(WEBVIEW_GUI_EXAMPLES_BUILD_GAIN OFF CACHE BOOL "" FORCE)
set(WEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH ON CACHE BOOL "" FORCE)
set(WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS OFF CACHE BOOL "" FORCE)

# Reuse the exact PolySynth CLAP core and dependency pins from the normal example
# tree. WCLAP owns only the reactor entry and package layer.
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_BINARY_DIR}/current-examples"
    EXCLUDE_FROM_ALL
)

# WASI SDK reactors do not link a C++ exception runtime. Keep the profile local
# to this WCLAP build; native PolySynth keeps its normal compiler settings. The
# canonical preset_production_catalog.h provides the factory-only WASI catalog.
target_compile_options(webview_gui_example_polysynth_clap_core PRIVATE
    -msimd128
    -fno-exceptions
)

# The pinned WCLAP bridge performs its historical pre-init clap.webview/3 probe
# regardless of whether the current PolySynth advertises a GUI. Adapt only this
# WCLAP factory boundary and keep the native strict clap-helpers instance intact.
add_executable(webview_gui_example_polysynth_wclap
    "${CMAKE_CURRENT_LIST_DIR}/polysynth_wclap_entry.cpp"
)
target_link_libraries(webview_gui_example_polysynth_wclap PRIVATE
    webview_gui_example_polysynth_clap_core
    webview-gui
)
target_compile_features(webview_gui_example_polysynth_wclap PRIVATE cxx_std_17)
set_target_properties(webview_gui_example_polysynth_wclap PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

include("${WEBVIEW_GUI_SOURCE_DIR}/examples/cmake/PresetResources.cmake")
include("${WEBVIEW_GUI_SOURCE_DIR}/cmake/WebviewGuiWclap.cmake")
webview_gui_configure_wclap_target(webview_gui_example_polysynth_wclap
    OUTPUT_NAME WebviewGuiPolySynth
    COMPILE_TARGETS webview_gui_example_polysynth_clap_core
    RESOURCE_DIRECTORY "${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_DIRECTORY}"
)

get_target_property(_polysynth_wclap_core_defs
    webview_gui_example_polysynth_clap_core COMPILE_DEFINITIONS)
if(NOT _polysynth_wclap_core_defs OR
   NOT "WEBVIEW_GUI_WEBVIEW_ONLY=1" IN_LIST _polysynth_wclap_core_defs)
    message(FATAL_ERROR
        "PolySynth WCLAP core must compile with WEBVIEW_GUI_WEBVIEW_ONLY=1 directly")
endif()

get_target_property(_webview_gui_interface_defs
    webview-gui INTERFACE_COMPILE_DEFINITIONS)
if(_webview_gui_interface_defs AND
   "WEBVIEW_GUI_WEBVIEW_ONLY=1" IN_LIST _webview_gui_interface_defs)
    message(FATAL_ERROR
        "WCLAP profile must not leak through webview-gui to unrelated consumers")
endif()
