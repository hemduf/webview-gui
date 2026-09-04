if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR)
    message(FATAL_ERROR "WEBVIEW_GUI_SOURCE_DIR is required")
endif()

foreach(plugin IN ITEMS gain polysynth)
    set(source "${WEBVIEW_GUI_SOURCE_DIR}/examples/${plugin}/${plugin}_plugin.cpp")
    file(READ "${source}" content)
    foreach(required IN ITEMS
            "preset_load_controller.h"
            "preset_production_catalog.h"
            "implementsPresetLoad() const noexcept override"
            "presetLoadFromLocation("
            "makeDefaultProductionPresetCatalog("
            "loadPresetFromLocation(")
        string(FIND "${content}" "${required}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR
                "#92 ${plugin} preset-load surface missing marker: ${required}")
        endif()
    endforeach()
endforeach()

# #92 must reuse the existing commit path without opting into #93 host parameter
# invalidation. Both plug-ins already expose apply*PresetDocument() helpers whose
# commits use notifyHostParams=false.
foreach(pair IN ITEMS
        "gain;applyGainPresetDocument"
        "polysynth;applyPolySynthPresetDocument")
    list(GET pair 0 plugin)
    list(GET pair 1 marker)
    set(source "${WEBVIEW_GUI_SOURCE_DIR}/examples/${plugin}/${plugin}_plugin.cpp")
    file(READ "${source}" content)
    string(FIND "${content}" "${marker}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "#92 ${plugin} missing existing transactional commit adapter ${marker}")
    endif()
endforeach()
