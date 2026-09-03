if(NOT DEFINED WEBVIEW_GUI_ROOT OR "${WEBVIEW_GUI_ROOT}" STREQUAL "")
    message(FATAL_ERROR "WEBVIEW_GUI_ROOT is required")
endif()

function(read_required relative_path out_var)
    set(path "${WEBVIEW_GUI_ROOT}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missing required file: ${relative_path}")
    endif()
    file(READ "${path}" content)
    set(${out_var} "${content}" PARENT_SCOPE)
endfunction()

read_required("examples/gain/wclap/CMakeLists.txt" gain_wclap)
read_required("examples/polysynth/wclap/CMakeLists.txt" poly_wclap)
read_required("examples/cmake/ExampleFormats.cmake" wrappers)

string(FIND "${gain_wclap}" "RESOURCE_DIRECTORY" gain_resource_index)
if(gain_resource_index EQUAL -1)
    message(FATAL_ERROR
        "#104 RED: Gain WCLAP does not package a canonical preset resource directory")
endif()

string(FIND "${poly_wclap}" "RESOURCE_DIRECTORY" poly_resource_index)
if(poly_resource_index EQUAL -1)
    message(FATAL_ERROR
        "#104 RED: PolySynth WCLAP does not package a canonical preset resource directory")
endif()

# Native wrapper packaging must not deliberately pass an empty resource directory.
string(FIND "${wrappers}" "RESOURCE_DIRECTORY \"\"" empty_native_resource)
if(NOT empty_native_resource EQUAL -1)
    message(FATAL_ERROR
        "#104 RED: native wrapper formats still disable resource packaging with RESOURCE_DIRECTORY \"\"")
endif()

message(STATUS "#104 preset packaging wiring contract satisfied")
