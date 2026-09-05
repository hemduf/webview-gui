cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED VALIDATOR_ROOT OR NOT IS_DIRECTORY "${VALIDATOR_ROOT}")
    message(FATAL_ERROR "VALIDATOR_ROOT must point to the pinned clap-validator checkout")
endif()

set(expected_revision "152b9823e992d782c5c1fd33bca0295478b919aa")
execute_process(
    COMMAND git -C "${VALIDATOR_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE actual_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE git_result
)
if(NOT git_result EQUAL 0 OR NOT actual_revision STREQUAL expected_revision)
    message(FATAL_ERROR
        "Refusing to patch clap-validator: expected ${expected_revision}, got '${actual_revision}'")
endif()

function(replace_once_or_verify file_path old_text new_text label)
    if(NOT EXISTS "${file_path}")
        message(FATAL_ERROR "Missing clap-validator source for ${label}: ${file_path}")
    endif()

    file(READ "${file_path}" source)
    string(FIND "${source}" "${old_text}" old_position)
    if(NOT old_position EQUAL -1)
        string(REPLACE "${old_text}" "${new_text}" patched "${source}")
        if(patched STREQUAL source)
            message(FATAL_ERROR "Failed to patch ${label}")
        endif()
        file(WRITE "${file_path}" "${patched}")
        return()
    endif()

    string(FIND "${source}" "${new_text}" new_position)
    if(new_position EQUAL -1)
        message(FATAL_ERROR
            "Pinned clap-validator source drifted for ${label}; expected neither original nor patched anchor")
    endif()
endfunction()

set(indexer_file "${VALIDATOR_ROOT}/src/plugin/preset_discovery/indexer.rs")
set(indexer_old [=[                if !path_str.starts_with('/') {
                    anyhow::bail!("'{path_str}' should be an absolute path, i.e. '/{path_str}'.");
                }
]=])
set(indexer_new [=[                if !PathBuf::from(&path_str).is_absolute() {
                    anyhow::bail!("'{path_str}' should be an absolute path for the current operating system.");
                }
]=])
replace_once_or_verify("${indexer_file}" "${indexer_old}" "${indexer_new}"
    "native FILE absolute-path validation")

set(metadata_file "${VALIDATOR_ROOT}/src/plugin/preset_discovery/metadata_receiver.rs")
set(metadata_old [=[                    _ => {}
                }

                // Container presets have a load key, single-preset files don't have a load key. The
]=])
set(metadata_new [=[                    _ => {}
                }
                // Do not retain the result mutex while flush_preset() reacquires it for a
                // subsequent preset in a multi-preset PLUGIN container.
                drop(result);

                // Container presets have a load key, single-preset files don't have a load key. The
]=])
replace_once_or_verify("${metadata_file}" "${metadata_old}" "${metadata_new}"
    "multi-preset metadata receiver mutex lifetime")

execute_process(
    COMMAND git -C "${VALIDATOR_ROOT}" diff --check
    RESULT_VARIABLE diff_check_result
)
if(NOT diff_check_result EQUAL 0)
    message(FATAL_ERROR "Patched clap-validator source failed git diff --check")
endif()

message(STATUS "Applied deterministic Preset Discovery fixes to pinned clap-validator ${expected_revision}")
