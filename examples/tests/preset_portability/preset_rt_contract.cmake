if(NOT DEFINED WEBVIEW_GUI_ROOT OR "${WEBVIEW_GUI_ROOT}" STREQUAL "")
    message(FATAL_ERROR "WEBVIEW_GUI_ROOT is required")
endif()

# Actual DSP/process implementation headers must remain completely unaware of
# preset parsing/storage. The plug-in translation units may later use the
# filesystem-free portable interface on non-RT callbacks, but native filesystem
# storage must never enter those translation units.
set(audio_process_files
    "examples/gain/gain_processor.h"
    "examples/polysynth/polysynth_parameter_voice_engine.h")

foreach(relative_path IN LISTS audio_process_files)
    set(path "${WEBVIEW_GUI_ROOT}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missing RT contract input: ${relative_path}")
    endif()
    file(READ "${path}" content)
    foreach(forbidden IN ITEMS
            "preset_storage.h"
            "native_preset_storage"
            "native_preset_user_storage"
            "parsePreset"
            "serializePreset"
            "std::filesystem"
            "<filesystem>")
        string(FIND "${content}" "${forbidden}" forbidden_index)
        if(NOT forbidden_index EQUAL -1)
            message(FATAL_ERROR
                "#104 RT contract: ${relative_path} reaches forbidden preset/filesystem token '${forbidden}'")
        endif()
    endforeach()
endforeach()

set(plugin_boundary_files
    "examples/gain/gain_plugin.cpp"
    "examples/polysynth/polysynth_plugin.cpp"
    "examples/gain/wclap/gain_wclap_entry.cpp"
    "examples/polysynth/wclap/polysynth_wclap_entry.cpp"
    "examples/polysynth/wclap/polysynth_wclap_proxy.h")
foreach(relative_path IN LISTS plugin_boundary_files)
    set(path "${WEBVIEW_GUI_ROOT}/${relative_path}")
    file(READ "${path}" content)
    foreach(forbidden IN ITEMS
            "native_preset_storage"
            "native_preset_user_storage"
            "std::filesystem"
            "<filesystem>")
        string(FIND "${content}" "${forbidden}" forbidden_index)
        if(NOT forbidden_index EQUAL -1)
            message(FATAL_ERROR
                "#104 RT boundary: ${relative_path} imports native filesystem storage '${forbidden}'")
        endif()
    endforeach()
endforeach()

message(STATUS "#104 audio-process preset isolation contract satisfied")
