if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR)
    message(FATAL_ERROR "WEBVIEW_GUI_SOURCE_DIR is required")
endif()

function(require_entry_contract relative_path include_marker factory_marker)
    set(path "${WEBVIEW_GUI_SOURCE_DIR}/${relative_path}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing entry source: ${path}")
    endif()
    file(READ "${path}" source)
    string(FIND "${source}" "${include_marker}" include_pos)
    if(include_pos EQUAL -1)
        message(FATAL_ERROR "${relative_path} does not include ${include_marker}")
    endif()
    string(FIND "${source}" "${factory_marker}" factory_pos)
    if(factory_pos EQUAL -1)
        message(FATAL_ERROR "${relative_path} does not route Preset Discovery through ${factory_marker}")
    endif()
endfunction()

require_entry_contract(
    "examples/gain/gain_entry.cpp"
    "gain_preset_discovery.h"
    "gainPresetDiscoveryFactory")
require_entry_contract(
    "examples/polysynth/polysynth_entry.cpp"
    "polysynth_preset_discovery.h"
    "polysynthPresetDiscoveryFactory")
