cmake_minimum_required(VERSION 3.24)
project(webview_gui_native_preset_storage_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBVIEW_GUI_EXAMPLES_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(WEBVIEW_GUI_COMMON_DIR ${WEBVIEW_GUI_EXAMPLES_DIR}/common)
set(WEBVIEW_GUI_PRESET_DIR ${WEBVIEW_GUI_COMMON_DIR}/presets)
set(WEBVIEW_GUI_CHOC_DIR ${WEBVIEW_GUI_EXAMPLES_DIR}/../include/webview-gui/_impl/platform/choc)

add_library(webview_gui_native_preset_storage STATIC
    ${WEBVIEW_GUI_PRESET_DIR}/native_preset_storage.cpp
)
target_include_directories(webview_gui_native_preset_storage PUBLIC
    ${WEBVIEW_GUI_COMMON_DIR}
    ${WEBVIEW_GUI_PRESET_DIR}
    ${WEBVIEW_GUI_CHOC_DIR}
)
target_compile_features(webview_gui_native_preset_storage PUBLIC cxx_std_17)
if(WIN32)
    target_link_libraries(webview_gui_native_preset_storage PRIVATE shell32 ole32)
endif()

add_executable(webview_gui_native_preset_storage_paths_tests
    native_preset_storage_paths_tests.cpp
)
target_link_libraries(webview_gui_native_preset_storage_paths_tests PRIVATE
    webview_gui_native_preset_storage
)
target_compile_features(webview_gui_native_preset_storage_paths_tests PRIVATE cxx_std_17)

add_executable(webview_gui_native_preset_storage_tests
    native_preset_storage_tests.cpp
)
target_link_libraries(webview_gui_native_preset_storage_tests PRIVATE
    webview_gui_native_preset_storage
)
target_compile_features(webview_gui_native_preset_storage_tests PRIVATE cxx_std_17)

add_executable(webview_gui_native_preset_storage_race_tests
    native_preset_storage_race_tests.cpp
)
target_link_libraries(webview_gui_native_preset_storage_race_tests PRIVATE
    webview_gui_native_preset_storage
)
target_compile_features(webview_gui_native_preset_storage_race_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_examples_native_preset_storage_paths
    COMMAND webview_gui_native_preset_storage_paths_tests)
add_test(NAME webview_gui_examples_native_preset_storage
    COMMAND webview_gui_native_preset_storage_tests)
add_test(NAME webview_gui_examples_native_preset_storage_races
    COMMAND webview_gui_native_preset_storage_race_tests)
add_test(NAME webview_gui_examples_native_preset_storage_rt_isolation
    COMMAND ${CMAKE_COMMAND}
        -DGAIN_PLUGIN_FILE=${WEBVIEW_GUI_EXAMPLES_DIR}/gain/gain_plugin.cpp
        -DPOLYSYNTH_PLUGIN_FILE=${WEBVIEW_GUI_EXAMPLES_DIR}/polysynth/polysynth_plugin.cpp
        -P ${CMAKE_CURRENT_LIST_DIR}/native_preset_storage_rt_contract.cmake)
