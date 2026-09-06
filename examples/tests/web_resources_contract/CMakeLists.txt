cmake_minimum_required(VERSION 3.24)
project(webview_gui_web_resources_contract LANGUAGES CXX)

include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/WebResources.cmake")

if(NOT WEBVIEW_GUI_TEST_UI_SOURCE_DIR)
    set(WEBVIEW_GUI_TEST_UI_SOURCE_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../gain/ui")
endif()

webview_gui_add_web_resources(
    TARGET fixture_resources
    SOURCE_DIR "${WEBVIEW_GUI_TEST_UI_SOURCE_DIR}"
    NAMESPACE "fixture::resources"
)

add_executable(webview_gui_web_resources_contract main.cpp)
target_link_libraries(webview_gui_web_resources_contract PRIVATE fixture_resources)
target_compile_features(webview_gui_web_resources_contract PRIVATE cxx_std_17)

add_test(NAME webview_gui_web_resources_contract
    COMMAND webview_gui_web_resources_contract)
