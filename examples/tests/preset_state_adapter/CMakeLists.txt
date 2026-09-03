cmake_minimum_required(VERSION 3.24)
project(webview_gui_preset_state_adapter_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

set(WEBVIEW_GUI_EXAMPLES_CLAP_REVISION
    "a47f6badb49d948fd009998f28309cdab78979c9")
set(CLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(webview_gui_preset_adapter_clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG ${WEBVIEW_GUI_EXAMPLES_CLAP_REVISION}
)
FetchContent_MakeAvailable(webview_gui_preset_adapter_clap)

set(WEBVIEW_GUI_EXAMPLES_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(WEBVIEW_GUI_GAIN_DIR ${WEBVIEW_GUI_EXAMPLES_DIR}/gain)
set(WEBVIEW_GUI_POLYSYNTH_DIR ${WEBVIEW_GUI_EXAMPLES_DIR}/polysynth)
set(WEBVIEW_GUI_COMMON_DIR ${WEBVIEW_GUI_EXAMPLES_DIR}/common)
set(WEBVIEW_GUI_PRESET_DIR ${WEBVIEW_GUI_COMMON_DIR}/presets)
set(WEBVIEW_GUI_CHOC_DIR ${WEBVIEW_GUI_EXAMPLES_DIR}/../include/webview-gui/_impl/platform/choc)

add_executable(webview_gui_preset_gain_state_adapter_tests
    gain_preset_state_tests.cpp
)
target_include_directories(webview_gui_preset_gain_state_adapter_tests PRIVATE
    ${WEBVIEW_GUI_GAIN_DIR}
    ${WEBVIEW_GUI_COMMON_DIR}
    ${WEBVIEW_GUI_PRESET_DIR}
    ${WEBVIEW_GUI_CHOC_DIR}
)
target_link_libraries(webview_gui_preset_gain_state_adapter_tests PRIVATE clap)
target_compile_features(webview_gui_preset_gain_state_adapter_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_polysynth_state_adapter_tests
    polysynth_preset_state_tests.cpp
)
target_include_directories(webview_gui_preset_polysynth_state_adapter_tests PRIVATE
    ${WEBVIEW_GUI_POLYSYNTH_DIR}
    ${WEBVIEW_GUI_COMMON_DIR}
    ${WEBVIEW_GUI_PRESET_DIR}
    ${WEBVIEW_GUI_CHOC_DIR}
)
target_link_libraries(webview_gui_preset_polysynth_state_adapter_tests PRIVATE clap)
target_compile_features(webview_gui_preset_polysynth_state_adapter_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_examples_preset_gain_state_adapter
    COMMAND webview_gui_preset_gain_state_adapter_tests)
add_test(NAME webview_gui_examples_preset_polysynth_state_adapter
    COMMAND webview_gui_preset_polysynth_state_adapter_tests)
add_test(NAME webview_gui_examples_preset_state_plugin_integration
    COMMAND ${CMAKE_COMMAND}
        -DGAIN_PLUGIN_FILE=${WEBVIEW_GUI_GAIN_DIR}/gain_plugin.cpp
        -DPOLYSYNTH_PLUGIN_FILE=${WEBVIEW_GUI_POLYSYNTH_DIR}/polysynth_plugin.cpp
        -P ${CMAKE_CURRENT_LIST_DIR}/preset_state_plugin_integration_contract.cmake)
