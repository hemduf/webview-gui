if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR OR WEBVIEW_GUI_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "WEBVIEW_GUI_SOURCE_DIR is required")
endif()

set(readme_path "${WEBVIEW_GUI_SOURCE_DIR}/examples/README.md")
set(preset_ci_path "${WEBVIEW_GUI_SOURCE_DIR}/.github/workflows/preset-contract.yml")
set(native_ci_contract_path "${WEBVIEW_GUI_SOURCE_DIR}/examples/tests/preset_contract/preset_native_ci_contract.cmake")

foreach(required_file IN ITEMS "${readme_path}" "${preset_ci_path}" "${native_ci_contract_path}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "#96 final qualification contract missing required file: ${required_file}")
    endif()
endforeach()

file(READ "${readme_path}" readme)
file(READ "${preset_ci_path}" preset_ci)
file(READ "${native_ci_contract_path}" native_ci_contract)

set(violations "")

macro(expect_contains haystack_var needle label)
    string(FIND "${${haystack_var}}" "${needle}" position)
    if(position EQUAL -1)
        list(APPEND violations "${label}: missing '${needle}'")
    endif()
endmacro()

macro(expect_absent haystack_var needle label)
    string(FIND "${${haystack_var}}" "${needle}" position)
    if(NOT position EQUAL -1)
        list(APPEND violations "${label}: stale '${needle}' is still present")
    endif()
endmacro()

# #96 closes the documented CLAP surface only when Preset Discovery is no
# longer described as future work and the maintained production WCLAP bridge is
# the reproducible dependency in the examples documentation.
expect_contains(readme "clap.preset-discovery-factory/2"
    "extension matrix Preset Discovery row")
expect_contains(readme "hemduf/wclap-bridge"
    "maintained WCLAP bridge reproduction")
expect_contains(readme "92fa28be64c59a6b815793b9dd752fc1d461d635"
    "pinned maintained WCLAP bridge revision")
expect_contains(readme "WCLAP advertises bundled factory presets through a PLUGIN location and does not advertise native FILE user-preset locations"
    "WCLAP preset location policy")
expect_contains(readme "patch_clap_validator_preset_discovery.cmake"
    "native preset validator reproduction")
expect_contains(readme "webview_gui_examples_preset_discovery_factory"
    "focused native Preset Discovery reproduction")
expect_contains(readme "webview_gui_examples_preset_load"
    "focused native preset-load reproduction")

# These statements were correct during staged implementation but must not
# survive the final #37 closeout.
expect_absent(readme "Preset Discovery remains a separate #37 stage"
    "staged Preset Discovery wording")
expect_absent(readme "WebCLAP/wclap-bridge` commit `cd11d22afbe2af350f24cd56e6e0536e5ca86452`"
    "obsolete production WCLAP bridge pin")
expect_absent(readme "Preset files are not yet part of the artifact gate because the remaining #37"
    "staged native artifact wording")

# Preserve #95's fail-closed ownership as part of the final closeout rather
# than allowing #96 documentation work to make preset qualification advisory.
expect_contains(native_ci_contract "webview_gui_examples_preset_discovery_factory"
    "native CI Preset Discovery ownership")
expect_contains(native_ci_contract "webview_gui_examples_preset_load"
    "native CI preset-load ownership")
expect_absent(preset_ci "continue-on-error"
    "preset contract blocking behavior")

# Documentation is part of the final contract, so changes to it must trigger
# this watchdog on both main pushes and pull requests.
string(REGEX MATCHALL "examples/README\\.md" readme_trigger_matches "${preset_ci}")
list(LENGTH readme_trigger_matches readme_trigger_count)
if(readme_trigger_count LESS 2)
    list(APPEND violations
        "preset documentation watchdog: examples/README.md appears ${readme_trigger_count} time(s), expected push and pull_request coverage")
endif()

if(violations)
    list(JOIN violations "\n - " violation_text)
    message(FATAL_ERROR "#96 final qualification contract RED:\n - ${violation_text}")
endif()

message(STATUS "#96 final preset qualification contract passed")
