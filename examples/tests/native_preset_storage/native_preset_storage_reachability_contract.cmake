if(NOT DEFINED GAIN_PLUGIN_FILE)
    message(FATAL_ERROR "GAIN_PLUGIN_FILE is required")
endif()
if(NOT DEFINED POLYSYNTH_PLUGIN_FILE)
    message(FATAL_ERROR "POLYSYNTH_PLUGIN_FILE is required")
endif()

file(READ "${GAIN_PLUGIN_FILE}" gain_source)
file(READ "${POLYSYNTH_PLUGIN_FILE}" polysynth_source)

foreach(forbidden
    "native_preset_storage.h"
    "NativePresetStorage"
    "resolveCurrentNativePresetBaseRoot"
    "resolveCurrentNativePresetScopedRoot")
    string(FIND "${gain_source}" "${forbidden}" gain_position)
    if(NOT gain_position EQUAL -1)
        message(FATAL_ERROR
            "Gain plug-in/audio path must not depend on native preset filesystem storage; found '${forbidden}'")
    endif()

    string(FIND "${polysynth_source}" "${forbidden}" polysynth_position)
    if(NOT polysynth_position EQUAL -1)
        message(FATAL_ERROR
            "PolySynth plug-in/audio path must not depend on native preset filesystem storage; found '${forbidden}'")
    endif()
endforeach()

message(STATUS "Native preset filesystem storage is unreachable from Gain/PolySynth plug-in translation units")
