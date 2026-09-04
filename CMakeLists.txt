cmake_minimum_required(VERSION 3.24)

# Audio plug-ins are loaded alongside unrelated third-party modules in one host
# process. Keep this implementation private to each plug-in and never let a
# parent BUILD_SHARED_LIBS setting turn it into a process-shared runtime.
add_library(webview-gui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source/webview-gui.cpp
)

include(FetchContent)

# CPM is only the dependency bootstrap. CHOC itself is fetched exclusively from
# the maintained hemduf fork below and pinned to an immutable commit.
FetchContent_Declare(
    cpm_cmake
    GIT_REPOSITORY https://github.com/cpm-cmake/CPM.cmake.git
    GIT_TAG v0.40.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cpm_cmake)
include("${cpm_cmake_SOURCE_DIR}/cmake/CPM.cmake")

CPMAddPackage(
    NAME choc
    GITHUB_REPOSITORY hemduf/choc
    GIT_TAG 3e815bc19e37824fa9dc6a63c8955a36fa2449ae
    GIT_SHALLOW FALSE
    DOWNLOAD_ONLY YES
)

if(NOT choc_SOURCE_DIR)
    message(FATAL_ERROR "CPM failed to provide the pinned hemduf/choc source tree")
endif()

set(WEBVIEW_GUI_CHOC_INCLUDE_DIRS "${choc_SOURCE_DIR}")

# Tests that compile the supported header-only plug-in mode need the exact same
# private CHOC include order as the library target.
set(WEBVIEW_GUI_CHOC_INCLUDE_DIRS "${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}"
    CACHE INTERNAL "webview-gui private CHOC include directories" FORCE)

target_include_directories(webview-gui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}
)

target_compile_features(webview-gui PUBLIC cxx_std_17)

set_target_properties(webview-gui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(webview-gui PRIVATE
        "-framework WebKit"
        "-framework Security"
    )
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    target_link_libraries(webview-gui PRIVATE ole32 user32 shell32 bcrypt)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(gtk3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules(webkit2 REQUIRED webkit2gtk-4.1 IMPORTED_TARGET)
    target_link_libraries(webview-gui PRIVATE PkgConfig::gtk3 PkgConfig::webkit2)
endif()

# Example dependencies and plug-in targets are strictly opt-in so normal
# consumers keep the existing library-only configure graph unchanged.
option(WEBVIEW_GUI_BUILD_EXAMPLES "Build the CLAP example targets" OFF)
if(WEBVIEW_GUI_BUILD_EXAMPLES)
    include(CTest)
    # Enabling examples is explicit, so keep their targets in the parent IDE
    # project as well. EXCLUDE_FROM_ALL would remove them from Visual Studio's
    # generated solution and make `cmake --build --target` fail on Windows.
    add_subdirectory(examples)
endif()
