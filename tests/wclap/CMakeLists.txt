cmake_minimum_required(VERSION 3.24)
project(webview_gui_wclap_smoke LANGUAGES C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG a47f6badb49d948fd009998f28309cdab78979c9
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(clap)

add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/../..
    ${CMAKE_CURRENT_BINARY_DIR}/webview-gui-under-test
    EXCLUDE_FROM_ALL
)

add_executable(webview_gui_wclap_smoke smoke_plugin.cpp)
target_include_directories(webview_gui_wclap_smoke PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include
    ${clap_SOURCE_DIR}/include
)
target_link_libraries(webview_gui_wclap_smoke PRIVATE webview-gui)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/WebviewGuiWclap.cmake)
webview_gui_configure_wclap_target(webview_gui_wclap_smoke
    OUTPUT_NAME WebviewGuiSmoke
    RESOURCE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/resources
)
