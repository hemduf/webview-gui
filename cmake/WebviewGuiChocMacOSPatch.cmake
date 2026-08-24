function(webview_gui_apply_choc_macos_lifetime_patch source_file output_file)
    file(READ "${source_file}" WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT)

    set(WEBVIEW_GUI_CHOC_MACOS_OLD_TEARDOWN [=[    ~Pimpl()
    {
        CHOC_AUTORELEASE_BEGIN
        deletionChecker->deleted = true;
        objc_setAssociatedObject (delegate, "choc_webview", nil, OBJC_ASSOCIATION_ASSIGN);
        objc_setAssociatedObject (webview, "choc_webview", nil, OBJC_ASSOCIATION_ASSIGN);
        objc::call<void> (webview, "release");
        webview = {};
        objc::call<void> (manager, "removeScriptMessageHandlerForName:", objc::getNSString ("external"));
        objc::call<void> (manager, "release");
        manager = {};
        objc::call<void> (delegate, "release");
        delegate = {};
        CHOC_AUTORELEASE_END
    }]=])

    set(WEBVIEW_GUI_CHOC_MACOS_SAFE_TEARDOWN [=[    ~Pimpl()
    {
        CHOC_AUTORELEASE_BEGIN
        deletionChecker->deleted = true;
        objc_setAssociatedObject (delegate, "choc_webview", nil, OBJC_ASSOCIATION_ASSIGN);
        objc_setAssociatedObject (webview, "choc_webview", nil, OBJC_ASSOCIATION_ASSIGN);

        // WKWebView may keep weak delegate pointers and queued navigation work
        // alive until its own teardown. Detach every callback entry point while
        // the plug-in image and its dynamically generated delegate IMPs are
        // still loaded, then stop outstanding work before releasing the view.
        objc::call<void> (webview, "setUIDelegate:", (id) nil);
        objc::call<void> (webview, "setNavigationDelegate:", (id) nil);
        objc::call<void> (manager, "removeScriptMessageHandlerForName:", objc::getNSString ("external"));
        objc::call<void> (webview, "stopLoading");

        objc::call<void> (webview, "release");
        webview = {};
        objc::call<void> (manager, "release");
        manager = {};
        objc::call<void> (delegate, "release");
        delegate = {};
        CHOC_AUTORELEASE_END
    }]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_CHOC_MACOS_OLD_TEARDOWN}"
        WEBVIEW_GUI_CHOC_MACOS_TEARDOWN_OFFSET)
    if(WEBVIEW_GUI_CHOC_MACOS_TEARDOWN_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC macOS WebView teardown changed; refusing to build without revalidating the plug-in unload patch")
    endif()

    string(REPLACE
        "${WEBVIEW_GUI_CHOC_MACOS_OLD_TEARDOWN}"
        "${WEBVIEW_GUI_CHOC_MACOS_SAFE_TEARDOWN}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    file(WRITE "${output_file}" "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
endfunction()
