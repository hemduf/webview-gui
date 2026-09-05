cmake_minimum_required(VERSION 3.24)
project(webview_gui_preset_portability_contract LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBVIEW_GUI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(WEBVIEW_GUI_PRESET_INCLUDE_DIR "${WEBVIEW_GUI_ROOT}/examples/common/presets")
set(WEBVIEW_GUI_BUNDLED_PRESET_ROOT "${WEBVIEW_GUI_PRESET_INCLUDE_DIR}/bundled")
include("${WEBVIEW_GUI_ROOT}/cmake/WebviewGuiChocDependency.cmake")
webview_gui_resolve_choc_dependency()
if(NOT WEBVIEW_GUI_CHOC_INCLUDE_DIRS)
    message(FATAL_ERROR "Preset portability tests could not resolve the repository-pinned CHOC dependency")
endif()

# Regression for #104 review corrections: production calls may package a target
# created by an examples subdirectory (PolySynth). CMake rejects a parent-level
# add_custom_command(TARGET ... POST_BUILD) for such a target, so configure a
# minimal child target through the real PresetResources helper here. Configure
# itself is the contract; this failed deterministically before the correction.
set(WEBVIEW_GUI_SOURCE_DIR "${WEBVIEW_GUI_ROOT}")
include("${WEBVIEW_GUI_ROOT}/examples/cmake/PresetResources.cmake")
add_subdirectory(cmake_cross_directory)
webview_gui_package_native_clap_preset_resources(
    webview_gui_preset_cross_directory_dummy
    PresetCrossDirectoryDummy)

# PresetResources resolves the same immutable CHOC dependency used by the root
# build. Focused contracts intentionally do not rely on a removed source-tree
# submodule path.
if(NOT WEBVIEW_GUI_CHOC_INCLUDE_DIRS)
    message(FATAL_ERROR "preset portability contract did not resolve CHOC")
endif()

add_executable(webview_gui_preset_portability_contract
    preset_storage_contract_tests.cpp
)
target_include_directories(webview_gui_preset_portability_contract PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}
)
target_compile_features(webview_gui_preset_portability_contract PRIVATE cxx_std_17)

add_executable(webview_gui_preset_factory_bundle_contract
    factory_bundle_contract_tests.cpp
)
target_include_directories(webview_gui_preset_factory_bundle_contract PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}
)
target_compile_definitions(webview_gui_preset_factory_bundle_contract PRIVATE
    WEBVIEW_GUI_BUNDLED_PRESET_ROOT="${WEBVIEW_GUI_BUNDLED_PRESET_ROOT}")
target_compile_features(webview_gui_preset_factory_bundle_contract PRIVATE cxx_std_17)

find_package(Threads REQUIRED)
add_executable(webview_gui_native_preset_adapter_contract
    native_storage_adapter_tests.cpp
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}/native_preset_storage.cpp
)
target_include_directories(webview_gui_native_preset_adapter_contract PRIVATE
    ${WEBVIEW_GUI_PRESET_INCLUDE_DIR}
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}
)
target_link_libraries(webview_gui_native_preset_adapter_contract PRIVATE Threads::Threads)
target_compile_features(webview_gui_native_preset_adapter_contract PRIVATE cxx_std_17)

enable_testing()
add_test(NAME webview_gui_preset_portability_contract
    COMMAND webview_gui_preset_portability_contract)
add_test(NAME webview_gui_preset_factory_bundle_contract
    COMMAND webview_gui_preset_factory_bundle_contract)
add_test(NAME webview_gui_native_preset_adapter_contract
    COMMAND webview_gui_native_preset_adapter_contract)
