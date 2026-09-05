if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR)
    message(FATAL_ERROR "WEBVIEW_GUI_SOURCE_DIR is required")
endif()

set(resources_file "${WEBVIEW_GUI_SOURCE_DIR}/examples/cmake/PresetResources.cmake")
set(formats_file "${WEBVIEW_GUI_SOURCE_DIR}/examples/cmake/ExampleFormats.cmake")
file(READ "${resources_file}" resources)
file(READ "${formats_file}" formats)

foreach(required IN ITEMS
        "function(webview_gui_attach_native_preset_runtime target)"
        "preset_production_catalog.cpp"
        "native_preset_storage.cpp"
        "WebviewGuiChocDependency.cmake"
        "webview_gui_resolve_choc_dependency()"
        "WEBVIEW_GUI_CHOC_INCLUDE_DIRS"
        "WEBVIEW_GUI_NATIVE_PRESET_RUNTIME_ATTACHED"
        "if(CMAKE_SYSTEM_NAME STREQUAL \"WASI\")"
        "webview_gui_attach_native_preset_runtime(\${target})")
    string(FIND "${resources}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "#91 wrapper runtime contract missing PresetResources marker: ${required}")
    endif()
endforeach()

# The maintained CHOC fork is resolved centrally through CPM. Reintroducing the
# removed physical submodule path would make focused/wrapper projects diverge
# from the root dependency contract again.
string(FIND "${resources}" "include/webview-gui/_impl/platform/choc" stale_choc_position)
if(NOT stale_choc_position EQUAL -1)
    message(FATAL_ERROR
        "#91 wrapper runtime must not depend on the removed in-tree CHOC submodule path")
endif()

# WIN32_LEAN_AND_MEAN must remain local to the preset storage implementation.
# Propagating it to wrapper targets hides Win32 declarations required by
# clap-wrapper standalone code (notably CommandLineToArgvW).
string(FIND "${resources}" "WIN32_LEAN_AND_MEAN" lean_position)
if(NOT lean_position EQUAL -1)
    message(FATAL_ERROR
        "#91 wrapper runtime must not propagate WIN32_LEAN_AND_MEAN to wrapper targets")
endif()

# VST3, standalone, AUv2 and AUv3 all route through resource tracking/copying,
# which must now also attach the runtime used by their shared *_entry.cpp.
foreach(required IN ITEMS
        "webview_gui_package_vst3_preset_resources(\${target})"
        "webview_gui_track_preset_resources(\${target})")
    string(FIND "${formats}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "#91 wrapper runtime contract missing ExampleFormats marker: ${required}")
    endif()
endforeach()
