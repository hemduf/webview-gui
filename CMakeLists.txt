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
    # The pinned CHOC WebView2 backend dispatches WebMessageReceived payloads
    # without checking ICoreWebView2WebMessageReceivedEventArgs::Source first.
    # Keep the upstream submodule immutable and generate a tiny, drift-checked
    # private patch that routes the handler through our source-origin guard.
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

    file(MAKE_DIRECTORY ${WEBVIEW_GUI_PATCHED_CHOC_DIR}/choc/gui)
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
