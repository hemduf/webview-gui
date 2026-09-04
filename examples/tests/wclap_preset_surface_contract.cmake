if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR)
    message(FATAL_ERROR "WEBVIEW_GUI_SOURCE_DIR is required")
endif()

set(gain_entry "${WEBVIEW_GUI_SOURCE_DIR}/examples/gain/wclap/gain_wclap_entry.cpp")
set(poly_entry "${WEBVIEW_GUI_SOURCE_DIR}/examples/polysynth/wclap/polysynth_wclap_entry.cpp")
set(catalog_header "${WEBVIEW_GUI_SOURCE_DIR}/examples/common/preset_production_catalog.h")

foreach(path IN ITEMS "${gain_entry}" "${poly_entry}" "${catalog_header}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missing #94 contract input: ${path}")
    endif()
endforeach()

file(READ "${gain_entry}" gain)
file(READ "${poly_entry}" poly)
file(READ "${catalog_header}" catalog)

string(FIND "${gain}" "gainPresetDiscoveryFactory" gain_factory)
if(gain_factory EQUAL -1)
    message(FATAL_ERROR "#94 RED: Gain WCLAP entry does not expose Preset Discovery")
endif()

string(FIND "${poly}" "polysynthPresetDiscoveryFactory" poly_factory)
if(poly_factory EQUAL -1)
    message(FATAL_ERROR "#94 RED: PolySynth WCLAP entry does not expose Preset Discovery")
endif()

string(FIND "${catalog}" "class WasiFactoryPresetCatalog" wasi_catalog)
if(wasi_catalog EQUAL -1)
    message(FATAL_ERROR "#94 RED: production WASI factory preset catalog is still unavailable")
endif()

string(FIND "${catalog}" "nativeUserLocation(std::string_view &) const noexcept override" no_file_location)
if(no_file_location EQUAL -1)
    message(FATAL_ERROR "#94 contract: WASI catalog must explicitly omit native FILE discovery")
endif()
