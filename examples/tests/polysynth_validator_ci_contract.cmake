if(NOT DEFINED WORKFLOW_FILE OR NOT EXISTS "${WORKFLOW_FILE}")
    message(FATAL_ERROR "WORKFLOW_FILE must point to the PolySynth workflow")
endif()
if(NOT DEFINED EXAMPLES_CMAKE_FILE OR NOT EXISTS "${EXAMPLES_CMAKE_FILE}")
    message(FATAL_ERROR "EXAMPLES_CMAKE_FILE must point to examples/CMakeLists.txt")
endif()

file(READ "${WORKFLOW_FILE}" workflow)
file(READ "${EXAMPLES_CMAKE_FILE}" examples_cmake)

function(require_text haystack needle description)
    string(FIND "${${haystack}}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${description}: missing '${needle}'")
    endif()
endfunction()

set(expected_revision "152b9823e992d782c5c1fd33bca0295478b919aa")

require_text(examples_cmake
    "WEBVIEW_GUI_EXAMPLES_CLAP_VALIDATOR_REVISION"
    "PolySynth validation must expose an explicit clap-validator revision")
require_text(examples_cmake
    "${expected_revision}"
    "PolySynth validation must use the reviewed clap-validator commit")

require_text(workflow
    "polysynth-validator:"
    "PolySynth workflow must have a dedicated blocking validator job")
require_text(workflow
    "[macos-latest, ubuntu-latest, windows-latest]"
    "PolySynth validator job must qualify macOS, Linux, and Windows")
require_text(workflow
    "free-audio/clap-validator"
    "PolySynth validator job must checkout the upstream validator repository")
require_text(workflow
    "${expected_revision}"
    "PolySynth validator checkout must pin the reviewed commit")
require_text(workflow
    "rustup toolchain install 1.95.0"
    "PolySynth validator build must pin the validator MSRV toolchain")
require_text(workflow
    "cargo +1.95.0 build --release --locked"
    "PolySynth validator build must honor Cargo.lock")
require_text(workflow
    "Build exact PolySynth CLAP artifact"
    "PolySynth validator job must build the PR artifact")
require_text(workflow
    "clap-validator --version"
    "PolySynth validator job must print the validator version")
require_text(workflow
    "PolySynth CLAP path:"
    "PolySynth validator job must print the exact plug-in path")
require_text(workflow
    "validate"
    "PolySynth validator job must execute validation")
require_text(workflow
    "--only-failed"
    "PolySynth validator job must preserve validator failure diagnostics")

string(FIND "${workflow}" "continue-on-error" continue_on_error_position)
if(NOT continue_on_error_position EQUAL -1)
    message(FATAL_ERROR "PolySynth validator workflow must be blocking; continue-on-error is forbidden")
endif()
