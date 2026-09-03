if(NOT DEFINED WEBVIEW_GUI_ROOT OR "${WEBVIEW_GUI_ROOT}" STREQUAL "")
    message(FATAL_ERROR "WEBVIEW_GUI_ROOT is required")
endif()

set(rt_files
    "examples/gain/gain_plugin.cpp"
    "examples/gain/gain_processor.h"
    "examples/polysynth/polysynth_plugin.cpp"
    "examples/polysynth/polysynth_parameter_voice_engine.h"
    "examples/gain/wclap/gain_wclap_entry.cpp"
    "examples/polysynth/wclap/polysynth_wclap_entry.cpp"
    "examples/polysynth/wclap/polysynth_wclap_proxy.h")

foreach(relative_path IN LISTS rt_files)
    set(path "${WEBVIEW_GUI_ROOT}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missing RT contract input: ${relative_path}")
    endif()
    file(READ "${path}" content)
    foreach(forbidden IN ITEMS
            "native_preset_storage"
            "preset_storage.h"
            "std::filesystem"
            "<filesystem>")
        string(FIND "${content}" "${forbidden}" forbidden_index)
        if(NOT forbidden_index EQUAL -1)
            message(FATAL_ERROR
                "#104 RT contract: ${relative_path} reaches forbidden preset/filesystem token '${forbidden}'")
        endif()
    endforeach()
endforeach()

message(STATUS "#104 RT/source reachability contract satisfied")
