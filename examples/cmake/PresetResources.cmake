include_guard(DIRECTORY)

if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR OR "${WEBVIEW_GUI_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "PresetResources.cmake requires WEBVIEW_GUI_SOURCE_DIR")
endif()

set(WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_DIRECTORY
    "${WEBVIEW_GUI_SOURCE_DIR}/examples/common/presets/bundled")

if(NOT IS_DIRECTORY "${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_DIRECTORY}/factory")
    message(FATAL_ERROR
        "Canonical bundled preset bank is missing: ${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_DIRECTORY}/factory")
endif()

file(GLOB_RECURSE WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_DIRECTORY}/*")

function(webview_gui_add_preset_copy_command target destination)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "preset resources: missing target ${target}")
    endif()

    get_target_property(target_source_dir ${target} SOURCE_DIR)
    if(target_source_dir STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${destination}/factory"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_DIRECTORY}"
                "${destination}"
            VERBATIM)
        return()
    endif()

    set(stage_target "${target}_preset_resources_stage")
    if(TARGET ${stage_target})
        message(FATAL_ERROR
            "preset resources: duplicate cross-directory staging target ${stage_target}")
    endif()

    add_custom_target(${stage_target}
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${destination}/factory"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_DIRECTORY}"
            "${destination}"
        DEPENDS ${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_FILES}
        VERBATIM)
    add_dependencies(${target} ${stage_target})
endfunction()

function(webview_gui_track_preset_resources target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "preset resources: missing target ${target}")
    endif()
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        ${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_FILES})

    if("${target}" MATCHES "_standalone$")
        get_target_property(standalone_packaged ${target} WEBVIEW_GUI_PRESET_RESOURCES_PACKAGED)
        if(NOT standalone_packaged)
            set_property(TARGET ${target} PROPERTY WEBVIEW_GUI_PRESET_RESOURCES_PACKAGED TRUE)
            get_target_property(output_name ${target} OUTPUT_NAME)
            if(NOT output_name OR output_name STREQUAL "output_name-NOTFOUND")
                set(output_name "${target}")
            endif()
            if(APPLE)
                set(destination "$<TARGET_FILE_DIR:${target}>/../Resources/presets")
            else()
                set(destination "$<TARGET_FILE_DIR:${target}>/${output_name}.resources/presets")
            endif()
            webview_gui_add_preset_copy_command(${target} "${destination}")
        endif()
    endif()
endfunction()

function(webview_gui_copy_preset_resources target destination)
    webview_gui_track_preset_resources(${target})
    webview_gui_add_preset_copy_command(${target} "${destination}")
endfunction()

# Native CLAP discovery needs the same #36 storage implementation used by the
# preset subsystem tests. Attach it once to each canonical native module here,
# where both Gain and PolySynth already pass for resource packaging.
function(webview_gui_attach_native_preset_runtime target)
    get_target_property(runtime_attached ${target} WEBVIEW_GUI_NATIVE_PRESET_RUNTIME_ATTACHED)
    if(runtime_attached)
        return()
    endif()
    set_property(TARGET ${target} PROPERTY WEBVIEW_GUI_NATIVE_PRESET_RUNTIME_ATTACHED TRUE)

    target_sources(${target} PRIVATE
        "${WEBVIEW_GUI_SOURCE_DIR}/examples/common/preset_production_catalog.cpp"
        "${WEBVIEW_GUI_SOURCE_DIR}/examples/common/presets/native_preset_storage.cpp")
    target_include_directories(${target} PRIVATE
        "${WEBVIEW_GUI_SOURCE_DIR}/examples/common"
        "${WEBVIEW_GUI_SOURCE_DIR}/examples/common/presets"
        "${WEBVIEW_GUI_SOURCE_DIR}/include/webview-gui/_impl/platform/choc")
    if(WIN32)
        target_link_libraries(${target} PRIVATE shell32 ole32)
    endif()
endfunction()

function(webview_gui_package_native_clap_preset_resources target output_name)
    webview_gui_attach_native_preset_runtime(${target})

    get_target_property(already_packaged ${target} WEBVIEW_GUI_PRESET_RESOURCES_PACKAGED)
    if(already_packaged)
        return()
    endif()
    set_property(TARGET ${target} PROPERTY WEBVIEW_GUI_PRESET_RESOURCES_PACKAGED TRUE)

    if(APPLE)
        set(destination "$<TARGET_FILE_DIR:${target}>/../Resources/presets")
    else()
        set(destination "$<TARGET_FILE_DIR:${target}>/${output_name}.resources/presets")
    endif()
    webview_gui_copy_preset_resources(${target} "${destination}")
endfunction()

function(webview_gui_package_vst3_preset_resources target)
    webview_gui_copy_preset_resources(
        ${target}
        "$<TARGET_FILE_DIR:${target}>/../Resources/presets")
endfunction()
