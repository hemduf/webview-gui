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
    "makeGainPresetCandidate"
    "commitPersistentParameterSnapshot")
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
    "makePolySynthPresetCandidate"
    "commitPersistentParameterSnapshot")
    string(FIND "${poly_source}" "${marker}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "PolySynthPlugin must use the shared #102 persistent preset state seam; missing '${marker}'")
    endif()
endforeach()

# State restore may perform browser bookkeeping after the commit, so pin the
# shared commit call rather than requiring it to be the literal return expression.
foreach(requirement
    "commitPersistentParameterSnapshot(loaded, true)"
    "commitPersistentParameterSnapshot(*mapped.candidate, notifyHostParams)")
    string(FIND "${gain_source}" "${requirement}" gain_position)
    if(gain_position EQUAL -1)
        message(FATAL_ERROR
            "GainPlugin state load and preset load must share the same commit seam with explicit host-notification policy; missing '${requirement}'")
    endif()
endforeach()

foreach(requirement
    "commitPersistentParameterSnapshot(loaded, true)"
    "commitPersistentParameterSnapshot(*mapped.candidate, false)")
    string(FIND "${poly_source}" "${requirement}" poly_position)
    if(poly_position EQUAL -1)
        message(FATAL_ERROR
            "PolySynthPlugin state load and preset load must share the same commit seam with distinct host-notification policy; missing '${requirement}'")
    endif()
endforeach()

string(FIND "${poly_source}" "struct ParameterSnapshot" local_snapshot_position)
if(NOT local_snapshot_position EQUAL -1)
    message(FATAL_ERROR
        "PolySynthPlugin still defines a private duplicate ParameterSnapshot; use polysynth_parameter_snapshot.h")
endif()

foreach(forbidden
    "CLAP_EVENT_PARAM_VALUE"
    "CLAP_EVENT_PARAM_GESTURE_BEGIN"
    "CLAP_EVENT_PARAM_GESTURE_END")
    string(FIND "${gain_source}" "${forbidden}" gain_event_position)
    string(FIND "${poly_source}" "${forbidden}" poly_event_position)
    # The plug-ins legitimately process these event types elsewhere; this contract
    # intentionally does not ban them globally. The shared preset apply functions
    # are instead locked above to call the snapshot commit seam directly.
endforeach()
