if(NOT DEFINED GAIN_PLUGIN_FILE)
    message(FATAL_ERROR "GAIN_PLUGIN_FILE is required")
endif()
if(NOT DEFINED POLYSYNTH_PLUGIN_FILE)
    message(FATAL_ERROR "POLYSYNTH_PLUGIN_FILE is required")
endif()

function(assert_process_has_no_storage file label)
    file(READ "${file}" source)
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
        "NativePresetStorage"
        "native_preset_storage"
        "std::filesystem"
        "resolveCurrentNativePreset"
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

assert_process_has_no_storage("${GAIN_PLUGIN_FILE}" "GainPlugin")
assert_process_has_no_storage("${POLYSYNTH_PLUGIN_FILE}" "PolySynthPlugin")
