function(require_workflow_contract workflow_file expected_name)
    set(workflow_path "${CMAKE_CURRENT_LIST_DIR}/../.github/workflows/${workflow_file}")
    if(NOT EXISTS "${workflow_path}")
        message(FATAL_ERROR "Required workflow is missing: ${workflow_file}")
    endif()

    file(READ "${workflow_path}" workflow_content)

    string(FIND "${workflow_content}" "name: ${expected_name}" name_offset)
    if(name_offset EQUAL -1)
        message(FATAL_ERROR
            "Required workflow '${workflow_file}' changed its stable workflow name; expected '${expected_name}'")
    endif()

    set(push_main_contract "  push:\n    branches:\n      - main")
    string(FIND "${workflow_content}" "${push_main_contract}" push_main_offset)
    if(push_main_offset EQUAL -1)
        message(FATAL_ERROR
            "Required workflow '${workflow_file}' must run on push to main so merged commits are requalified")
    endif()
endfunction()

function(require_workflow_check_name workflow_file expected_job_name)
    set(workflow_path "${CMAKE_CURRENT_LIST_DIR}/../.github/workflows/${workflow_file}")
    file(READ "${workflow_path}" workflow_content)
    string(FIND "${workflow_content}" "name: ${expected_job_name}" job_name_offset)
    if(job_name_offset EQUAL -1)
        message(FATAL_ERROR
            "Required workflow '${workflow_file}' changed a branch-protection check context; expected job name '${expected_job_name}'")
    endif()
endfunction()

require_workflow_contract("ci.yml" "CI")
require_workflow_check_name("ci.yml" "\${{ matrix.os }} / Debug")
require_workflow_check_name("ci.yml" "\${{ matrix.os }} / ASan+UBSan")
require_workflow_check_name("ci.yml" "ubuntu-latest / TSan registry")

require_workflow_contract("plugin-safe-profile.yml" "Plugin-safe CMake profile")
require_workflow_check_name("plugin-safe-profile.yml" "\${{ matrix.os }} / private module exports")

require_workflow_contract("macos-diagnostics.yml" "macOS plugin diagnostics")
require_workflow_check_name("macos-diagnostics.yml" "macOS Zombies lifecycle")

require_workflow_contract("header-only-contract.yml" "Header-only contract")
require_workflow_check_name("header-only-contract.yml" "\${{ matrix.os }} standalone header-only diagnostic")

require_workflow_contract("repository-policy.yml" "Repository policy")
require_workflow_check_name("repository-policy.yml" "required main qualification contract")
