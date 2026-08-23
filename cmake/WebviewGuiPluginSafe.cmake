include_guard(GLOBAL)

# Apply the repository's plug-in-safe linkage/visibility contract to a loadable
# CLAP/VST3/AU module. The implementation remains a private static dependency;
# only entry points explicitly marked for export by the plug-in wrapper should
# cross the module boundary.
function(webview_gui_configure_plugin_target target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "webview_gui_configure_plugin_target: unknown target '${target}'")
    endif()

    if(NOT TARGET webview-gui)
        message(FATAL_ERROR
            "webview_gui_configure_plugin_target requires add_subdirectory(webview-gui) first")
    endif()

    get_target_property(_webview_gui_type webview-gui TYPE)
    if(NOT _webview_gui_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR
            "plug-in-safe integration requires the private STATIC webview-gui target; got ${_webview_gui_type}")
    endif()

    target_link_libraries("${target}" PRIVATE webview-gui)

    # CMake maps these properties to -fvisibility=hidden and
    # -fvisibility-inlines-hidden on GCC/Clang-family compilers. On Windows,
    # exports remain opt-in through __declspec(dllexport), which is exactly the
    # desired plug-in ABI model.
    set_target_properties("${target}" PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
endfunction()
