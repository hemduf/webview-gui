include_guard(GLOBAL)

# Configure an executable CLAP entry target as a WCLAP WebAssembly reactor and
# package it as <OutputName>.wclap/module.wasm plus a .tar.gz distribution.
# The browser/editor is host-owned through clap.webview/3, so this helper forces
# webview-gui's WebView-only backend and never links native CHOC/OS GUI support.
function(webview_gui_configure_wclap_target target)
    set(oneValueArgs OUTPUT_NAME RESOURCE_DIRECTORY)
    cmake_parse_arguments(WCLAP "" "${oneValueArgs}" "" ${ARGN})

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

    target_compile_definitions(${target} PRIVATE WEBVIEW_GUI_WEBVIEW_ONLY=1)
    if(TARGET webview-gui)
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

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_bundle_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_bundle_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target}>" "${_bundle_dir}/module.wasm"
        VERBATIM
    )

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
