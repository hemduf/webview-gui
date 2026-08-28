cmake_minimum_required(VERSION 3.24)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

file(READ "${REPO_ROOT}/CMakeLists.txt" ROOT_CMAKE)
file(READ "${REPO_ROOT}/cmake/WebviewGuiChocLinuxPatch.cmake" LINUX_PATCH)
file(READ "${REPO_ROOT}/cmake/WebviewGuiChocMacOSPatch.cmake" MACOS_PATCH)
file(READ "${REPO_ROOT}/include/webview-gui/_impl/platform/choc.h" WRAPPER_SOURCE)

# CSP must be enforced by the native resource response, not by mutating HTML.
# Check each backend's concrete response-header primitive so a comment or helper
# declaration cannot satisfy this contract accidentally.
string(FIND "${ROOT_CMAKE}"
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

string(FIND "${WRAPPER_SOURCE}"
    "applyPluginHTMLHardening(resource.bytes)"
    LEGACY_HTML_CSP_REWRITE_POSITION)
if(NOT LEGACY_HTML_CSP_REWRITE_POSITION EQUAL -1)
    message(FATAL_ERROR "Resource loading still depends on HTML CSP rewriting instead of native response headers")
endif()
