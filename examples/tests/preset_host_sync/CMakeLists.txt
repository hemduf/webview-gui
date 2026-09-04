cmake_minimum_required(VERSION 3.24)
project(webview_gui_preset_host_sync_tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WEBVIEW_GUI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH
    "Path to the webview-gui repository root")
set(WEBVIEW_GUI_EXAMPLES_BUILD_GAIN ON CACHE BOOL "" FORCE)
set(WEBVIEW_GUI_EXAMPLES_BUILD_POLYSYNTH ON CACHE BOOL "" FORCE)
set(WEBVIEW_GUI_EXAMPLES_BUILD_WRAPPERS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_GUI_EXAMPLES_FORMAT_CLAP ON CACHE BOOL "" FORCE)

add_subdirectory("${WEBVIEW_GUI_SOURCE_DIR}/examples"
    "${CMAKE_BINARY_DIR}/examples-under-test")

add_executable(webview_gui_preset_host_sync_tests
    preset_host_sync_tests.cpp
)
target_include_directories(webview_gui_preset_host_sync_tests PRIVATE
    "${WEBVIEW_GUI_SOURCE_DIR}/examples/gain"
    "${WEBVIEW_GUI_SOURCE_DIR}/examples/polysynth"
    "${WEBVIEW_GUI_SOURCE_DIR}/examples/polysynth/wclap"
    "${WEBVIEW_GUI_SOURCE_DIR}/examples/common"
    "${WEBVIEW_GUI_SOURCE_DIR}/include"
)
target_link_libraries(webview_gui_preset_host_sync_tests PRIVATE
    webview_gui_example_gain_clap_core
    webview_gui_example_polysynth_clap_core
    webview-gui
    clap
)
target_compile_features(webview_gui_preset_host_sync_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_notification_tests
    preset_notification_tests.cpp
)
target_include_directories(webview_gui_preset_notification_tests PRIVATE
    "${WEBVIEW_GUI_SOURCE_DIR}/examples/common"
)
target_link_libraries(webview_gui_preset_notification_tests PRIVATE clap)
target_compile_features(webview_gui_preset_notification_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_preset_host_sync
    COMMAND webview_gui_preset_host_sync_tests)
add_test(NAME webview_gui_preset_notification
    COMMAND webview_gui_preset_notification_tests)
