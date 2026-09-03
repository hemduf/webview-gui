if(NOT DEFINED GAIN_PLUGIN_FILE)
    message(FATAL_ERROR "GAIN_PLUGIN_FILE is required")
endif()
if(NOT DEFINED POLYSYNTH_PLUGIN_FILE)
    message(FATAL_ERROR "POLYSYNTH_PLUGIN_FILE is required")
endif()

function(assert_plugin_tu_has_no_native_storage file label)
    file(READ "${file}" source)

    # #103 only provides the native persistence backend. Until the later CLAP/UI
    # integration stage, neither plug-in TU may acquire any dependency on it.
    # This is stronger than a direct process() scan and prevents a helper called
    # by process() from smuggling blocking filesystem work into the audio path.
    foreach(forbidden
        "native_preset_storage.h"
        "NativePresetStorage"
        "resolveCurrentNativePresetBaseRoot"
        "resolveCurrentNativePresetScopedRoot")
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

    string(SUBSTRING "${source}" ${process_start} -1 tail)
    string(FIND "${tail}" "bool enableDraftExtensions()" process_end)
    if(process_end EQUAL -1)
        message(FATAL_ERROR "${label}: end marker after process() not found")
    endif()
    string(SUBSTRING "${tail}" 0 ${process_end} process_source)

    foreach(forbidden
        "std::filesystem"
        "saveNew("
        "saveAs("
        "replace("
        "remove(")
        string(FIND "${process_source}" "${forbidden}" position)
        if(NOT position EQUAL -1)
            message(FATAL_ERROR
                "${label}: filesystem-backed preset storage is reachable from process(); found '${forbidden}'")
        endif()
    endforeach()
endfunction()

assert_plugin_tu_has_no_native_storage("${GAIN_PLUGIN_FILE}" "GainPlugin")
assert_plugin_tu_has_no_native_storage("${POLYSYNTH_PLUGIN_FILE}" "PolySynthPlugin")

message(STATUS "Native preset filesystem storage is isolated from Gain/PolySynth audio translation units")
