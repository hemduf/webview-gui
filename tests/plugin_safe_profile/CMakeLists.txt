cmake_minimum_required(VERSION 3.24)
project(webview_gui_plugin_safe_profile_tests LANGUAGES CXX)

include(CTest)
include(FetchContent)
find_package(Git REQUIRED)

# Reproduce a common plug-in superbuild where shared libraries are the parent
# default. The webview-gui implementation must still remain module-private.
set(BUILD_SHARED_LIBS ON CACHE BOOL "Parent shared-library default" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/webview-gui EXCLUDE_FROM_ALL)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/WebviewGuiPluginSafe.cmake)

get_target_property(WEBVIEW_GUI_LIBRARY_TYPE webview-gui TYPE)
if(NOT WEBVIEW_GUI_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "plugin-safe webview-gui must remain STATIC, got ${WEBVIEW_GUI_LIBRARY_TYPE}")
endif()

function(webview_gui_read_git_revision output_variable repository_dir)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${repository_dir}" rev-parse HEAD
        RESULT_VARIABLE revision_result
        OUTPUT_VARIABLE revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    string(LENGTH "${revision}" revision_length)
    if(NOT revision_result EQUAL 0
       OR NOT revision_length EQUAL 40
       OR NOT revision MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR "unable to resolve pinned revision for ${repository_dir}")
    endif()
    set(${output_variable} "${revision}" PARENT_SCOPE)
endfunction()

set(WEBVIEW_GUI_CURRENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(WEBVIEW_GUI_CURRENT_CHOC_ROOT
    "${WEBVIEW_GUI_CURRENT_ROOT}/include/webview-gui/_impl/platform/choc")
webview_gui_read_git_revision(WEBVIEW_GUI_CURRENT_REVISION "${WEBVIEW_GUI_CURRENT_ROOT}")
webview_gui_read_git_revision(WEBVIEW_GUI_CURRENT_CHOC_REVISION "${WEBVIEW_GUI_CURRENT_CHOC_ROOT}")

# Pin a second, historical source tree rather than merely compiling the current
# checkout with different flags. The exact webview-gui and CHOC SHAs are part of
# the qualification contract so future changes cannot silently weaken the test.
set(WEBVIEW_GUI_LEGACY_REVISION "10fe374c7cac1718de83c3ec91625c0d47ce84d6")
set(WEBVIEW_GUI_LEGACY_CHOC_REVISION "907766d3cbea079d6ff17ab34c13f9c88a0f4ec3")
FetchContent_Declare(
    plugin_safe_legacy_webview_gui
    GIT_REPOSITORY https://github.com/hemduf/webview-gui.git
    GIT_TAG ${WEBVIEW_GUI_LEGACY_REVISION}
    GIT_SHALLOW FALSE
    GIT_SUBMODULES_RECURSE TRUE
    # Populate only. Adding the historical project would collide with the
    # current webview-gui target; module B consumes its headers directly.
    SOURCE_SUBDIR __plugin_safe_fixture_do_not_add_subdirectory__)
FetchContent_MakeAvailable(plugin_safe_legacy_webview_gui)

set(WEBVIEW_GUI_LEGACY_ROOT "${plugin_safe_legacy_webview_gui_SOURCE_DIR}")
set(WEBVIEW_GUI_LEGACY_CHOC_ROOT
    "${WEBVIEW_GUI_LEGACY_ROOT}/include/webview-gui/_impl/platform/choc")
webview_gui_read_git_revision(WEBVIEW_GUI_LEGACY_ACTUAL_REVISION "${WEBVIEW_GUI_LEGACY_ROOT}")
webview_gui_read_git_revision(WEBVIEW_GUI_LEGACY_ACTUAL_CHOC_REVISION "${WEBVIEW_GUI_LEGACY_CHOC_ROOT}")

if(NOT WEBVIEW_GUI_LEGACY_ACTUAL_REVISION STREQUAL WEBVIEW_GUI_LEGACY_REVISION)
    message(FATAL_ERROR
        "historical webview-gui fixture drifted: expected ${WEBVIEW_GUI_LEGACY_REVISION}, got ${WEBVIEW_GUI_LEGACY_ACTUAL_REVISION}")
endif()
if(NOT WEBVIEW_GUI_LEGACY_ACTUAL_CHOC_REVISION STREQUAL WEBVIEW_GUI_LEGACY_CHOC_REVISION)
    message(FATAL_ERROR
        "historical CHOC fixture drifted: expected ${WEBVIEW_GUI_LEGACY_CHOC_REVISION}, got ${WEBVIEW_GUI_LEGACY_ACTUAL_CHOC_REVISION}")
endif()
if(WEBVIEW_GUI_CURRENT_REVISION STREQUAL WEBVIEW_GUI_LEGACY_ACTUAL_REVISION
   OR WEBVIEW_GUI_CURRENT_CHOC_REVISION STREQUAL WEBVIEW_GUI_LEGACY_ACTUAL_CHOC_REVISION)
    message(FATAL_ERROR
        "plugin isolation fixture requires distinct current and historical webview-gui/CHOC revisions")
endif()

# Module A uses the current recommended private static integration. Module B uses
# the historical revision's header-only integration. Both remain RT-host-private;
# the fixture never links one module against the other's webview implementation.
add_library(plugin_safe_module_a MODULE plugin_safe_module.cpp)
webview_gui_configure_plugin_target(plugin_safe_module_a)
target_compile_features(plugin_safe_module_a PRIVATE cxx_std_17)
target_compile_definitions(plugin_safe_module_a PRIVATE
    WEBVIEW_GUI_TEST_MODULE_VARIANT=1
    WEBVIEW_GUI_TEST_WEBVIEW_REVISION=\"${WEBVIEW_GUI_CURRENT_REVISION}\"
    WEBVIEW_GUI_TEST_CHOC_REVISION=\"${WEBVIEW_GUI_CURRENT_CHOC_REVISION}\")

add_library(plugin_safe_module_b MODULE plugin_safe_module.cpp)
target_compile_features(plugin_safe_module_b PRIVATE cxx_std_17)
target_compile_definitions(plugin_safe_module_b PRIVATE
    WEBVIEW_GUI_HEADER_ONLY=1
    WEBVIEW_GUI_TEST_MODULE_VARIANT=2
    WEBVIEW_GUI_TEST_WEBVIEW_REVISION=\"${WEBVIEW_GUI_LEGACY_ACTUAL_REVISION}\"
    WEBVIEW_GUI_TEST_CHOC_REVISION=\"${WEBVIEW_GUI_LEGACY_ACTUAL_CHOC_REVISION}\")
target_include_directories(plugin_safe_module_b PRIVATE
    ${WEBVIEW_GUI_LEGACY_ROOT}/include
    ${WEBVIEW_GUI_LEGACY_CHOC_ROOT})
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

get_target_property(MODULE_B_LINK_LIBRARIES plugin_safe_module_b LINK_LIBRARIES)
if(MODULE_B_LINK_LIBRARIES
   AND NOT MODULE_B_LINK_LIBRARIES STREQUAL "MODULE_B_LINK_LIBRARIES-NOTFOUND")
    list(FIND MODULE_B_LINK_LIBRARIES "webview-gui" MODULE_B_STATIC_LINK_INDEX)
    if(NOT MODULE_B_STATIC_LINK_INDEX EQUAL -1)
        message(FATAL_ERROR
            "plugin_safe_module_b must remain an independent header-only configuration")
    endif()
endif()

get_target_property(MODULE_B_INCLUDE_DIRECTORIES plugin_safe_module_b INCLUDE_DIRECTORIES)
list(FIND MODULE_B_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include" MODULE_B_CURRENT_INCLUDE_INDEX)
if(NOT MODULE_B_CURRENT_INCLUDE_INDEX EQUAL -1)
    message(FATAL_ERROR
        "plugin_safe_module_b must not consume the current webview-gui include tree")
endif()

get_target_property(MODULE_B_COMPILE_DEFINITIONS plugin_safe_module_b COMPILE_DEFINITIONS)
list(FIND MODULE_B_COMPILE_DEFINITIONS "WEBVIEW_GUI_HEADER_ONLY=1" MODULE_B_HEADER_ONLY_INDEX)
if(MODULE_B_HEADER_ONLY_INDEX EQUAL -1)
    message(FATAL_ERROR
        "plugin_safe_module_b must compile webview-gui in header-only mode")
endif()

# Negative-control fixture: an unrelated exported helper must be rejected by
# the same ABI scan used for real plug-in modules. The test is expected to fail
# inside verify_exports.cmake; if the verifier accidentally allows the helper,
# CTest reports this negative control as a failure.
add_library(plugin_safe_leaky_module MODULE plugin_safe_leaky_module.cpp)
target_compile_features(plugin_safe_leaky_module PRIVATE cxx_std_17)
set_target_properties(plugin_safe_leaky_module PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

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

add_test(
    NAME plugin_safe_export_scan_rejects_unrelated_exports
    COMMAND ${CMAKE_COMMAND}
        -DMODULE=$<TARGET_FILE:plugin_safe_leaky_module>
        -DNM=${CMAKE_NM}
        -DOBJDUMP=${CMAKE_OBJDUMP}
        -DPLATFORM=${CMAKE_SYSTEM_NAME}
        -P ${CMAKE_CURRENT_LIST_DIR}/verify_exports.cmake)
set_tests_properties(plugin_safe_export_scan_rejects_unrelated_exports PROPERTIES
    WILL_FAIL TRUE)

add_executable(plugin_safe_profile_host plugin_safe_profile_host.cpp)
target_compile_features(plugin_safe_profile_host PRIVATE cxx_std_17)
if(CMAKE_DL_LIBS)
    target_link_libraries(plugin_safe_profile_host PRIVATE ${CMAKE_DL_LIBS})
endif()
add_dependencies(plugin_safe_profile_host plugin_safe_module_a plugin_safe_module_b)

add_test(
    NAME plugin_safe_distinct_revision_modules_coexist
    COMMAND plugin_safe_profile_host
        $<TARGET_FILE:plugin_safe_module_a>
        $<TARGET_FILE:plugin_safe_module_b>)
