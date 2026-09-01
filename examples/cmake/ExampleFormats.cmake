include_guard(GLOBAL)

if(NOT WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS)
    return()
endif()

set(WEBVIEW_GUI_EXAMPLES_ASSET_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/artifacts" CACHE PATH
    "Predictable build-tree directory for wrapped example products")

# clap-wrapper 0.16.0 compiles its Windows support libraries with the static MSVC
# runtime. The canonical CLAP/core targets are created before FetchContent enters
# the wrapper directory, so their default would otherwise remain /MD[d] while the
# wrapper objects use /MT[d], which MSVC rejects at link time. Apply the same
# runtime to every compiled target already present in the examples subtree and
# keep the variable set for wrapper/SDK targets created below.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    function(webview_gui_examples_set_static_msvc_runtime directory)
        get_property(example_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(example_target IN LISTS example_targets)
            get_target_property(example_target_imported ${example_target} IMPORTED)
            get_target_property(example_target_type ${example_target} TYPE)
            if(NOT example_target_imported AND
               (example_target_type STREQUAL "EXECUTABLE" OR
                example_target_type STREQUAL "STATIC_LIBRARY" OR
                example_target_type STREQUAL "SHARED_LIBRARY" OR
                example_target_type STREQUAL "MODULE_LIBRARY" OR
                example_target_type STREQUAL "OBJECT_LIBRARY"))
                set_property(TARGET ${example_target} PROPERTY
                    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            endif()
        endforeach()

        get_property(example_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        foreach(example_subdirectory IN LISTS example_subdirectories)
            webview_gui_examples_set_static_msvc_runtime("${example_subdirectory}")
        endforeach()
    endfunction()

    webview_gui_examples_set_static_msvc_runtime("${CMAKE_CURRENT_SOURCE_DIR}")
endif()

function(webview_gui_add_example_wrappers)
    set(options SUPPORTS_ALL_NOTE_EXPRESSIONS)
    set(one_value_args
        TARGET_PREFIX
        CLAP_TARGET
        IMPL_TARGET
        ENTRY_SOURCE
        OUTPUT_NAME
        PLUGIN_ID
        BUNDLE_IDENTIFIER
        AU_SUBTYPE
        AU_TYPE)
    set(multi_value_args EXTRA_LINK_LIBRARIES)
    cmake_parse_arguments(FMT "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    foreach(required_arg IN ITEMS
            TARGET_PREFIX CLAP_TARGET IMPL_TARGET ENTRY_SOURCE OUTPUT_NAME
            PLUGIN_ID BUNDLE_IDENTIFIER AU_SUBTYPE AU_TYPE)
        if(NOT DEFINED FMT_${required_arg} OR "${FMT_${required_arg}}" STREQUAL "")
            message(FATAL_ERROR "webview-gui formats: ${required_arg} is required")
        endif()
    endforeach()
    if(NOT TARGET ${FMT_IMPL_TARGET})
        message(FATAL_ERROR "webview-gui formats: missing implementation target ${FMT_IMPL_TARGET}")
    endif()
    if(NOT TARGET ${FMT_CLAP_TARGET})
        message(FATAL_ERROR "webview-gui formats: missing canonical CLAP target ${FMT_CLAP_TARGET}")
    endif()

    set(all_target "${FMT_TARGET_PREFIX}_all")
    add_custom_target(${all_target})

    if(WEBVIEW_GUI_EXAMPLES_FORMAT_CLAP)
        add_dependencies(${all_target} ${FMT_CLAP_TARGET})
    endif()

    set(common_libraries ${FMT_IMPL_TARGET} ${FMT_EXTRA_LINK_LIBRARIES})
    set(asset_root "${WEBVIEW_GUI_EXAMPLES_ASSET_OUTPUT_DIRECTORY}")

    if(WEBVIEW_GUI_EXAMPLES_FORMAT_VST3)
        set(target "${FMT_TARGET_PREFIX}_vst3")
        add_library(${target} MODULE ${FMT_ENTRY_SOURCE})
        target_link_libraries(${target} PRIVATE ${common_libraries})
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_add_vst3_wrapper(
            TARGET ${target}
            OUTPUT_NAME "${FMT_OUTPUT_NAME}"
            SUPPORTS_ALL_NOTE_EXPRESSIONS "${FMT_SUPPORTS_ALL_NOTE_EXPRESSIONS}"
            BUNDLE_IDENTIFIER "${FMT_BUNDLE_IDENTIFIER}.vst3"
            BUNDLE_VERSION "0.1.0"
            WINDOWS_FOLDER_VST3 FALSE)

        # Linux VST3 is a shared module assembled from clap-wrapper and Steinberg
        # static archives. clap-wrapper 0.16.0 does not mark those archives PIC,
        # which produces R_X86_64_PC32 relocation failures at final link time.
        # Apply PIC only where ELF shared-module linking requires it; macOS and
        # Windows use their native wrapper paths unchanged.
        if(UNIX AND NOT APPLE)
            if(TARGET ${target}-clap-wrapper-vst3-lib)
                set_property(TARGET ${target}-clap-wrapper-vst3-lib PROPERTY POSITION_INDEPENDENT_CODE ON)
            else()
                message(FATAL_ERROR
                    "Pinned clap-wrapper VST3 helper target changed; revalidate the Linux PIC compatibility rule")
            endif()
            if(TARGET base-sdk-vst3)
                set_property(TARGET base-sdk-vst3 PROPERTY POSITION_INDEPENDENT_CODE ON)
            else()
                message(FATAL_ERROR
                    "Pinned clap-wrapper VST3 SDK target changed; revalidate the Linux PIC compatibility rule")
            endif()
        endif()

        # clap-wrapper 0.16.0 has a platform-specific target-name typo in its
        # ASSET_OUTPUT_DIRECTORY branch on macOS/Windows. Keep the wrapper's
        # native VST3 output layout instead of exercising that broken branch.
        add_dependencies(${all_target} ${target})
    endif()

    if(WEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE)
        set(target "${FMT_TARGET_PREFIX}_standalone")
        add_executable(${target} ${FMT_ENTRY_SOURCE})
        target_link_libraries(${target} PRIVATE ${common_libraries})
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_add_standalone_wrapper(
            TARGET ${target}
            OUTPUT_NAME "${FMT_OUTPUT_NAME}"
            BUNDLE_IDENTIFIER "${FMT_BUNDLE_IDENTIFIER}.standalone"
            BUNDLE_VERSION "0.1.0"
            STATICALLY_LINKED_CLAP_ENTRY TRUE
            PLUGIN_ID "${FMT_PLUGIN_ID}"
            RESOURCE_DIRECTORY "")

        # The pinned Windows standalone still includes MSVC's deprecated
        # <experimental/coroutine>. VS 2026 deliberately turns that deprecation
        # into STL1011 unless the documented compatibility macro is defined.
        # Keep this scoped to the wrapper-bearing standalone target so normal
        # webview-gui consumers do not inherit a global STL suppression.
        if(MSVC)
            target_compile_definitions(${target} PRIVATE
                _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
        endif()

        if(APPLE)
            set_target_properties(${target} PROPERTIES
                MACOSX_BUNDLE_GUI_IDENTIFIER "${FMT_BUNDLE_IDENTIFIER}.standalone"
                RUNTIME_OUTPUT_DIRECTORY "${asset_root}")
        elseif(WIN32)
            set_target_properties(${target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${asset_root}/Standalone")
        else()
            set_target_properties(${target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${asset_root}")
        endif()
        add_dependencies(${all_target} ${target})
    endif()

    if(WEBVIEW_GUI_EXAMPLES_FORMAT_AUV2)
        if(NOT APPLE)
            message(FATAL_ERROR "WEBVIEW_GUI_EXAMPLES_FORMAT_AUV2 is supported only on Apple platforms")
        endif()
        set(target "${FMT_TARGET_PREFIX}_auv2")
        add_library(${target} MODULE ${FMT_ENTRY_SOURCE})
        target_link_libraries(${target} PRIVATE ${common_libraries})
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_add_auv2_wrapper(
            TARGET ${target}
            OUTPUT_NAME "${FMT_OUTPUT_NAME}"
            BUNDLE_IDENTIFIER "${FMT_BUNDLE_IDENTIFIER}.auv2"
            BUNDLE_VERSION "0.1.0"
            RESOURCE_DIRECTORY ""
            MANUFACTURER_NAME "webview-gui"
            MANUFACTURER_CODE "WvGu"
            SUBTYPE_CODE "${FMT_AU_SUBTYPE}"
            INSTRUMENT_TYPE "${FMT_AU_TYPE}")
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${asset_root}")
        add_dependencies(${all_target} ${target})
    endif()

    if(WEBVIEW_GUI_EXAMPLES_FORMAT_AUV3)
        if(NOT APPLE)
            message(FATAL_ERROR "WEBVIEW_GUI_EXAMPLES_FORMAT_AUV3 is supported only on Apple platforms")
        endif()
        if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
            message(FATAL_ERROR "AUv3 requires the Xcode generator (-G Xcode)")
        endif()
        if(NOT COMMAND webview_gui_apply_clap_wrapper_auv3_host_switch_patch)
            message(FATAL_ERROR
                "Pinned clap-wrapper AUv3 Xcode compatibility patch is unavailable")
        endif()
        webview_gui_apply_clap_wrapper_auv3_host_switch_patch(
            "${webview_gui_example_clap_wrapper_SOURCE_DIR}/src/detail/standalone/macos/auv3/AUv3HostAppDelegate.mm")

        set(target "${FMT_TARGET_PREFIX}_auv3")
        add_executable(${target} ${FMT_ENTRY_SOURCE})
        target_link_libraries(${target} PRIVATE ${common_libraries})
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_compile_definitions(${target} PRIVATE STATICALLY_LINKED_CLAP_ENTRY=1)
        target_add_auv3_wrapper(
            TARGET ${target}
            OUTPUT_NAME "${FMT_OUTPUT_NAME}"
            BUNDLE_IDENTIFIER "${FMT_BUNDLE_IDENTIFIER}.auv3"
            BUNDLE_VERSION "0.1.0"
            RESOURCE_DIRECTORY ""
            MANUFACTURER_NAME "webview-gui"
            MANUFACTURER_CODE "WvGu"
            SUBTYPE_CODE "${FMT_AU_SUBTYPE}"
            INSTRUMENT_TYPE "${FMT_AU_TYPE}")
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${asset_root}")
        add_dependencies(${all_target} ${target})

        set(host_target "${FMT_TARGET_PREFIX}_auv3_standalone")
        add_executable(${host_target})
        target_add_auv3_standalone_wrapper(
            TARGET ${host_target}
            OUTPUT_NAME "${FMT_OUTPUT_NAME} AUv3"
            BUNDLE_IDENTIFIER "${FMT_BUNDLE_IDENTIFIER}.auv3standalone"
            BUNDLE_VERSION "0.1.0"
            AUV3_TARGET ${target}
            AU_TYPE "${FMT_AU_TYPE}"
            AU_SUBTYPE "${FMT_AU_SUBTYPE}"
            AU_MANUFACTURER "WvGu")
        set_target_properties(${host_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${asset_root}")
        add_dependencies(${all_target} ${host_target})
    endif()

    if(WEBVIEW_GUI_EXAMPLES_FORMAT_AAX)
        if(NOT DEFINED AAX_SDK_ROOT OR "${AAX_SDK_ROOT}" STREQUAL "")
            message(FATAL_ERROR
                "WEBVIEW_GUI_EXAMPLES_FORMAT_AAX requires an explicit AAX_SDK_ROOT; public CI never enables AAX")
        endif()
        if(NOT CLAP_WRAPPER_CAN_BUILD_AAX)
            message(FATAL_ERROR "clap-wrapper cannot build AAX with this platform/toolchain")
        endif()
        set(target "${FMT_TARGET_PREFIX}_aax")
        add_library(${target} MODULE ${FMT_ENTRY_SOURCE})
        target_link_libraries(${target} PRIVATE ${common_libraries})
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_add_aax_wrapper(
            TARGET ${target}
            OUTPUT_NAME "${FMT_OUTPUT_NAME}"
            BUNDLE_IDENTIFIER "${FMT_BUNDLE_IDENTIFIER}.aaxplugin"
            BUNDLE_VERSION "0.1.0")
        add_dependencies(${all_target} ${target})
    endif()
endfunction()

if(WEBVIEW_GUI_EXAMPLES_BUILD_GAIN)
    webview_gui_add_example_wrappers(
        TARGET_PREFIX webview_gui_example_gain_formats
        CLAP_TARGET webview_gui_example_gain
        IMPL_TARGET webview_gui_example_gain_clap_core
        ENTRY_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../gain/gain_entry.cpp"
        OUTPUT_NAME WebviewGuiGain
        PLUGIN_ID com.webview-gui.example.gain
        BUNDLE_IDENTIFIER com.webview-gui.example.gain
        AU_SUBTYPE WvGn
        AU_TYPE aufx)
endif()

if(WEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH)
    webview_gui_add_example_wrappers(
        TARGET_PREFIX webview_gui_example_polysynth_formats
        CLAP_TARGET webview_gui_example_polysynth
        IMPL_TARGET webview_gui_example_polysynth_clap_core
        ENTRY_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../polysynth/polysynth_entry.cpp"
        OUTPUT_NAME WebviewGuiPolySynth
        PLUGIN_ID com.webview-gui.example.polysynth
        BUNDLE_IDENTIFIER com.webview-gui.example.polysynth
        AU_SUBTYPE WvPs
        AU_TYPE aumu
        SUPPORTS_ALL_NOTE_EXPRESSIONS
        EXTRA_LINK_LIBRARIES webview-gui)
endif()
