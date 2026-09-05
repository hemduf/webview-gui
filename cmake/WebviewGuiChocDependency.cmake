include_guard(GLOBAL)

function(webview_gui_resolve_choc_dependency)
    if(DEFINED WEBVIEW_GUI_CHOC_INCLUDE_DIRS
       AND NOT "${WEBVIEW_GUI_CHOC_INCLUDE_DIRS}" STREQUAL "")
        return()
    endif()

    include(FetchContent)

    # CPM is only the dependency bootstrap. Pin the bootstrap itself to the
    # immutable commit behind v0.40.2, then pin the maintained CHOC fork by SHA.
    FetchContent_Declare(
        cpm_cmake
        GIT_REPOSITORY https://github.com/cpm-cmake/CPM.cmake.git
        GIT_TAG 0bc73f41cedb561efe5643826891dcb705c680de
        GIT_SHALLOW FALSE
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

    # Make the resolved path visible to sibling example/test directories while
    # keeping it an implementation detail of this build tree.
    set(WEBVIEW_GUI_CHOC_INCLUDE_DIRS "${choc_SOURCE_DIR}"
        CACHE INTERNAL "webview-gui private CHOC include directories" FORCE)
endfunction()
