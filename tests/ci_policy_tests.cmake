function(require_workflow_contract workflow_file expected_name)
    set(workflow_path "${CMAKE_CURRENT_LIST_DIR}/../.github/workflows/${workflow_file}")
    if(NOT EXISTS "${workflow_path}")
        message(FATAL_ERROR "Required workflow is missing: ${workflow_file}")
    endif()

    file(READ "${workflow_path}" workflow_content)

    string(FIND "${workflow_content}" "name: ${expected_name}" name_offset)
    if(name_offset EQUAL -1)
        message(FATAL_ERROR
            "Required workflow '${workflow_file}' changed its stable check name; expected '${expected_name}'")
    endif()

    set(push_main_contract "  push:\n    branches:\n      - main")
    string(FIND "${workflow_content}" "${push_main_contract}" push_main_offset)
    if(push_main_offset EQUAL -1)
        message(FATAL_ERROR
            "Required workflow '${workflow_file}' must run on push to main so merged commits are requalified")
    endif()
endfunction()

require_workflow_contract("ci.yml" "CI")
require_workflow_contract("plugin-safe-profile.yml" "Plugin-safe CMake profile")
require_workflow_contract("macos-diagnostics.yml" "macOS plugin diagnostics")
require_workflow_contract("header-only-contract.yml" "Header-only contract")
