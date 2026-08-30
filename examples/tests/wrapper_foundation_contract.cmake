if(NOT DEFINED WRAPPER_REVISION OR WRAPPER_REVISION STREQUAL "")
    message(FATAL_ERROR "WRAPPER_REVISION is required")
endif()

string(LENGTH "${WRAPPER_REVISION}" wrapper_revision_length)
if(NOT wrapper_revision_length EQUAL 40 OR NOT WRAPPER_REVISION MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR
        "clap-wrapper must be pinned to a full lowercase 40-character commit SHA")
endif()

if(NOT DEFINED WRAPPER_SOURCE_DIR OR NOT IS_DIRECTORY "${WRAPPER_SOURCE_DIR}")
    message(FATAL_ERROR "Fetched clap-wrapper source directory is missing")
endif()

foreach(required_file IN ITEMS
        CMakeLists.txt
        cmake/wrapper_functions.cmake
        cmake/wrap_vst3.cmake
        cmake/wrap_auv2.cmake
        cmake/wrap_auv3.cmake
        cmake/wrap_standalone.cmake)
    if(NOT EXISTS "${WRAPPER_SOURCE_DIR}/${required_file}")
        message(FATAL_ERROR
            "Pinned clap-wrapper source is missing required file: ${required_file}")
    endif()
endforeach()

if(NOT DEFINED CLAP_SOURCE_DIR
   OR NOT EXISTS "${CLAP_SOURCE_DIR}/include/clap/clap.h")
    message(FATAL_ERROR "The wrapper foundation is not sharing the pinned example CLAP SDK")
endif()

execute_process(
    COMMAND git -C "${WRAPPER_SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE git_result
    OUTPUT_VARIABLE actual_wrapper_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE git_error)

if(NOT git_result EQUAL 0)
    message(FATAL_ERROR
        "Unable to verify fetched clap-wrapper revision: ${git_error}")
endif()

if(NOT actual_wrapper_revision STREQUAL WRAPPER_REVISION)
    message(FATAL_ERROR
        "Fetched clap-wrapper revision ${actual_wrapper_revision} does not match pin ${WRAPPER_REVISION}")
endif()

message(STATUS
    "Pinned clap-wrapper foundation verified at ${actual_wrapper_revision}")
