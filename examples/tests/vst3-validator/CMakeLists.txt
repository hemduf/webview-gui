cmake_minimum_required(VERSION 3.25)

project(webview_gui_vst3_validator LANGUAGES C CXX)

if(NOT DEFINED VST3_SDK_ROOT OR VST3_SDK_ROOT STREQUAL "")
    message(FATAL_ERROR "VST3_SDK_ROOT is required")
endif()
get_filename_component(VST3_SDK_ROOT "${VST3_SDK_ROOT}" ABSOLUTE)
if(NOT EXISTS "${VST3_SDK_ROOT}/cmake/modules/SMTG_VST3_SDK.cmake")
    message(FATAL_ERROR "VST3_SDK_ROOT is not a complete Steinberg VST3 SDK: ${VST3_SDK_ROOT}")
endif()

# Build only the SDK libraries required by Steinberg's validator. Configuring the
# pinned SDK top-level CMakeLists also adds every hosting example unconditionally;
# on Linux that drags editorhost + gtkmm into a validator-only CI job.
list(APPEND CMAKE_MODULE_PATH "${VST3_SDK_ROOT}/cmake/modules")
include(SMTG_VST3_SDK)

smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()

set(ROOT "${VST3_SDK_ROOT}")
set(SDK_ROOT "${VST3_SDK_ROOT}")
set(public_sdk_SOURCE_DIR "${SDK_ROOT}/public.sdk")
set(pluginterfaces_SOURCE_DIR "${SDK_ROOT}/pluginterfaces")
set(SDK_IDE_LIBS_FOLDER FOLDER "Libraries")
set(SDK_IDE_HOSTING_EXAMPLES_FOLDER FOLDER "Hosting-Examples")
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "Run validator self-test after build" FORCE)

include_directories("${ROOT}" "${SDK_ROOT}")

smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
smtg_create_public_sdk_hosting_target()

add_subdirectory(
    "${SDK_ROOT}/public.sdk/samples/vst-hosting/validator"
    "${CMAKE_CURRENT_BINARY_DIR}/validator")
