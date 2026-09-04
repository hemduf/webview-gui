from pathlib import Path
import subprocess

CHOC_REF = "3e815bc19e37824fa9dc6a63c8955a36fa2449ae"
ROOT = Path(".")


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: {label}: expected one anchor, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


# -----------------------------------------------------------------------------
# Root CMake: replace the generated/private CHOC patch pipeline with one pinned
# CPM dependency on the maintained fork.
cmake = Path("CMakeLists.txt")
text = cmake.read_text(encoding="utf-8")
start_marker = "set(WEBVIEW_GUI_CHOC_SOURCE_DIR\n"
end_marker = "# Tests that compile the supported header-only plug-in mode need the exact same\n"
start = text.find(start_marker)
end = text.find(end_marker)
if start < 0 or end < 0 or end <= start:
    raise RuntimeError("CMakeLists.txt: unable to locate the CHOC patch pipeline")

new_dependency = f'''include(FetchContent)

# CPM is only the dependency bootstrap. CHOC itself is fetched exclusively from
# the maintained hemduf fork below and pinned to an immutable commit.
FetchContent_Declare(
    cpm_cmake
    GIT_REPOSITORY https://github.com/cpm-cmake/CPM.cmake.git
    GIT_TAG v0.40.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cpm_cmake)
include("${{cpm_cmake_SOURCE_DIR}}/cmake/CPM.cmake")

CPMAddPackage(
    NAME choc
    GITHUB_REPOSITORY hemduf/choc
    GIT_TAG {CHOC_REF}
    GIT_SHALLOW FALSE
    DOWNLOAD_ONLY YES
)

if(NOT choc_SOURCE_DIR)
    message(FATAL_ERROR "CPM failed to provide the pinned hemduf/choc source tree")
endif()

set(WEBVIEW_GUI_CHOC_INCLUDE_DIRS "${{choc_SOURCE_DIR}}")

'''
text = text[:start] + new_dependency + text[end:]
cmake.write_text(text, encoding="utf-8")

# -----------------------------------------------------------------------------
# Consumer opt-in renamed from the old generated webview-gui-only CHOC field to
# the generic fork option.
replace_once(
    Path("include/webview-gui/_impl/platform/choc.h"),
    "\toptions.webviewGuiDeferInitialResourceNavigation = true;",
    "\toptions.deferInitialResourceNavigation = true;",
    "deferred initial navigation option",
)

# -----------------------------------------------------------------------------
# Configure the fork's plugin-safe hooks in the existing wrapper. CHOC remains
# independent of webview-gui; only this adapter maps generic hooks to project
# policy/runtime helpers.
wrapper = Path("include/webview-gui/_impl/platform/choc_plugin_webview.h")
text = wrapper.read_text(encoding="utf-8")
include_anchor = '#include "../resource_security.h"\n'
if text.count(include_anchor) != 1:
    raise RuntimeError("choc_plugin_webview.h: resource security include anchor drifted")
common_hooks = '''#include "../resource_security.h"

// Opt into the unloadable-plugin profile provided by hemduf/choc. Policy stays
// in webview-gui and is supplied to CHOC through narrow compile-time hooks.
#define CHOC_WEBVIEW_PLUGIN_SAFE 1
#define CHOC_WEBVIEW_CONTENT_SECURITY_POLICY webview_gui::detail::pluginContentSecurityPolicy.data()
#if CHOC_WINDOWS
#define CHOC_WEBVIEW_RESOURCE_ALLOW_ORIGIN "https://choc.localhost"
#else
#define CHOC_WEBVIEW_RESOURCE_ALLOW_ORIGIN webview_gui::detail::appleLinuxPluginResourceAllowOrigin.data()
#endif
'''
text = text.replace(include_anchor, common_hooks, 1)

windows_old = '''#define CoInitialize(...) webview_gui::detail::suppressedCHOCWebViewCoInitialize(nullptr)
#include "choc/gui/choc_WebView.h"
#undef CoInitialize

#ifndef WEBVIEW_GUI_CHOC_WINDOWS_BRIDGE_GUARD
#error webview-gui requires its generated plugin-safe CHOC WebView2 bridge patch on Windows
#endif
#undef WEBVIEW_GUI_CHOC_WINDOWS_BRIDGE_GUARD
'''
windows_new = '''#define CoInitialize(...) webview_gui::detail::suppressedCHOCWebViewCoInitialize(nullptr)
#define CHOC_WEBVIEW_WINDOWS_RESOURCE_CALLBACK_GUARD \\
    webview_gui::detail::ScopedCOMApartment resourceCallbackApartment; \\
    if (! resourceCallbackApartment.ok()) return E_FAIL;
#define CHOC_WEBVIEW_WINDOWS_DISPATCH_MESSAGE(args, callback) \\
    webview_gui::detail::dispatchTrustedWindowsWebMessage((args), (callback))
#define CHOC_WEBVIEW_WINDOWS_HANDLE_NAVIGATION(args) \\
    webview_gui::detail::handleWindowsPluginNavigation( \\
        (args), [] (LPCWSTR uri) {{ webview_gui::detail::openWindowsExternalURL(uri); }})
#define CHOC_WEBVIEW_WINDOWS_HANDLE_PERMISSION(args) \\
    webview_gui::detail::handleWindowsPluginPermission( \\
        (args), COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ, \\
        COREWEBVIEW2_PERMISSION_STATE_ALLOW, COREWEBVIEW2_PERMISSION_STATE_DENY)
#include "choc/gui/choc_WebView.h"
#undef CHOC_WEBVIEW_WINDOWS_HANDLE_PERMISSION
#undef CHOC_WEBVIEW_WINDOWS_HANDLE_NAVIGATION
#undef CHOC_WEBVIEW_WINDOWS_DISPATCH_MESSAGE
#undef CHOC_WEBVIEW_WINDOWS_RESOURCE_CALLBACK_GUARD
#undef CoInitialize
'''
if text.count(windows_old) != 1:
    raise RuntimeError("choc_plugin_webview.h: generated Windows bridge guard block drifted")
text = text.replace(windows_old, windows_new, 1)

# Keep the common hook macros local to this adapter.
end_anchor = "\n#endif\n"
end_pos = text.rfind(end_anchor)
if end_pos < 0:
    raise RuntimeError("choc_plugin_webview.h: final platform endif not found")
cleanup = '''
#endif

#undef CHOC_WEBVIEW_RESOURCE_ALLOW_ORIGIN
#undef CHOC_WEBVIEW_CONTENT_SECURITY_POLICY
#undef CHOC_WEBVIEW_PLUGIN_SAFE
'''
text = text[:end_pos] + cleanup + text[end_pos + len(end_anchor):]
wrapper.write_text(text, encoding="utf-8")

# -----------------------------------------------------------------------------
# Replace the old generated-patch contract test with a contract for the fork.
contract = Path("tests/choc_bootstrap_lifecycle_contract.cmake")
contract.write_text(f'''cmake_minimum_required(VERSION 3.24)

set(REPO_ROOT "${{CMAKE_CURRENT_LIST_DIR}}/..")

file(READ "${{REPO_ROOT}}/CMakeLists.txt" ROOT_CMAKE)
file(READ "${{REPO_ROOT}}/include/webview-gui/_impl/platform/choc.h" CHOC_ADAPTER)
file(READ "${{REPO_ROOT}}/include/webview-gui/_impl/platform/choc_plugin_webview.h" CHOC_WRAPPER)

foreach(REQUIRED IN ITEMS
    "CPMAddPackage("
    "GITHUB_REPOSITORY hemduf/choc"
    "GIT_TAG {CHOC_REF}"
    "DOWNLOAD_ONLY YES")
    string(FIND "${{ROOT_CMAKE}}" "${{REQUIRED}}" POSITION)
    if(POSITION EQUAL -1)
        message(FATAL_ERROR "Root CMake no longer pins hemduf/choc through CPM: missing ${{REQUIRED}}")
    endif()
endforeach()

foreach(FORBIDDEN IN ITEMS
    "WEBVIEW_GUI_PATCHED_CHOC_DIR"
    "webview_gui_apply_choc_"
    "webview_gui_disable_choc_automatic_resource_navigation"
    "webview_gui_await_choc_windows_init_script_registration")
    string(FIND "${{ROOT_CMAKE}}" "${{FORBIDDEN}}" POSITION)
    if(NOT POSITION EQUAL -1)
        message(FATAL_ERROR "Legacy generated CHOC patch pipeline still present: ${{FORBIDDEN}}")
    endif()
endforeach()

string(FIND "${{CHOC_ADAPTER}}" "options.deferInitialResourceNavigation = true;" DEFER_POSITION)
if(DEFER_POSITION EQUAL -1)
    message(FATAL_ERROR "webview-gui must opt into fork deferred initial navigation")
endif()

foreach(REQUIRED IN ITEMS
    "#define CHOC_WEBVIEW_PLUGIN_SAFE 1"
    "CHOC_WEBVIEW_CONTENT_SECURITY_POLICY"
    "CHOC_WEBVIEW_RESOURCE_ALLOW_ORIGIN"
    "CHOC_WEBVIEW_WINDOWS_RESOURCE_CALLBACK_GUARD"
    "CHOC_WEBVIEW_WINDOWS_DISPATCH_MESSAGE"
    "CHOC_WEBVIEW_WINDOWS_HANDLE_NAVIGATION"
    "CHOC_WEBVIEW_WINDOWS_HANDLE_PERMISSION")
    string(FIND "${{CHOC_WRAPPER}}" "${{REQUIRED}}" POSITION)
    if(POSITION EQUAL -1)
        message(FATAL_ERROR "Plugin-safe CHOC wrapper hook missing: ${{REQUIRED}}")
    endif()
endforeach()

string(FIND "${{CHOC_ADAPTER}}" "wv.addInitScript(bridgeBootstrap)" BOOTSTRAP_POSITION)
string(FIND "${{CHOC_ADAPTER}}" "wv.navigate(startUri)" NAVIGATE_POSITION)
if(BOOTSTRAP_POSITION EQUAL -1 OR NAVIGATE_POSITION EQUAL -1 OR NOT BOOTSTRAP_POSITION LESS NAVIGATE_POSITION)
    message(FATAL_ERROR "Bridge bootstrap must be installed before explicit navigation")
endif()
''', encoding="utf-8")

# -----------------------------------------------------------------------------
# Remove the obsolete upstream submodule and every source-generation patch.
for obsolete in (
    ".gitmodules",
    "CHOC_PIN.md",
    "cmake/WebviewGuiChocBootstrapLifecyclePatch.cmake",
    "cmake/WebviewGuiChocLinuxPatch.cmake",
    "cmake/WebviewGuiChocMacOSPatch.cmake",
    "cmake/WebviewGuiChocWindowsResourceLifetimePatch.cmake",
):
    path = Path(obsolete)
    if path.exists():
        path.unlink()

submodule = "include/webview-gui/_impl/platform/choc"
subprocess.run(["git", "rm", "-f", submodule], check=True)

# Fail closed if source/tests still reference the deleted CMake patch machinery
# or the generated-only option/marker. The migration script itself is excluded.
needles = (
    "WebviewGuiChocBootstrapLifecyclePatch",
    "WebviewGuiChocLinuxPatch",
    "WebviewGuiChocMacOSPatch",
    "WebviewGuiChocWindowsResourceLifetimePatch",
    "webviewGuiDeferInitialResourceNavigation",
    "WEBVIEW_GUI_CHOC_WINDOWS_BRIDGE_GUARD",
    "WEBVIEW_GUI_PATCHED_CHOC_DIR",
)
stale = []
for path in ROOT.rglob("*"):
    if not path.is_file() or ".git" in path.parts or path == Path(__file__):
        continue
    try:
        source = path.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        continue
    for needle in needles:
        if needle in source:
            stale.append(f"{path}: {needle}")

if stale:
    raise RuntimeError("stale CHOC patch references remain:\n" + "\n".join(stale))

print(f"Migrated webview-gui to hemduf/choc@{CHOC_REF} via CPM")
