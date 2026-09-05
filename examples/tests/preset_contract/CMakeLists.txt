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

set(WEBVIEW_GUI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
include("${WEBVIEW_GUI_ROOT}/cmake/WebviewGuiChocDependency.cmake")
webview_gui_resolve_choc_dependency()
if(NOT WEBVIEW_GUI_CHOC_INCLUDE_DIRS)
    message(FATAL_ERROR "Preset contract tests could not resolve the repository-pinned CHOC dependency")
endif()
set(WEBVIEW_GUI_PRESET_TEST_CHOC_DIR "${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}")

add_library(webview_gui_preset_contract_native_support STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../../common/preset_production_catalog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../common/presets/native_preset_storage.cpp
)
target_include_directories(webview_gui_preset_contract_native_support PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../common
    ${CMAKE_CURRENT_LIST_DIR}/../../common/presets
    ${WEBVIEW_GUI_PRESET_TEST_CHOC_DIR}
)
target_link_libraries(webview_gui_preset_contract_native_support PUBLIC clap)
target_compile_features(webview_gui_preset_contract_native_support PUBLIC cxx_std_17)
if(WIN32)
    target_compile_definitions(webview_gui_preset_contract_native_support PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(webview_gui_preset_contract_native_support PRIVATE shell32 ole32)
endif()

add_executable(webview_gui_preset_contract_tests
    preset_contract_tests.cpp
)
target_include_directories(webview_gui_preset_contract_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common
)
target_link_libraries(webview_gui_preset_contract_tests PRIVATE clap)
target_compile_features(webview_gui_preset_contract_tests PRIVATE cxx_std_17)

find_package(Threads REQUIRED)
add_executable(webview_gui_preset_discovery_factory_tests
    preset_discovery_factory_tests.cpp
)
target_include_directories(webview_gui_preset_discovery_factory_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common
    ${CMAKE_CURRENT_LIST_DIR}/../..
    ${WEBVIEW_GUI_PRESET_TEST_CHOC_DIR}
)
target_link_libraries(webview_gui_preset_discovery_factory_tests PRIVATE
    clap
    Threads::Threads
    webview_gui_preset_contract_native_support
)
target_compile_features(webview_gui_preset_discovery_factory_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_metadata_tests
    preset_metadata_tests.cpp
)
target_include_directories(webview_gui_preset_metadata_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common
    ${CMAKE_CURRENT_LIST_DIR}/../..
    ${WEBVIEW_GUI_PRESET_TEST_CHOC_DIR}
)
target_link_libraries(webview_gui_preset_metadata_tests PRIVATE
    clap
    Threads::Threads
    webview_gui_preset_contract_native_support
)
target_compile_features(webview_gui_preset_metadata_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_metadata_review_tests
    preset_metadata_review_tests.cpp
)
target_include_directories(webview_gui_preset_metadata_review_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common
    ${CMAKE_CURRENT_LIST_DIR}/../..
    ${WEBVIEW_GUI_PRESET_TEST_CHOC_DIR}
)
target_link_libraries(webview_gui_preset_metadata_review_tests PRIVATE
    clap
    Threads::Threads
    webview_gui_preset_contract_native_support
)
target_compile_features(webview_gui_preset_metadata_review_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_load_tests
    preset_load_tests.cpp
)
target_include_directories(webview_gui_preset_load_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common
    ${CMAKE_CURRENT_LIST_DIR}/../..
    ${WEBVIEW_GUI_PRESET_TEST_CHOC_DIR}
)
target_link_libraries(webview_gui_preset_load_tests PRIVATE
    clap
    Threads::Threads
    webview_gui_preset_contract_native_support
)
target_compile_features(webview_gui_preset_load_tests PRIVATE cxx_std_17)

# #38 browser state is deliberately a pure main/UI-thread model. Keeping this
# target independent from storage/WebView/processor code makes persistent-vs-
# transient dirty semantics deterministic and prevents accidental RT coupling.
add_executable(webview_gui_preset_browser_state_tests
    preset_browser_state_tests.cpp
)
target_include_directories(webview_gui_preset_browser_state_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common
)
target_link_libraries(webview_gui_preset_browser_state_tests PRIVATE clap)
target_compile_features(webview_gui_preset_browser_state_tests PRIVATE cxx_std_17)

add_executable(webview_gui_preset_browser_controller_tests
    preset_browser_controller_tests.cpp
)
target_include_directories(webview_gui_preset_browser_controller_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../common
    ${CMAKE_CURRENT_LIST_DIR}/../..
    ${WEBVIEW_GUI_PRESET_TEST_CHOC_DIR}
)
target_link_libraries(webview_gui_preset_browser_controller_tests PRIVATE
    clap
    webview_gui_preset_contract_native_support
)
target_compile_features(webview_gui_preset_browser_controller_tests PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_examples_preset_contract
    COMMAND webview_gui_preset_contract_tests)
add_test(NAME webview_gui_examples_preset_discovery_factory
    COMMAND webview_gui_preset_discovery_factory_tests)
add_test(NAME webview_gui_examples_preset_metadata
    COMMAND webview_gui_preset_metadata_tests)
add_test(NAME webview_gui_examples_preset_metadata_review
    COMMAND webview_gui_preset_metadata_review_tests)
add_test(NAME webview_gui_examples_preset_load
    COMMAND webview_gui_preset_load_tests)
add_test(NAME webview_gui_examples_preset_browser_state
    COMMAND webview_gui_preset_browser_state_tests)
add_test(NAME webview_gui_examples_preset_browser_controller
    COMMAND webview_gui_preset_browser_controller_tests)
add_test(NAME webview_gui_examples_preset_plugin_load_contract
    COMMAND ${CMAKE_COMMAND}
        -DWEBVIEW_GUI_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../../..
        -P ${CMAKE_CURRENT_LIST_DIR}/preset_plugin_load_contract.cmake)
add_test(NAME webview_gui_examples_preset_validator_stage_contract
    COMMAND ${CMAKE_COMMAND}
        -DWEBVIEW_GUI_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../../..
        -P ${CMAKE_CURRENT_LIST_DIR}/preset_validator_stage_contract.cmake)
add_test(NAME webview_gui_examples_preset_wrapper_runtime_contract
    COMMAND ${CMAKE_COMMAND}
        -DWEBVIEW_GUI_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../../..
        -P ${CMAKE_CURRENT_LIST_DIR}/preset_wrapper_runtime_contract.cmake)
add_test(NAME webview_gui_examples_preset_entry_factory_contract
    COMMAND ${CMAKE_COMMAND}
        -DWEBVIEW_GUI_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../../..
        -P ${CMAKE_CURRENT_LIST_DIR}/preset_entry_factory_contract.cmake)
add_test(NAME webview_gui_examples_preset_native_ci_contract
    COMMAND ${CMAKE_COMMAND}
        -DWEBVIEW_GUI_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../../..
        -P ${CMAKE_CURRENT_LIST_DIR}/preset_native_ci_contract.cmake)
add_test(NAME webview_gui_examples_preset_final_qualification_contract
    COMMAND ${CMAKE_COMMAND}
        -DWEBVIEW_GUI_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../../..
        -P ${CMAKE_CURRENT_LIST_DIR}/preset_final_qualification_contract.cmake)
