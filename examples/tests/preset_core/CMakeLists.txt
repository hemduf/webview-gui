cmake_minimum_required(VERSION 3.24)
project(webview_gui_preset_core_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBVIEW_GUI_PRESET_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../common/presets)
set(WEBVIEW_GUI_CHOC_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../include/webview-gui/_impl/platform/choc)

add_executable(webview_gui_preset_core_tests
    preset_document_tests.cpp
)
target_include_directories(webview_gui_preset_core_tests PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
)
target_compile_features(webview_gui_preset_core_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_codec_tests
    preset_codec_tests.cpp
)
target_include_directories(webview_gui_preset_codec_tests PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIR}
)
target_compile_features(webview_gui_preset_codec_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_codec_string_safety_tests
    preset_codec_string_safety_tests.cpp
)
target_include_directories(webview_gui_preset_codec_string_safety_tests PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIR}
)
target_compile_features(webview_gui_preset_codec_string_safety_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_factory_catalog_tests
    preset_factory_catalog_tests.cpp
)
target_include_directories(webview_gui_preset_factory_catalog_tests PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIR}
)
target_compile_features(webview_gui_preset_factory_catalog_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_examples_preset_core
    COMMAND webview_gui_preset_core_tests)
add_test(NAME webview_gui_examples_preset_codec
    COMMAND webview_gui_preset_codec_tests)
add_test(NAME webview_gui_examples_preset_codec_string_safety
    COMMAND webview_gui_preset_codec_string_safety_tests)
add_test(NAME webview_gui_examples_preset_factory_catalog
    COMMAND webview_gui_preset_factory_catalog_tests)
add_test(NAME webview_gui_examples_preset_factory_catalog_isolation
    COMMAND ${CMAKE_COMMAND}
        -DCATALOG_HEADER=${WEBVIEW_GUI_PRESET_INCLUDE_DIR}/preset_factory_catalog.h
        -P ${CMAKE_CURRENT_LIST_DIR}/preset_factory_catalog_isolation_contract.cmake)
