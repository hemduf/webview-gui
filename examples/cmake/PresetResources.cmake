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

# Make resource-only edits rebuild the target that owns the packaged copy.
function(webview_gui_track_preset_resources target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "preset resources: missing target ${target}")
    endif()
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        ${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_FILES})
endfunction()

# Copy the clean resource root into a format-specific artifact location. The
# source tree itself is never copied, only examples/common/presets/bundled.
function(webview_gui_copy_preset_resources target destination)
    webview_gui_track_preset_resources(${target})
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${destination}/factory"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WEBVIEW_GUI_EXAMPLES_PRESET_RESOURCE_DIRECTORY}"
            "${destination}"
        VERBATIM)
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

# clap-wrapper 0.16.0 accepts RESOURCE_DIRECTORY for standalone but only warns
# that it is unsupported. Copy the canonical bank ourselves so standalone builds
# are not silently missing presets.
function(webview_gui_package_standalone_preset_resources target output_name)
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
