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
        "include/webview-gui/_impl/platform/choc"
        "WEBVIEW_GUI_NATIVE_PRESET_RUNTIME_ATTACHED"
        "if(CMAKE_SYSTEM_NAME STREQUAL \"WASI\")"
        "webview_gui_attach_native_preset_runtime(\${target})")
    string(FIND "${resources}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "#91 wrapper runtime contract missing PresetResources marker: ${required}")
    endif()
endforeach()

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
