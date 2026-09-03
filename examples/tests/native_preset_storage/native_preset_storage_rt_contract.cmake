if(NOT DEFINED GAIN_PLUGIN_FILE)
    message(FATAL_ERROR "GAIN_PLUGIN_FILE is required")
endif()
if(NOT DEFINED POLYSYNTH_PLUGIN_FILE)
    message(FATAL_ERROR "POLYSYNTH_PLUGIN_FILE is required")
endif()

function(assert_plugin_tu_has_no_native_storage file label)
    file(READ "${file}" source)

    # #103 provides an explicitly blocking native filesystem backend. Until the
    # later integration stage, neither plug-in translation unit may acquire any
    # dependency on it. Scanning the whole TU is intentionally stronger and less
    # brittle than trying to delimit process() with plug-in-specific markers: a
    # helper reachable from process() cannot hide a native-storage dependency.
    foreach(forbidden
        "native_preset_storage.h"
        "NativePresetStorage"
        "resolveCurrentNativePresetBaseRoot"
        "resolveCurrentNativePresetScopedRoot"
        "std::filesystem"
        "<filesystem>")
        string(FIND "${source}" "${forbidden}" position)
        if(NOT position EQUAL -1)
            message(FATAL_ERROR
                "${label}: native preset filesystem storage must remain outside the plug-in/audio TU in #103; found '${forbidden}'")
        endif()
    endforeach()

    string(FIND "${source}" "clap_process_status process(" process_start)
    if(process_start EQUAL -1)
        message(FATAL_ERROR "${label}: process() marker not found")
    endif()
endfunction()

assert_plugin_tu_has_no_native_storage("${GAIN_PLUGIN_FILE}" "GainPlugin")
assert_plugin_tu_has_no_native_storage("${POLYSYNTH_PLUGIN_FILE}" "PolySynthPlugin")

message(STATUS "Native preset filesystem storage is isolated from Gain/PolySynth audio translation units")
