cmake_minimum_required(VERSION 3.24)
project(webview_gui_clap_compat_consumer LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG a47f6badb49d948fd009998f28309cdab78979c9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(clap)

# Simulate a consumer pinned to a CLAP tree from before clap.webview/3 was
# shipped. Copy the otherwise-current SDK and deliberately remove only the
# draft header so all unrelated CLAP declarations remain realistic.
set(legacy_clap_include "${CMAKE_CURRENT_BINARY_DIR}/legacy-clap-include")
file(REMOVE_RECURSE "${legacy_clap_include}")
file(MAKE_DIRECTORY "${legacy_clap_include}")
file(COPY "${clap_SOURCE_DIR}/include/clap"
    DESTINATION "${legacy_clap_include}"
    PATTERN "webview.h" EXCLUDE
)

add_executable(webview_gui_clap_legacy_compile legacy_compile.cpp)
target_include_directories(webview_gui_clap_legacy_compile PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include
    ${legacy_clap_include}
)

# Also compile once with the official current SDK first on the include path.
# This protects the canonical-header path from accidental fallback-only drift.
add_executable(webview_gui_clap_current_compile current_compile.cpp)
target_include_directories(webview_gui_clap_current_compile PRIVATE
    ${clap_SOURCE_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../../include
)
