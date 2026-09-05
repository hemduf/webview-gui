cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR)
    message(FATAL_ERROR "WEBVIEW_GUI_SOURCE_DIR is required")
endif()

set(gain_workflow "${WEBVIEW_GUI_SOURCE_DIR}/.github/workflows/gain-core.yml")
set(polysynth_workflow "${WEBVIEW_GUI_SOURCE_DIR}/.github/workflows/polysynth-core.yml")
set(preset_workflow "${WEBVIEW_GUI_SOURCE_DIR}/.github/workflows/preset-contract.yml")
set(aggregate_contract "${WEBVIEW_GUI_SOURCE_DIR}/examples/tests/aggregate_qualification_contract.cmake")
set(validator_config "${WEBVIEW_GUI_SOURCE_DIR}/clap-validator.toml")

foreach(required_file IN ITEMS
        "${gain_workflow}"
        "${polysynth_workflow}"
        "${preset_workflow}"
        "${aggregate_contract}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "#95 native CI contract input is missing: ${required_file}")
    endif()
endforeach()

file(READ "${gain_workflow}" gain_ci)
file(READ "${polysynth_workflow}" polysynth_ci)
file(READ "${preset_workflow}" preset_ci)
file(READ "${aggregate_contract}" aggregate_ci)
if(EXISTS "${validator_config}")
    file(READ "${validator_config}" validator_cfg)
else()
    set(validator_cfg "")
endif()

set(violations)

macro(expect_contains haystack_var needle label)
    string(FIND "${${haystack_var}}" "${needle}" found_at)
    if(found_at EQUAL -1)
        list(APPEND violations "${label}: missing '${needle}'")
    endif()
endmacro()

macro(expect_absent haystack_var needle label)
    string(FIND "${${haystack_var}}" "${needle}" found_at)
    if(NOT found_at EQUAL -1)
        list(APPEND violations "${label}: unexpected '${needle}'")
    endif()
endmacro()

# Split the watchdog workflow into its two filtered event sections. This avoids
# a false green where a guarded path is duplicated in one list but absent from
# the other.
string(FIND "${preset_ci}" "  push:\n" preset_push_start)
string(FIND "${preset_ci}" "  pull_request:\n" preset_pull_start)
string(FIND "${preset_ci}" "  workflow_dispatch:" preset_dispatch_start)
if(preset_push_start EQUAL -1 OR preset_pull_start EQUAL -1 OR preset_dispatch_start EQUAL -1
   OR NOT preset_push_start LESS preset_pull_start
   OR NOT preset_pull_start LESS preset_dispatch_start)
    message(FATAL_ERROR "#95 could not parse preset-contract.yml event sections")
endif()
math(EXPR preset_push_length "${preset_pull_start} - ${preset_push_start}")
math(EXPR preset_pull_length "${preset_dispatch_start} - ${preset_pull_start}")
string(SUBSTRING "${preset_ci}" ${preset_push_start} ${preset_push_length} preset_push_ci)
string(SUBSTRING "${preset_ci}" ${preset_pull_start} ${preset_pull_length} preset_pull_ci)

# Preserve the already-blocking native validator baseline while #95 adds preset
# ownership. A failure here means the baseline regressed independently of #95.
foreach(workflow_var IN ITEMS gain_ci polysynth_ci)
    expect_contains(${workflow_var}
        "152b9823e992d782c5c1fd33bca0295478b919aa"
        "pinned clap-validator revision")
    expect_contains(${workflow_var} "macos-latest" "macOS native validator matrix")
    expect_contains(${workflow_var} "ubuntu-latest" "Linux native validator matrix")
    expect_contains(${workflow_var} "windows-latest" "Windows native validator matrix")
    expect_absent(${workflow_var} "continue-on-error" "blocking native validator")
endforeach()
expect_contains(gain_ci
    "webview_gui_example_gain_validate"
    "Gain exact-artifact validator target")
expect_contains(polysynth_ci
    "webview_gui_example_polysynth_validate"
    "PolySynth exact-artifact validator target")

# Each plug-in-owned native workflow must itself execute the deterministic
# Preset Discovery + preset-load contracts, rather than relying only on a
# separate shared workflow or on opaque validator coverage.
foreach(workflow_var IN ITEMS gain_ci polysynth_ci)
    expect_contains(${workflow_var}
        "webview_gui_examples_preset_discovery_factory"
        "plug-in-owned Preset Discovery gate")
    expect_contains(${workflow_var}
        "webview_gui_examples_preset_load"
        "plug-in-owned preset-load gate")

    # Shared preset implementation/resource changes must trigger these native
    # three-OS gates as well as plug-in-local changes.
    expect_contains(${workflow_var}
        "examples/common/preset_*.h"
        "shared preset header pull-request trigger")
    expect_contains(${workflow_var}
        "examples/common/preset_production_catalog.cpp"
        "shared preset implementation pull-request trigger")
    expect_contains(${workflow_var}
        "examples/common/presets/**"
        "shared preset resource/storage pull-request trigger")
endforeach()

# #35 aggregate qualification must guard those ownership markers so they cannot
# silently disappear from one of the plug-in workflows later.
expect_contains(aggregate_ci
    "webview_gui_examples_preset_discovery_factory"
    "aggregate Preset Discovery ownership guard")
expect_contains(aggregate_ci
    "webview_gui_examples_preset_load"
    "aggregate preset-load ownership guard")

# The watchdog itself must run whenever a guarded CI surface changes, on both
# main pushes and pull requests.
foreach(guarded_path IN ITEMS
        "examples/common/preset_*.h"
        ".github/workflows/gain-core.yml"
        ".github/workflows/polysynth-core.yml"
        "examples/tests/aggregate_qualification_contract.cmake"
        ".github/scripts/patch_clap_validator_preset_discovery.cmake"
        "clap-validator.toml")
    expect_contains(preset_push_ci "${guarded_path}"
        "preset contract push watchdog trigger")
    expect_contains(preset_pull_ci "${guarded_path}"
        "preset contract pull-request watchdog trigger")
endforeach()

# The #91 staged validator workaround is explicitly temporary. #95 must remove
# all three Preset Discovery exclusions (or remove the config entirely) before
# native preset validation can be considered blocking.
foreach(disabled_test IN ITEMS
        "preset-discovery-crawl = false"
        "preset-discovery-load = false"
        "preset-discovery-descriptor-consistency = false")
    expect_absent(validator_cfg "${disabled_test}" "temporary Preset Discovery validator exclusion")
endforeach()

if(violations)
    string(JOIN "\n - " violation_text ${violations})
    message(FATAL_ERROR
        "#95: native preset validation ownership contract failed:\n - ${violation_text}")
endif()

message(STATUS "#95 native preset validation ownership contract satisfied")
