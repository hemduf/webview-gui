cmake_minimum_required(VERSION 3.24)

# Audio plug-ins are loaded alongside unrelated third-party modules in one host
# process. Keep this implementation private to each plug-in and never let a
# parent BUILD_SHARED_LIBS setting turn it into a process-shared runtime.
add_library(webview-gui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source/webview-gui.cpp
)

set(WEBVIEW_GUI_CHOC_SOURCE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/include/webview-gui/_impl/platform/choc)
set(WEBVIEW_GUI_CHOC_INCLUDE_DIRS ${WEBVIEW_GUI_CHOC_SOURCE_DIR})

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    # The pinned CHOC WebView2 backend does not validate native bridge message
    # sources or top-level navigation. Keep the upstream submodule immutable and
    # generate a tiny, drift-checked private patch for the plugin-safe profile.
    set(WEBVIEW_GUI_PATCHED_CHOC_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/webview-gui-patched-choc)
    set(WEBVIEW_GUI_CHOC_WEBVIEW_SOURCE
        ${WEBVIEW_GUI_CHOC_SOURCE_DIR}/choc/gui/choc_WebView.h)
    set(WEBVIEW_GUI_CHOC_WEBVIEW_PATCHED
        ${WEBVIEW_GUI_PATCHED_CHOC_DIR}/choc/gui/choc_WebView.h)

    file(READ ${WEBVIEW_GUI_CHOC_WEBVIEW_SOURCE} WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT)

    set(WEBVIEW_GUI_UNSAFE_WEBMESSAGE_HANDLER [=[            LPWSTR message = {};
            args->TryGetWebMessageAsString (std::addressof (message));
            ownerPimpl.owner.invokeBinding (createUTF8FromUTF16 (message));
            sender->PostWebMessageAsString (message);
            CoTaskMemFree (message);
            return S_OK;]=])

    set(WEBVIEW_GUI_SAFE_WEBMESSAGE_HANDLER [=[            return webview_gui::detail::dispatchTrustedWindowsWebMessage (
                args,
                [&] (LPCWSTR message)
                {
                    ownerPimpl.owner.invokeBinding (createUTF8FromUTF16 (message));
                    sender->PostWebMessageAsString (message);
                });]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_UNSAFE_WEBMESSAGE_HANDLER}"
        WEBVIEW_GUI_CHOC_HANDLER_OFFSET)
    if(WEBVIEW_GUI_CHOC_HANDLER_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC WebView message handler changed; refusing to build without revalidating the Windows bridge-origin patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_UNSAFE_WEBMESSAGE_HANDLER}"
        "${WEBVIEW_GUI_SAFE_WEBMESSAGE_HANDLER}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    set(WEBVIEW_GUI_WEBMESSAGE_INTERFACE [=[MIDL_INTERFACE("57213f19-00e6-49fa-8e07-898ea01ecbd2")
ICoreWebView2WebMessageReceivedEventHandler : public IUnknown
{
public:
     virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *) = 0;
};]=])

    set(WEBVIEW_GUI_NAVIGATION_INTERFACES [=[

// Minimal ABI-compatible subset needed by the plugin-safe navigation policy.
// These methods are the stable leading ICoreWebView2NavigationStartingEventArgs
// vtable entries present since the initial WebView2 API.
struct ICoreWebView2NavigationStartingEventArgs : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsUserInitiated(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRedirected(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RequestHeaders(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Cancel(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Cancel(BOOL) = 0;
};

struct ICoreWebView2NavigationStartingEventHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) = 0;
};]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_WEBMESSAGE_INTERFACE}"
        WEBVIEW_GUI_CHOC_INTERFACE_OFFSET)
    if(WEBVIEW_GUI_CHOC_INTERFACE_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC WebView interfaces changed; refusing to build without revalidating the Windows navigation patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_WEBMESSAGE_INTERFACE}"
        "${WEBVIEW_GUI_WEBMESSAGE_INTERFACE}${WEBVIEW_GUI_NAVIGATION_INTERFACES}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    set(WEBVIEW_GUI_EVENT_BASES_OLD [=[                           public ICoreWebView2WebMessageReceivedEventHandler,
                           public ICoreWebView2PermissionRequestedEventHandler,]=])
    set(WEBVIEW_GUI_EVENT_BASES_NEW [=[                           public ICoreWebView2WebMessageReceivedEventHandler,
                           public ICoreWebView2NavigationStartingEventHandler,
                           public ICoreWebView2PermissionRequestedEventHandler,]=])
    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_EVENT_BASES_OLD}"
        WEBVIEW_GUI_CHOC_BASES_OFFSET)
    if(WEBVIEW_GUI_CHOC_BASES_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC EventHandler bases changed; refusing to build without revalidating the Windows navigation patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_EVENT_BASES_OLD}"
        "${WEBVIEW_GUI_EVENT_BASES_NEW}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    set(WEBVIEW_GUI_EVENT_REGISTRATION_OLD [=[            view->add_WebMessageReceived (this, std::addressof (token));
            view->add_PermissionRequested (this, std::addressof (token));]=])
    set(WEBVIEW_GUI_EVENT_REGISTRATION_NEW [=[            view->add_WebMessageReceived (this, std::addressof (token));
            view->add_NavigationStarting (
                static_cast<ICoreWebView2NavigationStartingEventHandler*> (this),
                std::addressof (token));
            view->add_PermissionRequested (this, std::addressof (token));]=])
    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_EVENT_REGISTRATION_OLD}"
        WEBVIEW_GUI_CHOC_REGISTRATION_OFFSET)
    if(WEBVIEW_GUI_CHOC_REGISTRATION_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC WebView event registration changed; refusing to build without revalidating the Windows navigation patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_EVENT_REGISTRATION_OLD}"
        "${WEBVIEW_GUI_EVENT_REGISTRATION_NEW}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    set(WEBVIEW_GUI_PERMISSION_INVOKE [=[        HRESULT STDMETHODCALLTYPE Invoke (ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) override]=])
    set(WEBVIEW_GUI_NAVIGATION_INVOKE [=[        HRESULT STDMETHODCALLTYPE Invoke (ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) override
        {
            if (deletionCheckerRef->deleted)
                return E_FAIL;

            return webview_gui::detail::handleWindowsPluginNavigation (
                args,
                [] (LPCWSTR uri) { webview_gui::detail::openWindowsExternalURL (uri); });
        }

        HRESULT STDMETHODCALLTYPE Invoke (ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) override]=])
    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_PERMISSION_INVOKE}"
        WEBVIEW_GUI_CHOC_PERMISSION_OFFSET)
    if(WEBVIEW_GUI_CHOC_PERMISSION_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC permission handler changed; refusing to build without revalidating the Windows navigation patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_PERMISSION_INVOKE}"
        "${WEBVIEW_GUI_NAVIGATION_INVOKE}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    # The patched WebView header keeps CHOC's relative includes (for example
    # ../platform/choc_Platform.h). Mirror the pinned CHOC include tree before
    # overwriting that one header so the generated private profile has the same
    # layout on every compiler, including MinGW.
    file(REMOVE_RECURSE ${WEBVIEW_GUI_PATCHED_CHOC_DIR}/choc)
    file(COPY ${WEBVIEW_GUI_CHOC_SOURCE_DIR}/choc
        DESTINATION ${WEBVIEW_GUI_PATCHED_CHOC_DIR})
    file(WRITE ${WEBVIEW_GUI_CHOC_WEBVIEW_PATCHED}
        "#define WEBVIEW_GUI_CHOC_WINDOWS_BRIDGE_GUARD 1\n${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    list(PREPEND WEBVIEW_GUI_CHOC_INCLUDE_DIRS ${WEBVIEW_GUI_PATCHED_CHOC_DIR})
endif()

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
