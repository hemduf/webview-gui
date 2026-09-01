cmake_minimum_required(VERSION 3.24)

foreach(required IN ITEMS
        GAIN_CORE_WORKFLOW
        POLYSYNTH_CORE_WORKFLOW
        GAIN_WCLAP_WORKFLOW
        POLYSYNTH_WCLAP_WORKFLOW
        FORMATS_WORKFLOW
        README_FILE)
    if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
        message(FATAL_ERROR "${required} must point to an existing file")
    endif()
endforeach()

file(READ "${GAIN_CORE_WORKFLOW}" gain_core)
file(READ "${POLYSYNTH_CORE_WORKFLOW}" polysynth_core)
file(READ "${GAIN_WCLAP_WORKFLOW}" gain_wclap)
file(READ "${POLYSYNTH_WCLAP_WORKFLOW}" polysynth_wclap)
file(READ "${FORMATS_WORKFLOW}" formats)
file(READ "${README_FILE}" readme)

function(require_contains haystack_var needle label)
    string(FIND "${${haystack_var}}" "${needle}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Missing ${label}: ${needle}")
    endif()
endfunction()

# Native CLAP validation remains owned by the plug-in development workflows.
foreach(workflow_var IN ITEMS gain_core polysynth_core)
    require_contains(${workflow_var}
        "152b9823e992d782c5c1fd33bca0295478b919aa"
        "pinned clap-validator revision")
    require_contains(${workflow_var} "macos-latest" "macOS validator platform")
    require_contains(${workflow_var} "ubuntu-latest" "Linux validator platform")
    require_contains(${workflow_var} "windows-latest" "Windows validator platform")
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

# #34 owns production of the wrapped products; #35 upgrades that matrix from
# build-only to actual format/artifact qualification.
require_contains(formats "ubuntu-latest" "Linux wrapper platform")
require_contains(formats "macos-latest" "macOS wrapper platform")
require_contains(formats "windows-latest" "Windows wrapper platform")
require_contains(formats "WEBVIEW_GUI_EXAMPLES_FORMAT_VST3=ON" "VST3 wrapper build")
require_contains(formats "WEBVIEW_GUI_EXAMPLES_FORMAT_STANDALONE=ON" "standalone wrapper build")
require_contains(formats "WEBVIEW_GUI_EXAMPLES_FORMAT_AUV2=ON" "AUv2 wrapper build")
require_contains(formats "WEBVIEW_GUI_EXAMPLES_FORMAT_AUV3=ON" "AUv3 wrapper build")

# RED for #35: these are the remaining aggregate qualification gates.
require_contains(formats "Build Steinberg VST3 validator" "VST3 validator build step")
require_contains(formats "Validate wrapped VST3 products" "VST3 validation step")
require_contains(formats "Run auval for AUv2" "AUv2 auval step")
require_contains(formats "auval -v aufx WvGn WvGu" "Gain AUv2 auval invocation")
require_contains(formats "auval -v aumu WvPs WvGu" "PolySynth AUv2 auval invocation")
require_contains(formats "Verify native artifact hygiene" "native artifact hygiene step")
require_contains(formats "GITHUB_WORKSPACE" "source-tree path hygiene input")

require_contains(readme "VST3 validator" "VST3 validator reproduction documentation")
require_contains(readme "auval" "AUv2 validator reproduction documentation")

message(STATUS "Aggregate example qualification contract is complete")
