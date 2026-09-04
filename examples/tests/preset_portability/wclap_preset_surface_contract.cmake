if(NOT DEFINED WEBVIEW_GUI_ROOT OR "${WEBVIEW_GUI_ROOT}" STREQUAL "")
    message(FATAL_ERROR "WEBVIEW_GUI_ROOT is required")
endif()

function(read_required relative_path out_var)
    set(path "${WEBVIEW_GUI_ROOT}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missing required file: ${relative_path}")
    endif()
    file(READ "${path}" content)
    set(${out_var} "${content}" PARENT_SCOPE)
endfunction()

read_required("examples/gain/wclap/gain_wclap_entry.cpp" gain_entry)
read_required("examples/polysynth/wclap/polysynth_wclap_entry.cpp" polysynth_entry)
read_required("examples/common/preset_discovery_factory.h" discovery_factory)

function(require_wclap_preset_factory entry_var plugin_name discovery_header discovery_factory_name)
    set(entry "${${entry_var}}")

    foreach(required_token IN ITEMS
            "${discovery_header}"
            "presets::classifyPresetClapId(factoryId)"
            "presets::ClapPresetSurface::EntryFactory"
            "${discovery_factory_name}()")
        string(FIND "${entry}" "${required_token}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR
                "#94 RED: ${plugin_name} WCLAP entry does not expose Preset Discovery; missing '${required_token}'")
        endif()
    endforeach()
endfunction()

require_wclap_preset_factory(
    gain_entry
    "Gain"
    "../gain_preset_discovery.h"
    "gainPresetDiscoveryFactory")
require_wclap_preset_factory(
    polysynth_entry
    "PolySynth"
    "../polysynth_preset_discovery.h"
    "polysynthPresetDiscoveryFactory")

# WCLAP/WASI must only advertise the bundled PLUGIN location. Native FILE user
# locations are deliberately omitted when __wasi__ is defined.
foreach(required_token IN ITEMS
        "#if !defined(__wasi__)"
        "CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN")
    string(FIND "${discovery_factory}" "${required_token}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "#94: WCLAP preset discovery location policy is missing '${required_token}'")
    endif()
endforeach()

message(STATUS "#94 WCLAP preset entry/location source contract satisfied")
