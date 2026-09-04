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

    # add_custom_command(TARGET ... POST_BUILD) may only be declared in the
    # directory that created the target. Most wrappers are created in the
    # examples root, but the canonical PolySynth CLAP target is created by the
    # polysynth subdirectory and packaged by its parent. For that cross-directory
    # case use an explicit staging dependency before link; LINK_DEPENDS below
    # still makes resource-only changes relink the owning target. The staging
    # commands create the final resource directory directly and CMake's later
    # link/bundle step preserves it.
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

# Make resource-only edits rebuild the target that owns the packaged copy. The
# pinned clap-wrapper accepts RESOURCE_DIRECTORY for standalone but does not copy
# it, so the existing standalone tracking call also closes that wrapper gap.
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

# Copy the clean resource root into a format-specific artifact location. The
# source tree itself is never copied, only examples/common/presets/bundled.
function(webview_gui_copy_preset_resources target destination)
    webview_gui_track_preset_resources(${target})
    webview_gui_add_preset_copy_command(${target} "${destination}")
endfunction()

function(webview_gui_package_native_clap_preset_resources target output_name)
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

# clap-wrapper 0.16.0 only copies VST3 RESOURCE_DIRECTORY on macOS and contains
# stale TCLP_RESOURCE_DIRECTORY checks on Windows/Linux. Use one repository-owned
# post-build path for all three desktop platforms.
function(webview_gui_package_vst3_preset_resources target)
    webview_gui_copy_preset_resources(
        ${target}
        "$<TARGET_FILE_DIR:${target}>/../Resources/presets")
endfunction()
