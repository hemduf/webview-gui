cmake_minimum_required(VERSION 3.24)
project(webview_gui_plugin_safe_profile_tests LANGUAGES CXX)

include(CTest)

# Reproduce a common plug-in superbuild where shared libraries are the parent
# default. The webview-gui implementation must still remain module-private.
set(BUILD_SHARED_LIBS ON CACHE BOOL "Parent shared-library default" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/webview-gui EXCLUDE_FROM_ALL)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/WebviewGuiPluginSafe.cmake)

get_target_property(WEBVIEW_GUI_LIBRARY_TYPE webview-gui TYPE)
if(NOT WEBVIEW_GUI_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "plugin-safe webview-gui must remain STATIC, got ${WEBVIEW_GUI_LIBRARY_TYPE}")
endif()

add_library(plugin_safe_module_a MODULE plugin_safe_module.cpp)
add_library(plugin_safe_module_b MODULE plugin_safe_module.cpp)

foreach(target plugin_safe_module_a plugin_safe_module_b)
    webview_gui_configure_plugin_target(${target})
    target_compile_features(${target} PRIVATE cxx_std_17)
endforeach()

target_compile_definitions(plugin_safe_module_a PRIVATE WEBVIEW_GUI_TEST_MODULE_VARIANT=1)
target_compile_definitions(plugin_safe_module_b PRIVATE WEBVIEW_GUI_TEST_MODULE_VARIANT=2)

foreach(target plugin_safe_module_a plugin_safe_module_b)
    add_test(
        NAME ${target}_exports_only_plugin_abi
        COMMAND ${CMAKE_COMMAND}
            -DMODULE=$<TARGET_FILE:${target}>
            -DNM=${CMAKE_NM}
            -DPLATFORM=${CMAKE_SYSTEM_NAME}
            -P ${CMAKE_CURRENT_LIST_DIR}/verify_exports.cmake
    )
endforeach()
