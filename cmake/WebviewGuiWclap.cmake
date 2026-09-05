include_guard(GLOBAL)

# Configure an executable CLAP entry target as a WCLAP WebAssembly reactor and
# package it as <OutputName>.wclap/module.wasm plus a .tar.gz distribution.
# The browser/editor is host-owned through clap.webview/3, so this helper forces
# webview-gui's WebView-only backend and never links native CHOC/OS GUI support.
# COMPILE_TARGETS lists additional plug-in/core targets which compile inline
# ClapWebviewGui code and therefore need the same target-local WCLAP profile.
# WEB_RESOURCE_TARGET optionally names a target produced by
# webview_gui_add_web_resources(); its exact Vite dist tree is copied into the
# .wclap bundle after it has been generated.
function(webview_gui_configure_wclap_target target)
    set(oneValueArgs OUTPUT_NAME RESOURCE_DIRECTORY WEB_RESOURCE_TARGET)
    set(multiValueArgs COMPILE_TARGETS)
    cmake_parse_arguments(WCLAP "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TARGET ${target})
        message(FATAL_ERROR "webview_gui_configure_wclap_target: '${target}' is not a target")
    endif()

    get_target_property(_target_type ${target} TYPE)
    if(NOT _target_type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR
            "webview_gui_configure_wclap_target: WCLAP modules are WebAssembly executable/reactors; '${target}' is ${_target_type}")
    endif()

    set(_webview_gui_is_wasm FALSE)
    if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
        set(_webview_gui_is_wasm TRUE)
    elseif(CMAKE_CXX_COMPILER_TARGET MATCHES "^wasm(32|64)(-|$)")
        set(_webview_gui_is_wasm TRUE)
    endif()
    if(NOT _webview_gui_is_wasm)
        message(FATAL_ERROR
            "webview_gui_configure_wclap_target must be used with an Emscripten or WASI/WebAssembly toolchain")
    endif()

    if(NOT WCLAP_OUTPUT_NAME)
        set(WCLAP_OUTPUT_NAME "${target}")
    endif()

    set(_bundle_name "${WCLAP_OUTPUT_NAME}.wclap")
    set(_bundle_dir "${CMAKE_CURRENT_BINARY_DIR}/${_bundle_name}")
    set(_archive "${CMAKE_CURRENT_BINARY_DIR}/${_bundle_name}.tar.gz")
    set(_module_dir "${CMAKE_CURRENT_BINARY_DIR}/wclap-module/${target}")

    set(_wclap_compile_targets ${target} ${WCLAP_COMPILE_TARGETS})
    list(REMOVE_DUPLICATES _wclap_compile_targets)
    foreach(_wclap_compile_target IN LISTS _wclap_compile_targets)
        if(NOT TARGET ${_wclap_compile_target})
            message(FATAL_ERROR
                "webview_gui_configure_wclap_target: COMPILE_TARGETS entry '${_wclap_compile_target}' is not a target")
        endif()
        target_compile_definitions(${_wclap_compile_target} PRIVATE WEBVIEW_GUI_WEBVIEW_ONLY=1)
    endforeach()

    if(TARGET webview-gui)
        # Keep the implementation object itself on the no-native-GUI backend, but
        # do not publish this definition to unrelated consumers in the same CMake
        # configure. Header-defined consumers must opt in through COMPILE_TARGETS.
        target_compile_definitions(webview-gui PRIVATE WEBVIEW_GUI_WEBVIEW_ONLY=1)
    endif()

    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE -msimd128 --no-entry)
        target_link_options(${target} PRIVATE
            -msimd128
            -sSTANDALONE_WASM
            --no-entry
            -sEXPORTED_FUNCTIONS=_clap_entry,_malloc
            -sINITIAL_MEMORY=512kb
            -sALLOW_MEMORY_GROWTH=1
            -sALLOW_TABLE_GROWTH=1
            -sPURE_WASI
            --export-table
        )
    else()
        target_compile_options(${target} PRIVATE -msimd128 -fno-exceptions)
        target_link_options(${target} PRIVATE
            -mexec-model=reactor
            -Wl,--max-memory=2147483648,--export-table,--growable-table,--export=malloc,--export=clap_entry
        )
    endif()

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "module"
        PREFIX ""
        SUFFIX ".wasm"
        RUNTIME_OUTPUT_DIRECTORY "${_module_dir}"
    )

    if(CMAKE_CONFIGURATION_TYPES)
        foreach(_config IN LISTS CMAKE_CONFIGURATION_TYPES)
            string(TOUPPER "${_config}" _config_upper)
            set_target_properties(${target} PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${_config_upper}" "${_module_dir}"
            )
        endforeach()
    endif()

    set(_resource_files)
    if(WCLAP_RESOURCE_DIRECTORY)
        if(NOT IS_DIRECTORY "${WCLAP_RESOURCE_DIRECTORY}")
            message(FATAL_ERROR
                "webview_gui_configure_wclap_target: RESOURCE_DIRECTORY does not exist: ${WCLAP_RESOURCE_DIRECTORY}")
        endif()
        file(GLOB_RECURSE _resource_files
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES FALSE
            "${WCLAP_RESOURCE_DIRECTORY}/*"
        )
        string(REPLACE ";" "\n" _resource_manifest_content "${_resource_files}")
        set(_resource_manifest "${CMAKE_CURRENT_BINARY_DIR}/${target}-wclap-resources.txt")
        file(CONFIGURE
            OUTPUT "${_resource_manifest}"
            CONTENT "${_resource_manifest_content}\n"
            @ONLY
        )
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${_resource_manifest}"
            ${_resource_files}
        )
    endif()

    set(_web_resource_dist "")
    if(WCLAP_WEB_RESOURCE_TARGET)
        if(NOT TARGET ${WCLAP_WEB_RESOURCE_TARGET})
            message(FATAL_ERROR
                "webview_gui_configure_wclap_target: WEB_RESOURCE_TARGET '${WCLAP_WEB_RESOURCE_TARGET}' is not a target")
        endif()
        get_target_property(_web_resource_dist ${WCLAP_WEB_RESOURCE_TARGET} WEBVIEW_GUI_WEB_DIST_DIR)
        get_target_property(_web_resource_stamp ${WCLAP_WEB_RESOURCE_TARGET} WEBVIEW_GUI_WEB_DIST_STAMP)
        get_target_property(_web_resource_build_target ${WCLAP_WEB_RESOURCE_TARGET} WEBVIEW_GUI_WEB_DIST_TARGET)
        if(NOT _web_resource_dist OR NOT _web_resource_stamp OR NOT _web_resource_build_target)
            message(FATAL_ERROR
                "webview_gui_configure_wclap_target: WEB_RESOURCE_TARGET '${WCLAP_WEB_RESOURCE_TARGET}' was not created by webview_gui_add_web_resources")
        endif()
        add_dependencies(${target} ${_web_resource_build_target})
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_web_resource_stamp}")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_bundle_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_bundle_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target}>" "${_bundle_dir}/module.wasm"
        VERBATIM
    )

    if(WCLAP_WEB_RESOURCE_TARGET)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_web_resource_dist}"
                "${_bundle_dir}"
            VERBATIM
        )
    endif()

    if(WCLAP_RESOURCE_DIRECTORY)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${WCLAP_RESOURCE_DIRECTORY}"
                "${_bundle_dir}"
            VERBATIM
        )
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f "${_archive}"
        COMMAND ${CMAKE_COMMAND} -E tar cz "${_archive}" "${_bundle_name}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        VERBATIM
    )

    set_property(TARGET ${target} PROPERTY WEBVIEW_GUI_WCLAP_BUNDLE "${_bundle_dir}")
    set_property(TARGET ${target} PROPERTY WEBVIEW_GUI_WCLAP_ARCHIVE "${_archive}")
endfunction()
