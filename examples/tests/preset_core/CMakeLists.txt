cmake_minimum_required(VERSION 3.24)
project(webview_gui_preset_core_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(webview_gui_preset_core_tests
    preset_document_tests.cpp
)
target_include_directories(webview_gui_preset_core_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common/presets
)
target_compile_features(webview_gui_preset_core_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_codec_tests
    preset_codec_tests.cpp
)
target_include_directories(webview_gui_preset_codec_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common/presets
)
target_compile_features(webview_gui_preset_codec_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_examples_preset_core
    COMMAND webview_gui_preset_core_tests)
add_test(NAME webview_gui_examples_preset_codec
    COMMAND webview_gui_preset_codec_tests)
