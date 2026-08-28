cmake_minimum_required(VERSION 3.24)
project(webview_gui_standalone_header_only_consumer LANGUAGES CXX)

if(NOT DEFINED WEBVIEW_GUI_SOURCE_DIR)
    message(FATAL_ERROR "WEBVIEW_GUI_SOURCE_DIR is required")
endif()

add_library(webview_gui_standalone_header_only_consumer OBJECT main.cpp)
target_compile_features(webview_gui_standalone_header_only_consumer PRIVATE cxx_std_17)
target_compile_definitions(webview_gui_standalone_header_only_consumer PRIVATE WEBVIEW_GUI_HEADER_ONLY=1)
target_include_directories(webview_gui_standalone_header_only_consumer PRIVATE
    "${WEBVIEW_GUI_SOURCE_DIR}/include"
)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(gtk3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules(webkit2 REQUIRED webkit2gtk-4.1 IMPORTED_TARGET)
    target_link_libraries(webview_gui_standalone_header_only_consumer PRIVATE
        PkgConfig::gtk3
        PkgConfig::webkit2
    )
endif()
