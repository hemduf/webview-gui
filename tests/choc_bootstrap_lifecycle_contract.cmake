cmake_minimum_required(VERSION 3.24)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
include("${REPO_ROOT}/cmake/WebviewGuiChocBootstrapLifecyclePatch.cmake")

# Representative snippets from the pinned CHOC Linux, macOS and Windows
# resource-backed startup paths. The plugin profile must defer CHOC's implicit
# navigation so webview-gui can register document-start security scripts first.
set(CHOC_SOURCE [=[
        if (options.fetchResource)
        {
            webkit_web_context_register_uri_scheme (webviewContext, getURIScheme (options).c_str(), onResourceRequested, this, nullptr);
            navigate ({});
        }

        if (options->fetchResource)
            navigate ({});

        if (options.fetchResource)
            navigate ({});
]=])

webview_gui_disable_choc_automatic_resource_navigation(CHOC_SOURCE)

string(FIND "${CHOC_SOURCE}" "navigate ({});" REMAINING_NAVIGATION)
if(NOT REMAINING_NAVIGATION EQUAL -1)
    message(FATAL_ERROR "CHOC automatic resource navigation was not fully deferred")
endif()

string(REGEX MATCHALL "WEBVIEW_GUI_CHOC_BOOTSTRAP_NAVIGATION_DEFERRED" PATCH_MARKERS "${CHOC_SOURCE}")
list(LENGTH PATCH_MARKERS PATCH_MARKER_COUNT)
if(NOT PATCH_MARKER_COUNT EQUAL 3)
    message(FATAL_ERROR "Expected all three CHOC platform startup paths to be patched")
endif()

file(READ "${REPO_ROOT}/include/webview-gui/_impl/platform/choc.h" WRAPPER_SOURCE)
string(FIND "${WRAPPER_SOURCE}" "wv.addInitScript(bridgeBootstrap)" BOOTSTRAP_POSITION)
string(FIND "${WRAPPER_SOURCE}" "wv.navigate(startUri)" NAVIGATE_POSITION)
if(BOOTSTRAP_POSITION EQUAL -1 OR NAVIGATE_POSITION EQUAL -1 OR NOT BOOTSTRAP_POSITION LESS NAVIGATE_POSITION)
    message(FATAL_ERROR "webview-gui must install the bridge bootstrap before explicit navigation")
endif()

file(READ "${REPO_ROOT}/CMakeLists.txt" ROOT_CMAKE)
file(READ "${REPO_ROOT}/cmake/WebviewGuiChocLinuxPatch.cmake" LINUX_PATCH)
file(READ "${REPO_ROOT}/cmake/WebviewGuiChocMacOSPatch.cmake" MACOS_PATCH)

foreach(PATCH_SOURCE IN ITEMS ROOT_CMAKE LINUX_PATCH MACOS_PATCH)
    string(FIND "${${PATCH_SOURCE}}" "webview_gui_disable_choc_automatic_resource_navigation" WIRED_POSITION)
    if(WIRED_POSITION EQUAL -1)
        message(FATAL_ERROR "${PATCH_SOURCE} does not wire the bootstrap lifecycle patch")
    endif()
endforeach()
