cmake_minimum_required(VERSION 3.24)
project(webview_gui_preset_portability_contract LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBVIEW_GUI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(WEBVIEW_GUI_PRESET_INCLUDE_DIR "${WEBVIEW_GUI_ROOT}/examples/common/presets")
set(WEBVIEW_GUI_CHOC_INCLUDE_DIR "${WEBVIEW_GUI_ROOT}/include/webview-gui/_impl/platform/choc")
set(WEBVIEW_GUI_BUNDLED_PRESET_ROOT "${WEBVIEW_GUI_PRESET_INCLUDE_DIR}/bundled")

add_executable(webview_gui_preset_portability_contract
    preset_storage_contract_tests.cpp
)
target_include_directories(webview_gui_preset_portability_contract PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIR}
)
target_compile_features(webview_gui_preset_portability_contract PRIVATE cxx_std_17)

add_executable(webview_gui_preset_factory_bundle_contract
    factory_bundle_contract_tests.cpp
)
target_include_directories(webview_gui_preset_factory_bundle_contract PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIR}
)
target_compile_definitions(webview_gui_preset_factory_bundle_contract PRIVATE
    WEBVIEW_GUI_BUNDLED_PRESET_ROOT="${WEBVIEW_GUI_BUNDLED_PRESET_ROOT}")
target_compile_features(webview_gui_preset_factory_bundle_contract PRIVATE cxx_std_17)

find_package(Threads REQUIRED)
add_executable(webview_gui_native_preset_adapter_contract
    native_storage_adapter_tests.cpp
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}/native_preset_storage.cpp
)
target_include_directories(webview_gui_native_preset_adapter_contract PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIR}
)
target_link_libraries(webview_gui_native_preset_adapter_contract PRIVATE Threads::Threads)
target_compile_features(webview_gui_native_preset_adapter_contract PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_preset_portability_contract
    COMMAND webview_gui_preset_portability_contract)
add_test(NAME webview_gui_preset_factory_bundle_contract
    COMMAND webview_gui_preset_factory_bundle_contract)
add_test(NAME webview_gui_native_preset_adapter_contract
    COMMAND webview_gui_native_preset_adapter_contract)
