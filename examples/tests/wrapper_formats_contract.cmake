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

string(REGEX MATCH
    "option\\(WEBVIEW_GUI_EXAMPLES_FORMAT_AAX[^)]*OFF\\)"
    aax_default_off "${examples_cmake}")
if("${aax_default_off}" STREQUAL "")
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
        webview_gui_example_polysynth_formats
        SUPPORTS_ALL_NOTE_EXPRESSIONS)
    string(FIND "${all_cmake}" "${required_token}" token_index)
    if(token_index EQUAL -1)
        message(FATAL_ERROR "Wrapper integration is missing required token: ${required_token}")
    endif()
endforeach()

# Regression contracts discovered by the cross-platform #34 smoke matrix:
# - Linux VST3 links static wrapper/VST SDK archives into a module and therefore
#   requires those archives to be PIC.
# - VS 2026 turns clap-wrapper 0.16.0's <experimental/coroutine> deprecation into
#   a hard error unless the pinned wrapper target opts into the documented shim.
# - Xcode 26 rejects the pinned AUv3 host's unscoped switch case because its block
#   capture has a lifetime that crosses the following case label. Keep the patch
#   explicit and fail-closed in the pinned-wrapper compatibility layer.
# #35 adds two distributable VST3 requirements discovered by real validator and
# artifact runs: Windows must use Steinberg's bundle layout, and wrapper modules
# must keep implementation/inlined WebView symbols hidden just like native CLAP.
foreach(required_compatibility_token IN ITEMS
        "set_property(TARGET \${target}-clap-wrapper-vst3-lib PROPERTY POSITION_INDEPENDENT_CODE ON)"
        "set_property(TARGET base-sdk-vst3 PROPERTY POSITION_INDEPENDENT_CODE ON)"
        "WINDOWS_FOLDER_VST3 TRUE"
        "CXX_VISIBILITY_PRESET hidden"
        "VISIBILITY_INLINES_HIDDEN YES"
        "_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS"
        "webview_gui_apply_clap_wrapper_auv3_host_switch_patch")
    string(FIND "${all_cmake}" "${required_compatibility_token}" compatibility_index)
    if(compatibility_index EQUAL -1)
        message(FATAL_ERROR
            "Missing pinned-wrapper compatibility contract: ${required_compatibility_token}")
    endif()
endforeach()

message(STATUS "Example wrapper format contract is present")
