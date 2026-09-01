cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED EXAMPLES_CMAKE OR NOT EXISTS "${EXAMPLES_CMAKE}")
    message(FATAL_ERROR "EXAMPLES_CMAKE must point to examples/CMakeLists.txt")
endif()
if(NOT DEFINED POLYSYNTH_CMAKE OR NOT EXISTS "${POLYSYNTH_CMAKE}")
    message(FATAL_ERROR "POLYSYNTH_CMAKE must point to examples/polysynth/CMakeLists.txt")
endif()

file(READ "${EXAMPLES_CMAKE}" examples_cmake)
file(READ "${POLYSYNTH_CMAKE}" polysynth_cmake)
set(all_cmake "${examples_cmake}\n${polysynth_cmake}")

foreach(option_name IN ITEMS
        WEBVIEW_GUI_EXAMPLES_FORMAT_CLAP
        WEBVIEW_GUI_EXAMPLES_FORMAT_VST3
        WEBVIEW_GUI_EXAMPLES_FORMAT_AUV2
        WEBVIEW_GUI_EXAMPLES_FORMAT_AUV3
        WEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE
        WEBVIEW_GUI_EXAMPLES_FORMAT_AAX)
    string(FIND "${examples_cmake}" "option(${option_name}" option_index)
    if(option_index EQUAL -1)
        message(FATAL_ERROR "Missing explicit format option ${option_name}")
    endif()
endforeach()

if(NOT examples_cmake MATCHES "option\\(WEBVIEW_GUI_EXAMPLES_FORMAT_AAX[^\\n]* OFF\\)")
    message(FATAL_ERROR "AAX must remain explicitly OFF by default")
endif()

string(REGEX MATCHALL "make_clapfirst_plugins\\(" clapfirst_calls "${all_cmake}")
list(LENGTH clapfirst_calls clapfirst_count)
if(clapfirst_count LESS 2)
    message(FATAL_ERROR "Gain and PolySynth must each use make_clapfirst_plugins()")
endif()

foreach(required_token IN ITEMS
        webview_gui_example_gain_clap_core
        webview_gui_example_polysynth_clap_core
        gain/gain_entry.cpp
        polysynth_entry.cpp
        com.webview-gui.example.gain
        com.webview-gui.example.polysynth
        WebviewGuiGain
        WebviewGuiPolySynth)
    string(FIND "${all_cmake}" "${required_token}" token_index)
    if(token_index EQUAL -1)
        message(FATAL_ERROR "Wrapper integration is missing required token: ${required_token}")
    endif()
endforeach()

message(STATUS "Example wrapper format contract is present")
