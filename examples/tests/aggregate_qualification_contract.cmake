cmake_minimum_required(VERSION 3.24)

foreach(required IN ITEMS
        CI_WORKFLOW
        GAIN_CORE_WORKFLOW
        POLYSYNTH_CORE_WORKFLOW
        GAIN_WCLAP_WORKFLOW
        POLYSYNTH_WCLAP_WORKFLOW
        FORMATS_WORKFLOW
        SANITIZER_WORKFLOW
        README_FILE)
    if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
        message(FATAL_ERROR "${required} must point to an existing file")
    endif()
endforeach()

file(READ "${CI_WORKFLOW}" ci)
file(READ "${GAIN_CORE_WORKFLOW}" gain_core)
file(READ "${POLYSYNTH_CORE_WORKFLOW}" polysynth_core)
file(READ "${GAIN_WCLAP_WORKFLOW}" gain_wclap)
file(READ "${POLYSYNTH_WCLAP_WORKFLOW}" polysynth_wclap)
file(READ "${FORMATS_WORKFLOW}" formats)
file(READ "${SANITIZER_WORKFLOW}" example_sanitizers)
file(READ "${README_FILE}" readme)

function(require_contains haystack_var needle label)
    string(FIND "${${haystack_var}}" "${needle}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Missing ${label}: ${needle}")
    endif()
endfunction()

function(require_absent haystack_var needle label)
    string(FIND "${${haystack_var}}" "${needle}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR "Unexpected ${label}: ${needle}")
    endif()
endfunction()

# Native CLAP validation remains owned by the plug-in development workflows.
# #95 additionally requires deterministic Preset Discovery + preset-load gates
# in those same three-OS matrices so the preset surface cannot become advisory.
foreach(workflow_var IN ITEMS gain_core polysynth_core)
    require_contains(${workflow_var}
        "152b9823e992d782c5c1fd33bca0295478b919aa"
        "pinned clap-validator revision")
    require_contains(${workflow_var} "macos-latest" "macOS validator platform")
    require_contains(${workflow_var} "ubuntu-latest" "Linux validator platform")
    require_contains(${workflow_var} "windows-latest" "Windows validator platform")
    require_contains(${workflow_var}
        "webview_gui_examples_preset_discovery_factory"
        "plug-in-owned Preset Discovery gate")
    require_contains(${workflow_var}
        "webview_gui_examples_preset_load"
        "plug-in-owned preset-load gate")
    require_contains(${workflow_var}
        "patch_clap_validator_preset_discovery.cmake"
        "pinned validator Preset Discovery fix")
    require_absent(${workflow_var} "continue-on-error" "blocking native validator/preset gate")
endforeach()
require_contains(gain_core
    "webview_gui_example_gain_validate"
    "Gain blocking clap-validator target")
require_contains(polysynth_core
    "webview_gui_example_polysynth_validate"
    "PolySynth blocking clap-validator target")

# WCLAP must validate a real bundle through the pinned host path, not just compile wasm.
foreach(workflow_var IN ITEMS gain_wclap polysynth_wclap)
    require_contains(${workflow_var} "WASI_VERSION: '33'" "pinned WASI SDK")
    require_contains(${workflow_var} "clap-trap validate" "real WCLAP lifecycle host")
    require_contains(${workflow_var} "native GUI import leaked into WCLAP" "native dependency isolation")
    require_contains(${workflow_var} ".wclap.tar.gz" "WCLAP distributable archive")
endforeach()

# The repository-wide sanitizers remain a library-level safety net on main, while
# #35 also qualifies representative Gain/PolySynth processor and handoff binaries
# directly on pull requests.
require_contains(ci "sanitizers:" "repository ASan/UBSan job")
require_contains(ci "thread-sanitizer:" "repository TSan job")
require_contains(example_sanitizers "Examples ASan+UBSan" "example ASan/UBSan matrix")
require_contains(example_sanitizers "Examples TSan" "example TSan job")
require_contains(example_sanitizers
    "webview_gui_example_gain_clap_tests"
    "Gain processor/CLAP sanitizer target")
require_contains(example_sanitizers
    "webview_gui_example_polysynth_state_snapshot_concurrency_tests"
    "PolySynth concurrency sanitizer target")
require_contains(example_sanitizers
    "polysynth_auv2_float32_parameter_tests.cpp"
    "AUv2 Float32 endpoint regression")
require_contains(example_sanitizers "-fsanitize=address,undefined" "ASan/UBSan compiler flags")
require_contains(example_sanitizers "-fsanitize=thread" "TSan compiler flags")

# #34 owns production of the wrapped products; #35 upgrades that matrix from
# build-only to actual format/artifact qualification.
require_contains(formats "ubuntu-latest" "Linux wrapper platform")
require_contains(formats "macos-latest" "macOS wrapper platform")
require_contains(formats "windows-latest" "Windows wrapper platform")
require_contains(formats "WEBVIEW_GUI_EXAMPLES_FORMAT_VST3=ON" "VST3 wrapper build")
require_contains(formats "WEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=ON" "standalone wrapper build")
require_contains(formats "WEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=ON" "AUv2 wrapper build")
require_contains(formats "WEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=ON" "AUv3 wrapper build")

require_contains(formats "Build Steinberg VST3 validator" "VST3 validator build step")
require_contains(formats
    "cmake -S examples/tests/vst3-validator -B build-vst3-validator"
    "isolated VST3 validator driver")
require_absent(formats
    "cmake -S build-formats/cpm/vst3sdk -B build-vst3-validator"
    "full Steinberg SDK project used for validator-only build")
require_contains(formats "Validate wrapped VST3 products" "VST3 validation step")
require_contains(formats "Run auval for AUv2" "AUv2 auval step")
require_contains(formats "auval -v aufx WvGn WvGu" "Gain AUv2 auval invocation")
require_contains(formats "auval -v aumu WvPs WvGu" "PolySynth AUv2 auval invocation")
require_contains(formats "Verify native artifact hygiene" "native artifact hygiene step")
require_contains(formats "GITHUB_WORKSPACE" "source-tree path hygiene input")

require_contains(readme "Native CLAP validator reproduction" "native CLAP validator reproduction documentation")
require_contains(readme "152b9823e992d782c5c1fd33bca0295478b919aa" "documented clap-validator pin")
require_contains(readme "WCLAP / WASI reproduction" "WCLAP/WASI reproduction documentation")
require_contains(readme "wasi-sdk-33.0" "documented WASI SDK version")
require_contains(readme "VST3 validator" "VST3 validator reproduction documentation")
require_contains(readme "auval" "AUv2 validator reproduction documentation")

message(STATUS "Aggregate example qualification contract is complete")
