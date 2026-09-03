cmake_minimum_required(VERSION 3.24)
project(webview_gui_preset_contract_tests LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(webview_gui_preset_contract_clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG a47f6badb49d948fd009998f28309cdab78979c9
)
FetchContent_MakeAvailable(webview_gui_preset_contract_clap)

add_executable(webview_gui_preset_contract_tests
    preset_contract_tests.cpp
)
target_include_directories(webview_gui_preset_contract_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common
)
target_link_libraries(webview_gui_preset_contract_tests PRIVATE clap)
target_compile_features(webview_gui_preset_contract_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_examples_preset_contract
    COMMAND webview_gui_preset_contract_tests)
