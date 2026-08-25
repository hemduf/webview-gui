cmake_minimum_required(VERSION 3.24)
project(webview_gui_tests LANGUAGES CXX)

include(CTest)
include(FetchContent)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WEBVIEW_GUI_ENABLE_TSAN_TESTS "Build the callback-registry ThreadSanitizer qualification target" OFF)

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

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/webview-gui-under-test EXCLUDE_FROM_ALL)

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

add_executable(webview_gui_realtime_handoff_tests realtime_handoff_tests.cpp)
target_include_directories(webview_gui_realtime_handoff_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
target_link_libraries(webview_gui_realtime_handoff_tests PRIVATE doctest::doctest Threads::Threads)
add_test(NAME webview_gui_realtime_handoff_tests COMMAND webview_gui_realtime_handoff_tests)

add_executable(webview_gui_bridge_security_tests bridge_security_tests.cpp)
target_include_directories(webview_gui_bridge_security_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
target_link_libraries(webview_gui_bridge_security_tests PRIVATE doctest::doctest webview-gui)
add_test(NAME webview_gui_bridge_security_tests COMMAND webview_gui_bridge_security_tests)

add_executable(webview_gui_clap_tests clap_adapter_tests.cpp)
target_include_directories(webview_gui_clap_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../include
    ${clap_SOURCE_DIR}/include
)
target_compile_definitions(webview_gui_clap_tests PRIVATE WEBVIEW_GUI_TESTING=1)
target_link_libraries(webview_gui_clap_tests PRIVATE doctest::doctest webview-gui)
add_test(NAME webview_gui_clap_tests COMMAND webview_gui_clap_tests)

if(WEBVIEW_GUI_ENABLE_TSAN_TESTS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "WEBVIEW_GUI_ENABLE_TSAN_TESTS is currently supported only on Linux")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "WEBVIEW_GUI_ENABLE_TSAN_TESTS requires GCC or Clang")
    endif()

    add_executable(webview_gui_callback_registry_tsan_tests callback_registry_tsan_tests.cpp)
    target_include_directories(webview_gui_callback_registry_tsan_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include
    )
    target_compile_options(webview_gui_callback_registry_tsan_tests PRIVATE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(webview_gui_callback_registry_tsan_tests PRIVATE -fsanitize=thread)
    target_link_libraries(webview_gui_callback_registry_tsan_tests PRIVATE
        doctest::doctest
        Threads::Threads
    )
    add_test(NAME webview_gui_callback_registry_tsan_tests COMMAND webview_gui_callback_registry_tsan_tests)
endif()

if(APPLE)
    add_library(webview_gui_test_module_a MODULE macos_module.cpp)
    add_library(webview_gui_test_module_b MODULE macos_module.cpp)

    foreach(target webview_gui_test_module_a webview_gui_test_module_b)
        target_include_directories(${target} PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/../include
            ${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}
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

    add_executable(webview_gui_macos_bridge_roundtrip_tests macos_bridge_roundtrip_tests.cpp)
    target_include_directories(webview_gui_macos_bridge_roundtrip_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include
    )
    target_link_libraries(webview_gui_macos_bridge_roundtrip_tests PRIVATE
        doctest::doctest
        webview-gui
        "-framework WebKit"
        "-framework CoreFoundation"
    )
    add_test(NAME webview_gui_macos_bridge_roundtrip_tests COMMAND webview_gui_macos_bridge_roundtrip_tests)
endif()

if(WIN32)
    add_library(webview_gui_windows_module_a MODULE windows_module.cpp)
    add_library(webview_gui_windows_module_b MODULE windows_module.cpp)

    foreach(target webview_gui_windows_module_a webview_gui_windows_module_b)
        target_include_directories(${target} PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/../include
            ${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}
        )
        target_compile_definitions(${target} PRIVATE WEBVIEW_GUI_HEADER_ONLY=1 WEBVIEW_GUI_TESTING=1)
        target_link_libraries(${target} PRIVATE ole32 user32 shell32)
        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
        )
    endforeach()

    add_executable(webview_gui_windows_isolation_tests windows_isolation_tests.cpp)
    target_include_directories(webview_gui_windows_isolation_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include
    )
    target_link_libraries(webview_gui_windows_isolation_tests PRIVATE
        doctest::doctest
        webview-gui
        Threads::Threads
        ole32
        user32
    )
    target_compile_definitions(webview_gui_windows_isolation_tests PRIVATE
        MODULE_A_PATH="$<TARGET_FILE:webview_gui_windows_module_a>"
        MODULE_B_PATH="$<TARGET_FILE:webview_gui_windows_module_b>"
    )
    add_dependencies(webview_gui_windows_isolation_tests
        webview_gui_windows_module_a
        webview_gui_windows_module_b
    )
    add_test(NAME webview_gui_windows_isolation_tests COMMAND webview_gui_windows_isolation_tests)

    add_executable(webview_gui_windows_navigation_tests windows_navigation_tests.cpp)
    target_include_directories(webview_gui_windows_navigation_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include
    )
    target_link_libraries(webview_gui_windows_navigation_tests PRIVATE
        doctest::doctest
        ole32
        user32
        shell32
    )
    add_test(NAME webview_gui_windows_navigation_tests COMMAND webview_gui_windows_navigation_tests)
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(test_gtk3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules(test_webkit2 REQUIRED webkit2gtk-4.1 IMPORTED_TARGET)

    add_executable(webview_gui_linux_runtime_baseline_tests linux_runtime_baseline_tests.cpp)
    target_link_libraries(webview_gui_linux_runtime_baseline_tests PRIVATE
        doctest::doctest
        Threads::Threads
        PkgConfig::test_gtk3
        PkgConfig::test_webkit2
    )
    add_test(NAME webview_gui_linux_runtime_baseline_tests COMMAND webview_gui_linux_runtime_baseline_tests)

    add_executable(webview_gui_linux_embed_tests linux_embed_tests.cpp)
    target_include_directories(webview_gui_linux_embed_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include
        ${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}
    )
    target_link_libraries(webview_gui_linux_embed_tests PRIVATE
        doctest::doctest
        webview-gui
        Threads::Threads
        PkgConfig::test_gtk3
        PkgConfig::test_webkit2
    )
    add_test(NAME webview_gui_linux_embed_tests COMMAND webview_gui_linux_embed_tests)
endif()
