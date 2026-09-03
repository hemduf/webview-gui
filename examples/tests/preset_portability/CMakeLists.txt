cmake_minimum_required(VERSION 3.24)
project(webview_gui_preset_portability_contract LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBVIEW_GUI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(WEBVIEW_GUI_PRESET_INCLUDE_DIR "${WEBVIEW_GUI_ROOT}/examples/common/presets")
set(WEBVIEW_GUI_CHOC_INCLUDE_DIR "${WEBVIEW_GUI_ROOT}/include/webview-gui/_impl/platform/choc")

add_executable(webview_gui_preset_portability_contract
    preset_storage_contract_tests.cpp
)
target_include_directories(webview_gui_preset_portability_contract PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIR}
)
target_compile_features(webview_gui_preset_portability_contract PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_preset_portability_contract
    COMMAND webview_gui_preset_portability_contract)
