cmake_minimum_required(VERSION 3.24)
project(webview_gui_wclap_contract LANGUAGES CXX)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG a47f6badb49d948fd009998f28309cdab78979c9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(clap)

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_BINARY_DIR}/webview-gui-under-test"
    EXCLUDE_FROM_ALL
)

add_executable(webview_gui_wclap_host_webview_contract
    wclap_host_webview_contract.cpp
)
target_include_directories(webview_gui_wclap_host_webview_contract PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${clap_SOURCE_DIR}/include"
)
target_compile_definitions(webview_gui_wclap_host_webview_contract PRIVATE
    WEBVIEW_GUI_TESTING=1
)
target_link_libraries(webview_gui_wclap_host_webview_contract PRIVATE
    webview-gui
)

add_test(
    NAME webview_gui_wclap_host_webview_contract
    COMMAND webview_gui_wclap_host_webview_contract
)

add_executable(webview_gui_wclap_bridge_init_negotiation_contract
    wclap_bridge_init_negotiation_contract.cpp
)
target_include_directories(webview_gui_wclap_bridge_init_negotiation_contract PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${clap_SOURCE_DIR}/include"
)
target_compile_definitions(webview_gui_wclap_bridge_init_negotiation_contract PRIVATE
    WEBVIEW_GUI_TESTING=1
)
target_link_libraries(webview_gui_wclap_bridge_init_negotiation_contract PRIVATE
    webview-gui
)

add_test(
    NAME webview_gui_wclap_bridge_init_negotiation_contract
    COMMAND webview_gui_wclap_bridge_init_negotiation_contract
)

add_executable(webview_gui_wclap_init_negotiation_contract
    wclap_init_negotiation_contract.cpp
)
target_include_directories(webview_gui_wclap_init_negotiation_contract PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${clap_SOURCE_DIR}/include"
)
target_compile_definitions(webview_gui_wclap_init_negotiation_contract PRIVATE
    WEBVIEW_GUI_TESTING=1
    WEBVIEW_GUI_WEBVIEW_ONLY=1
)
target_link_libraries(webview_gui_wclap_init_negotiation_contract PRIVATE
    webview-gui
)

add_test(
    NAME webview_gui_wclap_init_negotiation_contract
    COMMAND webview_gui_wclap_init_negotiation_contract
)

add_executable(webview_gui_wclap_legacy_webview_proxy_contract
    wclap_legacy_webview_proxy_contract.cpp
)
target_include_directories(webview_gui_wclap_legacy_webview_proxy_contract PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${clap_SOURCE_DIR}/include"
)
target_link_libraries(webview_gui_wclap_legacy_webview_proxy_contract PRIVATE
    webview-gui
)

add_test(
    NAME webview_gui_wclap_legacy_webview_proxy_contract
    COMMAND webview_gui_wclap_legacy_webview_proxy_contract
)

add_executable(webview_gui_polysynth_wclap_proxy_contract
    polysynth_wclap_proxy_contract.cpp
)
target_include_directories(webview_gui_polysynth_wclap_proxy_contract PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${CMAKE_CURRENT_LIST_DIR}/../../examples/polysynth"
    "${CMAKE_CURRENT_LIST_DIR}/../../examples/polysynth/wclap"
    "${clap_SOURCE_DIR}/include"
)
target_compile_definitions(webview_gui_polysynth_wclap_proxy_contract PRIVATE
    WEBVIEW_GUI_TESTING=1
    WEBVIEW_GUI_WEBVIEW_ONLY=1
)
target_link_libraries(webview_gui_polysynth_wclap_proxy_contract PRIVATE
    webview-gui
)

add_test(
    NAME webview_gui_polysynth_wclap_proxy_contract
    COMMAND webview_gui_polysynth_wclap_proxy_contract
)

add_executable(webview_gui_polysynth_wclap_state_passthrough_contract
    polysynth_wclap_state_passthrough_contract.cpp
)
target_include_directories(webview_gui_polysynth_wclap_state_passthrough_contract PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${CMAKE_CURRENT_LIST_DIR}/../../examples/polysynth"
    "${CMAKE_CURRENT_LIST_DIR}/../../examples/polysynth/wclap"
    "${clap_SOURCE_DIR}/include"
)
target_compile_definitions(webview_gui_polysynth_wclap_state_passthrough_contract PRIVATE
    WEBVIEW_GUI_TESTING=1
    WEBVIEW_GUI_WEBVIEW_ONLY=1
)
target_link_libraries(webview_gui_polysynth_wclap_state_passthrough_contract PRIVATE
    webview-gui
)

add_test(
    NAME webview_gui_polysynth_wclap_state_passthrough_contract
    COMMAND webview_gui_polysynth_wclap_state_passthrough_contract
)

add_executable(webview_gui_wclap_backend_selection_contract
    wclap_backend_selection_contract.cpp
)
target_include_directories(webview_gui_wclap_backend_selection_contract PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
)
target_compile_definitions(webview_gui_wclap_backend_selection_contract PRIVATE
    __wasm__=1
)

add_test(
    NAME webview_gui_wclap_backend_selection_contract
    COMMAND webview_gui_wclap_backend_selection_contract
)
