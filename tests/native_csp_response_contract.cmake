cmake_minimum_required(VERSION 3.24)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

file(READ "${REPO_ROOT}/CMakeLists.txt" ROOT_CMAKE)
file(READ "${REPO_ROOT}/cmake/WebviewGuiChocDependency.cmake" CHOC_DEPENDENCY)
file(READ "${REPO_ROOT}/include/webview-gui/_impl/platform/choc_plugin_webview.h" CHOC_WRAPPER)
file(READ "${REPO_ROOT}/include/webview-gui/_impl/platform/choc.h" WRAPPER_SOURCE)

# Native CSP enforcement now lives in the maintained CHOC fork. webview-gui
# supplies only the policy value through the fork's narrow compile-time hook.
string(FIND "${CHOC_DEPENDENCY}"
    "GITHUB_REPOSITORY hemduf/choc"
    FORK_POSITION)
string(FIND "${CHOC_DEPENDENCY}"
    "GIT_TAG 3e815bc19e37824fa9dc6a63c8955a36fa2449ae"
    FORK_PIN_POSITION)
if(FORK_POSITION EQUAL -1 OR FORK_PIN_POSITION EQUAL -1)
    message(FATAL_ERROR "Native CSP contract requires the pinned hemduf/choc fork")
endif()

string(FIND "${ROOT_CMAKE}"
    "WebviewGuiChocDependency.cmake"
    DEPENDENCY_MODULE_POSITION)
string(FIND "${ROOT_CMAKE}"
    "webview_gui_resolve_choc_dependency()"
    DEPENDENCY_RESOLVE_POSITION)
if(DEPENDENCY_MODULE_POSITION EQUAL -1 OR DEPENDENCY_RESOLVE_POSITION EQUAL -1)
    message(FATAL_ERROR "Root build no longer resolves the centralized pinned CHOC dependency")
endif()

string(FIND "${CHOC_WRAPPER}"
    "#define CHOC_WEBVIEW_CONTENT_SECURITY_POLICY webview_gui::detail::pluginContentSecurityPolicy.data()"
    CSP_HOOK_POSITION)
if(CSP_HOOK_POSITION EQUAL -1)
    message(FATAL_ERROR "webview-gui does not supply CSP to the forked CHOC native resource backends")
endif()

string(FIND "${CHOC_WRAPPER}"
    "#define CHOC_WEBVIEW_RESOURCE_ALLOW_ORIGIN"
    CORS_HOOK_POSITION)
if(CORS_HOOK_POSITION EQUAL -1)
    message(FATAL_ERROR "webview-gui does not supply the resource allow-origin policy to forked CHOC")
endif()

# Resource bytes must pass through unchanged. Keeping HTML rewriting would leave
# a second parser-dependent policy path and could corrupt malformed/encoded HTML.
string(FIND "${WRAPPER_SOURCE}"
    "applyPluginHTMLHardening(resource.bytes)"
    LEGACY_HTML_CSP_REWRITE_POSITION)
if(NOT LEGACY_HTML_CSP_REWRITE_POSITION EQUAL -1)
    message(FATAL_ERROR "Resource loading still rewrites HTML for CSP instead of relying on native response headers")
endif()

# The old source-generation machinery must not reappear as a second CSP path.
foreach(FORBIDDEN IN ITEMS
    "WebviewGuiChocWindowsResourceLifetimePatch"
    "WebviewGuiChocLinuxPatch"
    "WebviewGuiChocMacOSPatch")
    string(FIND "${ROOT_CMAKE}" "${FORBIDDEN}" POSITION)
    if(NOT POSITION EQUAL -1)
        message(FATAL_ERROR "Legacy generated CSP patch is still wired: ${FORBIDDEN}")
    endif()
endforeach()
