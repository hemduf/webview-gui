if(NOT DEFINED GAIN_PLUGIN_FILE)
    message(FATAL_ERROR "GAIN_PLUGIN_FILE is required")
endif()
if(NOT DEFINED POLYSYNTH_PLUGIN_FILE)
    message(FATAL_ERROR "POLYSYNTH_PLUGIN_FILE is required")
endif()

file(READ "${GAIN_PLUGIN_FILE}" gain_source)
file(READ "${POLYSYNTH_PLUGIN_FILE}" poly_source)

foreach(marker
    "gain_persistent_state.h"
    "gain_preset_state.h"
    "GainParameterSnapshot"
    "captureGainPreset"
    "makeGainPresetCandidate")
    string(FIND "${gain_source}" "${marker}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "GainPlugin must use the shared #102 persistent preset state seam; missing '${marker}'")
    endif()
endforeach()

foreach(marker
    "polysynth_parameter_snapshot.h"
    "polysynth_preset_state.h"
    "capturePolySynthPreset"
    "makePolySynthPresetCandidate")
    string(FIND "${poly_source}" "${marker}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "PolySynthPlugin must use the shared #102 persistent preset state seam; missing '${marker}'")
    endif()
endforeach()

string(FIND "${poly_source}" "struct ParameterSnapshot" local_snapshot_position)
if(NOT local_snapshot_position EQUAL -1)
    message(FATAL_ERROR
        "PolySynthPlugin still defines a private duplicate ParameterSnapshot; use polysynth_parameter_snapshot.h")
endif()
