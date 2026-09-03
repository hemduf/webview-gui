cmake_minimum_required(VERSION 3.24)
project(webview_gui_native_preset_storage_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBVIEW_GUI_EXAMPLES_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(WEBVIEW_GUI_COMMON_DIR ${WEBVIEW_GUI_EXAMPLES_DIR}/common)
set(WEBVIEW_GUI_PRESET_DIR ${WEBVIEW_GUI_COMMON_DIR}/presets)
set(WEBVIEW_GUI_CHOC_DIR ${WEBVIEW_GUI_EXAMPLES_DIR}/../include/webview-gui/_impl/platform/choc)

add_executable(webview_gui_native_preset_storage_paths_tests
    native_preset_storage_paths_tests.cpp
)
target_include_directories(webview_gui_native_preset_storage_paths_tests PRIVATE
    ${WEBVIEW_GUI_COMMON_DIR}
    ${WEBVIEW_GUI_PRESET_DIR}
    ${WEBVIEW_GUI_CHOC_DIR}
)
target_compile_features(webview_gui_native_preset_storage_paths_tests PRIVATE cxx_std_17)

add_executable(webview_gui_native_preset_storage_tests
    native_preset_storage_tests.cpp
)
target_include_directories(webview_gui_native_preset_storage_tests PRIVATE
    ${WEBVIEW_GUI_COMMON_DIR}
    ${WEBVIEW_GUI_PRESET_DIR}
    ${WEBVIEW_GUI_CHOC_DIR}
)
target_compile_features(webview_gui_native_preset_storage_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_examples_native_preset_storage_paths
    COMMAND webview_gui_native_preset_storage_paths_tests)
add_test(NAME webview_gui_examples_native_preset_storage
    COMMAND webview_gui_native_preset_storage_tests)
