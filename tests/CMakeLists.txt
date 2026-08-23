cmake_minimum_required(VERSION 3.24)
project(webview_gui_tests LANGUAGES CXX)

include(CTest)
include(FetchContent)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

set(CLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG a47f6badb49d948fd009998f28309cdab78979c9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(clap)

# Build the production library as part of the test graph so adapter tests catch
# compile/link regressions in the actual backend rather than a mocked copy.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/webview-gui-under-test EXCLUDE_FROM_ALL)

# Plug-in-safe integration contract: the parent project's BUILD_SHARED_LIBS must
# never turn this helper into a shared runtime, and the static archive must be
# suitable for linking into a hidden-symbol plug-in module.
get_target_property(WEBVIEW_GUI_LIBRARY_TYPE webview-gui TYPE)
if(NOT WEBVIEW_GUI_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "webview-gui must remain STATIC, got ${WEBVIEW_GUI_LIBRARY_TYPE}")
endif()

get_target_property(WEBVIEW_GUI_PIC webview-gui POSITION_INDEPENDENT_CODE)
if(NOT WEBVIEW_GUI_PIC)
    message(FATAL_ERROR "webview-gui must be built with POSITION_INDEPENDENT_CODE")
endif()

get_target_property(WEBVIEW_GUI_VISIBILITY webview-gui CXX_VISIBILITY_PRESET)
if(NOT WEBVIEW_GUI_VISIBILITY STREQUAL "hidden")
    message(FATAL_ERROR "webview-gui must use hidden C++ symbol visibility")
endif()

get_target_property(WEBVIEW_GUI_INLINE_VISIBILITY webview-gui VISIBILITY_INLINES_HIDDEN)
if(NOT WEBVIEW_GUI_INLINE_VISIBILITY)
    message(FATAL_ERROR "webview-gui must hide inline symbols")
endif()

add_executable(webview_gui_core_tests core_tests.cpp)
target_include_directories(webview_gui_core_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
target_link_libraries(webview_gui_core_tests PRIVATE doctest::doctest Threads::Threads)
add_test(NAME webview_gui_core_tests COMMAND webview_gui_core_tests)

add_executable(webview_gui_clap_tests clap_adapter_tests.cpp)
target_include_directories(webview_gui_clap_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../include
    ${clap_SOURCE_DIR}/include
)
target_compile_definitions(webview_gui_clap_tests PRIVATE WEBVIEW_GUI_TESTING=1)
target_link_libraries(webview_gui_clap_tests PRIVATE doctest::doctest webview-gui)
add_test(NAME webview_gui_clap_tests COMMAND webview_gui_clap_tests)

if(APPLE)
    add_library(webview_gui_test_module_a MODULE macos_module.cpp)
    add_library(webview_gui_test_module_b MODULE macos_module.cpp)

    foreach(target webview_gui_test_module_a webview_gui_test_module_b)
        target_include_directories(${target} PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/../include
            ${CMAKE_CURRENT_LIST_DIR}/../include/webview-gui/_impl/platform/choc
        )
        target_compile_definitions(${target} PRIVATE WEBVIEW_GUI_HEADER_ONLY=1 WEBVIEW_GUI_TESTING=1)
        target_link_libraries(${target} PRIVATE "-framework WebKit")
        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
        )
    endforeach()

    add_executable(webview_gui_macos_isolation_tests macos_isolation_tests.cpp)
    target_link_libraries(webview_gui_macos_isolation_tests PRIVATE doctest::doctest ${CMAKE_DL_LIBS} "-framework Foundation")
    target_compile_definitions(webview_gui_macos_isolation_tests PRIVATE
        MODULE_A_PATH="$<TARGET_FILE:webview_gui_test_module_a>"
        MODULE_B_PATH="$<TARGET_FILE:webview_gui_test_module_b>"
    )
    add_dependencies(webview_gui_macos_isolation_tests
        webview_gui_test_module_a
        webview_gui_test_module_b
    )
    add_test(NAME webview_gui_macos_isolation_tests COMMAND webview_gui_macos_isolation_tests)
endif()
