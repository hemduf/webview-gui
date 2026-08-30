cmake_minimum_required(VERSION 3.24)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

file(READ "${REPO_ROOT}/cmake/WebviewGuiChocWindowsResourceLifetimePatch.cmake" WINDOWS_PATCH)
file(READ "${REPO_ROOT}/cmake/WebviewGuiChocLinuxPatch.cmake" LINUX_PATCH)
file(READ "${REPO_ROOT}/cmake/WebviewGuiChocMacOSPatch.cmake" MACOS_PATCH)
file(READ "${REPO_ROOT}/include/webview-gui/_impl/platform/choc.h" WRAPPER_SOURCE)

# CSP must be enforced by each native resource-response backend. Check each
# concrete response-header primitive so a comment or helper declaration cannot
# satisfy the contract accidentally.
string(FIND "${WINDOWS_PATCH}"
    "headers.emplace_back (\"Content-Security-Policy: "
    WINDOWS_CSP_HEADER_POSITION)
if(WINDOWS_CSP_HEADER_POSITION EQUAL -1)
    message(FATAL_ERROR "Windows WebView2 resources do not attach a native Content-Security-Policy response header")
endif()

string(FIND "${LINUX_PATCH}"
    "soup_message_headers_append (headers, \"Content-Security-Policy\""
    LINUX_CSP_HEADER_POSITION)
if(LINUX_CSP_HEADER_POSITION EQUAL -1)
    message(FATAL_ERROR "Linux WebKitGTK resources do not attach a native Content-Security-Policy response header")
endif()

string(FIND "${MACOS_PATCH}"
    "getNSString (\"Content-Security-Policy\")"
    MACOS_CSP_HEADER_POSITION)
if(MACOS_CSP_HEADER_POSITION EQUAL -1)
    message(FATAL_ERROR "macOS WKURLSchemeHandler resources do not attach a native Content-Security-Policy response header")
endif()

# Exercise the Windows patch at the exact point where CMake applies it: before
# the later wildcard-CORS narrowing. This catches anchors that only match the
# final generated source and therefore fail during a real Windows configure.
file(READ
    "${REPO_ROOT}/include/webview-gui/_impl/platform/choc/choc/gui/choc_WebView.h"
    WINDOWS_PATCH_APPLICATION)
include("${REPO_ROOT}/cmake/WebviewGuiChocWindowsResourceLifetimePatch.cmake")
webview_gui_apply_choc_windows_resource_lifetime_patch(WINDOWS_PATCH_APPLICATION)
string(FIND "${WINDOWS_PATCH_APPLICATION}"
    "Content-Security-Policy: "
    WINDOWS_APPLIED_CSP_POSITION)
if(WINDOWS_APPLIED_CSP_POSITION EQUAL -1)
    message(FATAL_ERROR "Windows native CSP patch did not apply to the pinned CHOC source")
endif()

# Once native response headers carry CSP on every supported backend, resource
# bytes must pass through unchanged. Keeping HTML rewriting would leave a second,
# parser-dependent policy path and could still corrupt malformed or encoded HTML.
string(FIND "${WRAPPER_SOURCE}"
    "applyPluginHTMLHardening(resource.bytes)"
    LEGACY_HTML_CSP_REWRITE_POSITION)
if(NOT LEGACY_HTML_CSP_REWRITE_POSITION EQUAL -1)
    message(FATAL_ERROR "Resource loading still rewrites HTML for CSP instead of relying exclusively on native response headers")
endif()
