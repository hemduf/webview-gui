cmake_minimum_required(VERSION 3.24)

# Audio plug-ins are loaded alongside unrelated third-party modules in one host
# process. Keep this implementation private to each plug-in and never let a
# parent BUILD_SHARED_LIBS setting turn it into a process-shared runtime.
add_library(webview-gui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source/webview-gui.cpp
)

target_include_directories(webview-gui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/webview-gui/_impl/platform/choc
)

target_compile_features(webview-gui PUBLIC cxx_std_17)

set_target_properties(webview-gui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(webview-gui PRIVATE "-framework WebKit")
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    target_link_libraries(webview-gui PRIVATE ole32 user32 shell32)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(gtk3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules(webkit2 REQUIRED webkit2gtk-4.1 IMPORTED_TARGET)
    target_link_libraries(webview-gui PRIVATE PkgConfig::gtk3 PkgConfig::webkit2)
endif()
