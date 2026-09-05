cmake_minimum_required(VERSION 3.24)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

file(READ "${REPO_ROOT}/CMakeLists.txt" ROOT_CMAKE)
file(READ "${REPO_ROOT}/cmake/WebviewGuiChocDependency.cmake" CHOC_DEPENDENCY)
file(READ "${REPO_ROOT}/include/webview-gui/_impl/platform/choc.h" CHOC_ADAPTER)
file(READ "${REPO_ROOT}/include/webview-gui/_impl/platform/choc_plugin_webview.h" CHOC_WRAPPER)

foreach(REQUIRED IN ITEMS
    "WebviewGuiChocDependency.cmake"
    "webview_gui_resolve_choc_dependency()")
    string(FIND "${ROOT_CMAKE}" "${REQUIRED}" POSITION)
    if(POSITION EQUAL -1)
        message(FATAL_ERROR "Root CMake no longer resolves the centralized CHOC dependency: missing ${REQUIRED}")
    endif()
endforeach()

foreach(REQUIRED IN ITEMS
    "CPMAddPackage("
    "GITHUB_REPOSITORY hemduf/choc"
    "GIT_TAG 3e815bc19e37824fa9dc6a63c8955a36fa2449ae"
    "GIT_TAG 0bc73f41cedb561efe5643826891dcb705c680de"
    "DOWNLOAD_ONLY YES")
    string(FIND "${CHOC_DEPENDENCY}" "${REQUIRED}" POSITION)
    if(POSITION EQUAL -1)
        message(FATAL_ERROR "Central dependency module no longer pins CPM/hemduf/choc immutably: missing ${REQUIRED}")
    endif()
endforeach()

foreach(FORBIDDEN IN ITEMS
    "WEBVIEW_GUI_PATCHED_CHOC_DIR"
    "webview_gui_apply_choc_"
    "webview_gui_disable_choc_automatic_resource_navigation"
    "webview_gui_await_choc_windows_init_script_registration")
    string(FIND "${ROOT_CMAKE}" "${FORBIDDEN}" POSITION)
    if(NOT POSITION EQUAL -1)
        message(FATAL_ERROR "Legacy generated CHOC patch pipeline still present: ${FORBIDDEN}")
    endif()
endforeach()

string(FIND "${CHOC_ADAPTER}" "options.deferInitialResourceNavigation = true;" DEFER_POSITION)
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
    string(FIND "${CHOC_WRAPPER}" "${REQUIRED}" POSITION)
    if(POSITION EQUAL -1)
        message(FATAL_ERROR "Plugin-safe CHOC wrapper hook missing: ${REQUIRED}")
    endif()
endforeach()

string(FIND "${CHOC_ADAPTER}" "wv.addInitScript(bridgeBootstrap)" BOOTSTRAP_POSITION)
string(FIND "${CHOC_ADAPTER}" "wv.navigate(startUri)" NAVIGATE_POSITION)
if(BOOTSTRAP_POSITION EQUAL -1 OR NAVIGATE_POSITION EQUAL -1 OR NOT BOOTSTRAP_POSITION LESS NAVIGATE_POSITION)
    message(FATAL_ERROR "Bridge bootstrap must be installed before explicit navigation")
endif()
