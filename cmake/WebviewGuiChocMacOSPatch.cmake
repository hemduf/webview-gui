function(webview_gui_apply_choc_macos_lifetime_patch source_file output_file)
    file(READ "${source_file}" WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT)

    set(WEBVIEW_GUI_CHOC_MACOS_OLD_CREATE_DELEGATE [=[    id createDelegate()
    {
        static DelegateClass dc;
        return objc::call<id> ((id) dc.delegateClass, "new");
    }]=])

    set(WEBVIEW_GUI_CHOC_MACOS_SAFE_CREATE_DELEGATE [=[    id createDelegate()
    {
        // Keep only weak static state at module scope. Every live Pimpl holds a
        // lease, so the dynamic Objective-C class is destroyed while the
        // plug-in image is still loaded as soon as the last WebView goes away.
        static std::weak_ptr<DelegateClass> sharedDelegateClass;
        delegateClassLease = sharedDelegateClass.lock();

        if (! delegateClassLease)
        {
            delegateClassLease = std::make_shared<DelegateClass>();
            sharedDelegateClass = delegateClassLease;
        }

        return objc::call<id> ((id) delegateClassLease->delegateClass, "new");
    }]=])

    set(WEBVIEW_GUI_CHOC_MACOS_OLD_DELEGATE_TAIL [=[        Class delegateClass = {};
    };

    static constexpr long WKUserScriptInjectionTimeAtDocumentStart = 0;]=])

    set(WEBVIEW_GUI_CHOC_MACOS_SAFE_DELEGATE_TAIL [=[        Class delegateClass = {};
    };

    // Strong ownership is per Pimpl, never process/module-static. This makes
    // objc_disposeClassPair run while the module code is still resident.
    std::shared_ptr<DelegateClass> delegateClassLease;

    static constexpr long WKUserScriptInjectionTimeAtDocumentStart = 0;]=])

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

        // The final Pimpl lease owns the generated DelegateClass. Releasing it
        // here disposes the class before dlclose, rather than from a static
        // destructor after WebKit teardown timing has become nondeterministic.
        delegateClassLease.reset();
        CHOC_AUTORELEASE_END
    }]=])

    foreach(PAIR_NAME CREATE_DELEGATE DELEGATE_TAIL TEARDOWN)
        string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
            "${WEBVIEW_GUI_CHOC_MACOS_OLD_${PAIR_NAME}}"
            WEBVIEW_GUI_CHOC_MACOS_${PAIR_NAME}_OFFSET)
        if(WEBVIEW_GUI_CHOC_MACOS_${PAIR_NAME}_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Pinned CHOC macOS WebView ${PAIR_NAME} changed; refusing to build without revalidating the plug-in unload patch")
        endif()

        string(REPLACE
            "${WEBVIEW_GUI_CHOC_MACOS_OLD_${PAIR_NAME}}"
            "${WEBVIEW_GUI_CHOC_MACOS_SAFE_${PAIR_NAME}}"
            WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
            "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
    endforeach()

    file(WRITE "${output_file}" "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
endfunction()
