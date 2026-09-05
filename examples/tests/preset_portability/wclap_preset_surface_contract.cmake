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
read_required("examples/common/presets/preset_factory_catalog.h" factory_catalog)
read_required("examples/common/preset_wasi_production_catalog.h" wasi_catalog)
read_required("examples/common/preset_load_controller.h" load_controller)

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

# #92 deliberately shipped an empty WASI factory catalog. #94 replaces it with
# the canonical #101 definitions and keeps a parser-free PresetDocument path.
foreach(required_token IN ITEMS
        "PresetDocument document"
        "kGainFactoryDefinitions"
        "kPolyFactoryDefinitions")
    string(FIND "${factory_catalog}" "${required_token}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "#94 RED: WASI factory catalog is still incomplete; missing '${required_token}'")
    endif()
endforeach()

foreach(required_token IN ITEMS
        "WasiProductionPresetCatalog"
        "loadFactory"
        "enumerateFactoryMetadata"
        "WCLAP FILE preset discovery is unavailable"
        "WCLAP FILE preset loading is unavailable")
    string(FIND "${wasi_catalog}" "${required_token}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "#94 RED: WASI production catalog is incomplete; missing '${required_token}'")
    endif()
endforeach()

string(FIND "${wasi_catalog}" "preset_codec.h" wasi_codec_dependency)
if(NOT wasi_codec_dependency EQUAL -1)
    message(FATAL_ERROR "#94: WCLAP production catalog must not import the CHOC-backed preset codec")
endif()

foreach(required_token IN ITEMS
        "PresetDocumentStateSink"
        "validateLocationShape"
        "catalog.loadFactory(loadKey, sink)"
        "notifyHostParameterValuesChanged"
        "hostPresetLoad->loaded")
    string(FIND "${load_controller}" "${required_token}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "#94 RED: WCLAP transactional preset-load path is incomplete; missing '${required_token}'")
    endif()
endforeach()

string(FIND "${load_controller}" "WCLAP preset loading is deferred to #94" deferred_stub)
if(NOT deferred_stub EQUAL -1)
    message(FATAL_ERROR "#94: deferred WCLAP preset-load stub is still reachable")
endif()

message(STATUS "#94 WCLAP preset entry/catalog/load/location source contract satisfied")
