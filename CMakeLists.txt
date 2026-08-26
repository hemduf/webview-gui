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

    # The pinned Windows backend uses std::wstring but relies on a transitive
    # standard-library include. It also appends UTF-8 std::string environment
    # options directly to std::wstring, which MSVC correctly rejects. Patch only
    # the private build-tree copy and fail closed if the pinned source drifts.
    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}" "#include <string>"
        WEBVIEW_GUI_CHOC_STRING_INCLUDE_OFFSET)
    if(WEBVIEW_GUI_CHOC_STRING_INCLUDE_OFFSET EQUAL -1)
        set(WEBVIEW_GUI_CHOC_PLATFORM_INCLUDE
            "#include \"../platform/choc_Platform.h\"")
        string(REGEX MATCHALL
            "#include \\\"\\.\\./platform/choc_Platform\\.h\\\""
            WEBVIEW_GUI_CHOC_PLATFORM_INCLUDE_MATCHES
            "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
        list(LENGTH WEBVIEW_GUI_CHOC_PLATFORM_INCLUDE_MATCHES
            WEBVIEW_GUI_CHOC_PLATFORM_INCLUDE_COUNT)
        if(NOT WEBVIEW_GUI_CHOC_PLATFORM_INCLUDE_COUNT EQUAL 1)
            message(FATAL_ERROR
                "Pinned CHOC include prologue changed; refusing to inject the Windows standard-library dependencies without revalidation")
        endif()
        string(REPLACE
            "${WEBVIEW_GUI_CHOC_PLATFORM_INCLUDE}"
            "${WEBVIEW_GUI_CHOC_PLATFORM_INCLUDE}\n#include <string>\n#include <limits>"
            WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
            "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
    endif()

    # Do not couple this patch to whitespace between CHOC statements: verify that
    # each semantically unsafe append occurs exactly once, then replace only those
    # proven literals. Match without the trailing semicolon because CMake list
    # semantics treat semicolons in MATCHALL results as element separators.
    string(REGEX MATCHALL "result \\+= L\" --\""
        WEBVIEW_GUI_CHOC_ENVIRONMENT_PREFIX_MATCHES
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
    string(REGEX MATCHALL "result \\+= key"
        WEBVIEW_GUI_CHOC_ENVIRONMENT_KEY_MATCHES
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
    string(REGEX MATCHALL "result \\+= value"
        WEBVIEW_GUI_CHOC_ENVIRONMENT_VALUE_MATCHES
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
    list(LENGTH WEBVIEW_GUI_CHOC_ENVIRONMENT_PREFIX_MATCHES
        WEBVIEW_GUI_CHOC_ENVIRONMENT_PREFIX_COUNT)
    list(LENGTH WEBVIEW_GUI_CHOC_ENVIRONMENT_KEY_MATCHES
        WEBVIEW_GUI_CHOC_ENVIRONMENT_KEY_COUNT)
    list(LENGTH WEBVIEW_GUI_CHOC_ENVIRONMENT_VALUE_MATCHES
        WEBVIEW_GUI_CHOC_ENVIRONMENT_VALUE_COUNT)
    if(NOT WEBVIEW_GUI_CHOC_ENVIRONMENT_PREFIX_COUNT EQUAL 1
       OR NOT WEBVIEW_GUI_CHOC_ENVIRONMENT_KEY_COUNT EQUAL 1
       OR NOT WEBVIEW_GUI_CHOC_ENVIRONMENT_VALUE_COUNT EQUAL 1)
        message(FATAL_ERROR
            "Pinned CHOC WebView environment-option builder changed; refusing to patch UTF-8 strings into UTF-16 without revalidation")
    endif()

    set(WEBVIEW_GUI_CHOC_WINDOWS_ENVIRONMENT_PREFIX_PATCH [=[const auto webviewGuiEnvironmentOldSize = result.size();
            const auto webviewGuiAppendUTF8 = [&result] (const std::string& text)
            {
                if (text.empty())
                    return true;

                if (text.size() > static_cast<size_t> (std::numeric_limits<int>::max()))
                    return false;

                const auto inputSize = static_cast<int> (text.size());
                const auto outputSize = MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS,
                                                               text.data(), inputSize, nullptr, 0);
                if (outputSize <= 0)
                    return false;

                const auto outputSizeAsSizeT = static_cast<size_t> (outputSize);
                const auto outputOffset = result.size();
                if (outputOffset > result.max_size() - outputSizeAsSizeT)
                    return false;

                result.resize (outputOffset + outputSizeAsSizeT);
                if (MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(), inputSize,
                                         &result[outputOffset], outputSize) == outputSize)
                    return true;

                result.resize (outputOffset);
                return false;
            };

            result += L" --";]=])
    set(WEBVIEW_GUI_CHOC_WINDOWS_ENVIRONMENT_KEY_PATCH [=[if (! webviewGuiAppendUTF8 (key))
            {
                result.resize (webviewGuiEnvironmentOldSize);
                return;
            }]=])
    set(WEBVIEW_GUI_CHOC_WINDOWS_ENVIRONMENT_VALUE_PATCH [=[if (! webviewGuiAppendUTF8 (value))
            {
                result.resize (webviewGuiEnvironmentOldSize);
                return;
            }]=])

    string(REPLACE
        "result += L\" --\";"
        "${WEBVIEW_GUI_CHOC_WINDOWS_ENVIRONMENT_PREFIX_PATCH}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
    string(REPLACE
        "result += key;"
        "${WEBVIEW_GUI_CHOC_WINDOWS_ENVIRONMENT_KEY_PATCH}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
    string(REPLACE
        "result += value;"
        "${WEBVIEW_GUI_CHOC_WINDOWS_ENVIRONMENT_VALUE_PATCH}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

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

    set(WEBVIEW_GUI_UNSAFE_CORS [=[                headers.emplace_back ("Access-Control-Allow-Origin: *");]=])
    set(WEBVIEW_GUI_SAFE_CORS [=[                headers.emplace_back ("Access-Control-Allow-Origin: https://choc.localhost");]=])
    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_UNSAFE_CORS}"
        WEBVIEW_GUI_CHOC_CORS_OFFSET)
    if(WEBVIEW_GUI_CHOC_CORS_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC WebView resource headers changed; refusing to build without revalidating the Windows CORS patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_UNSAFE_CORS}"
        "${WEBVIEW_GUI_SAFE_CORS}"
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
        "#define WEBVIEW_GUI_CHOC_WINDOWS_UTF8_ENVIRONMENT_GUARD 1\n#define WEBVIEW_GUI_CHOC_WINDOWS_BRIDGE_GUARD 1\n${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    list(PREPEND WEBVIEW_GUI_CHOC_INCLUDE_DIRS ${WEBVIEW_GUI_PATCHED_CHOC_DIR})
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # CHOC releases WKWebView before detaching its delegate callbacks. In an
    # unloadable plug-in that can leave WebKit with IMPs into an image that is
    # about to disappear. Patch only the pinned private CHOC copy and fail the
    # configure step if the upstream teardown drifts.
    set(WEBVIEW_GUI_PATCHED_CHOC_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/webview-gui-patched-choc)
    set(WEBVIEW_GUI_CHOC_WEBVIEW_SOURCE
        ${WEBVIEW_GUI_CHOC_SOURCE_DIR}/choc/gui/choc_WebView.h)
    set(WEBVIEW_GUI_CHOC_WEBVIEW_PATCHED
        ${WEBVIEW_GUI_PATCHED_CHOC_DIR}/choc/gui/choc_WebView.h)

    file(REMOVE_RECURSE ${WEBVIEW_GUI_PATCHED_CHOC_DIR}/choc)
    file(COPY ${WEBVIEW_GUI_CHOC_SOURCE_DIR}/choc
        DESTINATION ${WEBVIEW_GUI_PATCHED_CHOC_DIR})

    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/WebviewGuiChocMacOSPatch.cmake)
    webview_gui_apply_choc_macos_lifetime_patch(
        "${WEBVIEW_GUI_CHOC_WEBVIEW_SOURCE}"
        "${WEBVIEW_GUI_CHOC_WEBVIEW_PATCHED}")

    list(PREPEND WEBVIEW_GUI_CHOC_INCLUDE_DIRS ${WEBVIEW_GUI_PATCHED_CHOC_DIR})
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # CHOC's pinned WebKitGTK backend has several ownership bugs that are benign
    # enough in a short-lived app but leak per editor lifecycle in an unloadable
    # plug-in. Keep the submodule immutable and patch a private build-tree copy.
    set(WEBVIEW_GUI_PATCHED_CHOC_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/webview-gui-patched-choc)
    set(WEBVIEW_GUI_CHOC_WEBVIEW_SOURCE
        ${WEBVIEW_GUI_CHOC_SOURCE_DIR}/choc/gui/choc_WebView.h)
    set(WEBVIEW_GUI_CHOC_WEBVIEW_PATCHED
        ${WEBVIEW_GUI_PATCHED_CHOC_DIR}/choc/gui/choc_WebView.h)

    file(REMOVE_RECURSE ${WEBVIEW_GUI_PATCHED_CHOC_DIR}/choc)
    file(COPY ${WEBVIEW_GUI_CHOC_SOURCE_DIR}/choc
        DESTINATION ${WEBVIEW_GUI_PATCHED_CHOC_DIR})

    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/WebviewGuiChocLinuxPatch.cmake)
    webview_gui_apply_choc_linux_lifetime_patch(
        "${WEBVIEW_GUI_CHOC_WEBVIEW_SOURCE}"
        "${WEBVIEW_GUI_CHOC_WEBVIEW_PATCHED}")

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
