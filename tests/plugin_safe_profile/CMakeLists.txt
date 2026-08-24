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

# Module A uses the recommended private static integration. Module B deliberately
# uses the independently compiled header-only integration so the qualification
# harness exercises two different webview-gui configurations in one process.
add_library(plugin_safe_module_a MODULE plugin_safe_module.cpp)
webview_gui_configure_plugin_target(plugin_safe_module_a)
target_compile_features(plugin_safe_module_a PRIVATE cxx_std_17)
target_compile_definitions(plugin_safe_module_a PRIVATE WEBVIEW_GUI_TEST_MODULE_VARIANT=1)

add_library(plugin_safe_module_b MODULE plugin_safe_module.cpp)
target_compile_features(plugin_safe_module_b PRIVATE cxx_std_17)
target_compile_definitions(plugin_safe_module_b PRIVATE
    WEBVIEW_GUI_HEADER_ONLY=1
    WEBVIEW_GUI_TEST_MODULE_VARIANT=2)
target_include_directories(plugin_safe_module_b PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include
    ${WEBVIEW_GUI_CHOC_INCLUDE_DIRS})
set_target_properties(plugin_safe_module_b PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(plugin_safe_module_b PRIVATE
        "-framework WebKit"
        "-framework Security")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(plugin_safe_module_b PRIVATE ole32 user32 shell32 bcrypt)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(plugin_safe_gtk3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules(plugin_safe_webkit2 REQUIRED webkit2gtk-4.1 IMPORTED_TARGET)
    target_link_libraries(plugin_safe_module_b PRIVATE
        PkgConfig::plugin_safe_gtk3
        PkgConfig::plugin_safe_webkit2)
endif()

# RED/GREEN build-contract checks: before this fixture was split, module B also
# linked the same static webview-gui target and these assertions failed.
get_target_property(MODULE_B_LINK_LIBRARIES plugin_safe_module_b LINK_LIBRARIES)
if(MODULE_B_LINK_LIBRARIES
   AND NOT MODULE_B_LINK_LIBRARIES STREQUAL "MODULE_B_LINK_LIBRARIES-NOTFOUND")
    list(FIND MODULE_B_LINK_LIBRARIES "webview-gui" MODULE_B_STATIC_LINK_INDEX)
    if(NOT MODULE_B_STATIC_LINK_INDEX EQUAL -1)
        message(FATAL_ERROR
            "plugin_safe_module_b must remain an independent header-only configuration")
    endif()
endif()

get_target_property(MODULE_B_COMPILE_DEFINITIONS plugin_safe_module_b COMPILE_DEFINITIONS)
list(FIND MODULE_B_COMPILE_DEFINITIONS "WEBVIEW_GUI_HEADER_ONLY=1" MODULE_B_HEADER_ONLY_INDEX)
if(MODULE_B_HEADER_ONLY_INDEX EQUAL -1)
    message(FATAL_ERROR
        "plugin_safe_module_b must compile webview-gui in header-only mode")
endif()

foreach(target plugin_safe_module_a plugin_safe_module_b)
    add_test(
        NAME ${target}_exports_only_plugin_abi
        COMMAND ${CMAKE_COMMAND}
            -DMODULE=$<TARGET_FILE:${target}>
            -DNM=${CMAKE_NM}
            -DOBJDUMP=${CMAKE_OBJDUMP}
            -DPLATFORM=${CMAKE_SYSTEM_NAME}
            -P ${CMAKE_CURRENT_LIST_DIR}/verify_exports.cmake
    )
endforeach()

add_executable(plugin_safe_profile_host plugin_safe_profile_host.cpp)
target_compile_features(plugin_safe_profile_host PRIVATE cxx_std_17)
if(CMAKE_DL_LIBS)
    target_link_libraries(plugin_safe_profile_host PRIVATE ${CMAKE_DL_LIBS})
endif()
add_dependencies(plugin_safe_profile_host plugin_safe_module_a plugin_safe_module_b)

add_test(
    NAME plugin_safe_static_and_header_only_modules_coexist
    COMMAND plugin_safe_profile_host
        $<TARGET_FILE:plugin_safe_module_a>
        $<TARGET_FILE:plugin_safe_module_b>)
