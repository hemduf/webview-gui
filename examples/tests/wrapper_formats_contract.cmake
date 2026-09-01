cmake_minimum_required(VERSION 3.24)

foreach(required_path_var IN ITEMS EXAMPLES_CMAKE POLYSYNTH_CMAKE FORMATS_CMAKE)
    if(NOT DEFINED ${required_path_var} OR NOT EXISTS "${${required_path_var}}")
        message(FATAL_ERROR "${required_path_var} must point to an existing CMake source")
    endif()
endforeach()

file(READ "${EXAMPLES_CMAKE}" examples_cmake)
file(READ "${POLYSYNTH_CMAKE}" polysynth_cmake)
file(READ "${FORMATS_CMAKE}" formats_cmake)
set(all_cmake "${examples_cmake}\n${polysynth_cmake}\n${formats_cmake}")

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

# Keep the already-qualified native CLAP targets canonical. The clap-first
# convenience function always creates another CLAP product, so #34 uses the
# underlying thin format adapters and never duplicates processor/CLAP logic.
foreach(wrapper_api IN ITEMS
        target_add_vst3_wrapper
        target_add_auv2_wrapper
        target_add_auv3_wrapper
        target_add_auv3_standalone_wrapper
        target_add_standalone_wrapper
        target_add_aax_wrapper)
    string(FIND "${formats_cmake}" "${wrapper_api}(" api_index)
    if(api_index EQUAL -1)
        message(FATAL_ERROR "Missing clap-wrapper adapter API ${wrapper_api}")
    endif()
endforeach()

foreach(required_token IN ITEMS
        webview_gui_example_gain_clap_core
        webview_gui_example_polysynth_clap_core
        gain/gain_entry.cpp
        polysynth/polysynth_entry.cpp
        com.webview-gui.example.gain
        com.webview-gui.example.polysynth
        WebviewGuiGain
        WebviewGuiPolySynth
        webview_gui_example_gain_formats
        webview_gui_example_polysynth_formats)
    string(FIND "${all_cmake}" "${required_token}" token_index)
    if(token_index EQUAL -1)
        message(FATAL_ERROR "Wrapper integration is missing required token: ${required_token}")
    endif()
endforeach()

message(STATUS "Example wrapper format contract is present")
