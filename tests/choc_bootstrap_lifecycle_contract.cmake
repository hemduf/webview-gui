cmake_minimum_required(VERSION 3.24)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
include("${REPO_ROOT}/cmake/WebviewGuiChocBootstrapLifecyclePatch.cmake")

# Representative snippets from the pinned CHOC Options and Linux/macOS/Windows
# resource-backed startup paths. Raw CHOC users must retain upstream's automatic
# initial resource navigation; only webview-gui opts into deferring that
# navigation until its security document-start scripts are registered.
set(CHOC_SOURCE [=[
    struct Options
    {
        std::function<void(choc::ui::WebView&)> webviewIsReady;
        FetchResource fetchResource;
    };

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

string(FIND "${CHOC_SOURCE}"
    "bool webviewGuiDeferInitialResourceNavigation = false;"
    DEFER_OPTION_POSITION)
if(DEFER_OPTION_POSITION EQUAL -1)
    message(FATAL_ERROR "Private CHOC Options must preserve upstream navigation by default")
endif()

string(FIND "${CHOC_SOURCE}"
    "if (! options.webviewGuiDeferInitialResourceNavigation)"
    LINUX_GUARD_POSITION)
string(FIND "${CHOC_SOURCE}"
    "if (options->fetchResource && ! options->webviewGuiDeferInitialResourceNavigation)"
    MACOS_GUARD_POSITION)
string(FIND "${CHOC_SOURCE}"
    "if (options.fetchResource && ! options.webviewGuiDeferInitialResourceNavigation)"
    WINDOWS_GUARD_POSITION)
if(LINUX_GUARD_POSITION EQUAL -1
   OR MACOS_GUARD_POSITION EQUAL -1
   OR WINDOWS_GUARD_POSITION EQUAL -1)
    message(FATAL_ERROR "All CHOC platform startup paths must gate only the opted-in deferred navigation")
endif()

# Match the call prefix only: a CMake list treats semicolons inside full matches
# as separators, which would double-count otherwise-valid navigate({}) calls.
string(REGEX MATCHALL "navigate \\(" AUTOMATIC_NAVIGATIONS "${CHOC_SOURCE}")
list(LENGTH AUTOMATIC_NAVIGATIONS AUTOMATIC_NAVIGATION_COUNT)
if(NOT AUTOMATIC_NAVIGATION_COUNT EQUAL 3)
    message(FATAL_ERROR "Raw CHOC startup navigation semantics must remain available on all three platforms")
endif()

file(READ "${REPO_ROOT}/include/webview-gui/_impl/platform/choc.h" WRAPPER_SOURCE)
string(FIND "${WRAPPER_SOURCE}"
    "options.webviewGuiDeferInitialResourceNavigation = true;"
    WRAPPER_DEFER_POSITION)
string(FIND "${WRAPPER_SOURCE}" "wv.addInitScript(bridgeBootstrap)" BOOTSTRAP_POSITION)
string(FIND "${WRAPPER_SOURCE}" "wv.navigate(startUri)" NAVIGATE_POSITION)
if(WRAPPER_DEFER_POSITION EQUAL -1)
    message(FATAL_ERROR "webview-gui must explicitly opt into deferred initial CHOC navigation")
endif()
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

# WebView2 registers document-start scripts asynchronously. In the opted-in
# webview-gui profile, Navigate must remain deferred until every registration's
# completion handler has reported success. Raw CHOC keeps its upstream behavior.
set(WINDOWS_ASYNC_SOURCE [=[
MIDL_INTERFACE("49511172-cc67-4bca-9923-137112f4c4cc")
ICoreWebView2ExecuteScriptCompletedHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke (HRESULT, LPCWSTR) = 0;
};

    std::shared_ptr<DeletionChecker> deletionChecker { std::make_shared<DeletionChecker>() };

    bool navigate (const std::string& url)
    {
        if (! coreWebView)
            return false;

        if (url.empty())
            return navigate (defaultURI);

        return coreWebView->Navigate (createUTF16StringFromUTF8 (url).c_str()) == S_OK;
    }

    bool addInitScript (const std::string& script)
    {
        if (! coreWebView)
            return false;

        return coreWebView->AddScriptToExecuteOnDocumentCreated (createUTF16StringFromUTF8 (script).c_str(), nullptr) == S_OK;
    }
]=])

webview_gui_await_choc_windows_init_script_registration(WINDOWS_ASYNC_SOURCE)

string(FIND "${WINDOWS_ASYNC_SOURCE}"
    "ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler"
    WINDOWS_COMPLETION_HANDLER_POSITION)
string(FIND "${WINDOWS_ASYNC_SOURCE}"
    "pendingInitScriptRegistrations"
    WINDOWS_PENDING_POSITION)
string(FIND "${WINDOWS_ASYNC_SOURCE}"
    "deferredNavigation"
    WINDOWS_DEFERRED_POSITION)
string(FIND "${WINDOWS_ASYNC_SOURCE}"
    "options.webviewGuiDeferInitialResourceNavigation"
    WINDOWS_OPT_IN_POSITION)
if(WINDOWS_COMPLETION_HANDLER_POSITION EQUAL -1
   OR WINDOWS_PENDING_POSITION EQUAL -1
   OR WINDOWS_DEFERRED_POSITION EQUAL -1
   OR WINDOWS_OPT_IN_POSITION EQUAL -1)
    message(FATAL_ERROR "Windows WebView2 navigation must await successful init-script registration")
endif()

string(FIND "${ROOT_CMAKE}"
    "webview_gui_await_choc_windows_init_script_registration"
    WINDOWS_ASYNC_PATCH_WIRED_POSITION)
if(WINDOWS_ASYNC_PATCH_WIRED_POSITION EQUAL -1)
    message(FATAL_ERROR "Windows private CHOC profile does not wire async init-script registration gating")
endif()
