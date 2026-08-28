include_guard(GLOBAL)

# CHOC normally starts resource-backed navigation from inside each native Pimpl,
# before webviewIsReady can install webview-gui's security document-start scripts.
# Keep upstream CHOC semantics as the default and add a private per-instance opt-in
# used only by WebviewGui. Every edit is exact and fail-closed against pinned CHOC
# source drift.
function(webview_gui_disable_choc_automatic_resource_navigation content_var)
    set(content "${${content_var}}")

    set(OPTIONS_OLD [=[        std::function<void(choc::ui::WebView&)> webviewIsReady;]=])
    set(OPTIONS_NEW [=[        std::function<void(choc::ui::WebView&)> webviewIsReady;

        // webview-gui private build option. Upstream CHOC behaviour remains the
        // default for direct CHOC users; the wrapper opts in only while it needs
        // to install security scripts before the first resource navigation.
        bool webviewGuiDeferInitialResourceNavigation = false;]=])

    string(FIND "${content}" "${OPTIONS_OLD}" options_offset)
    if(options_offset EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC Options changed; refusing to build without revalidating bootstrap navigation control")
    endif()
    string(REPLACE
        "${OPTIONS_OLD}"
        "${OPTIONS_NEW}"
        content
        "${content}")

    set(LINUX_OLD [=[            webkit_web_context_register_uri_scheme (webviewContext, getURIScheme (options).c_str(), onResourceRequested, this, nullptr);
            navigate ({});]=])
    set(LINUX_NEW [=[            webkit_web_context_register_uri_scheme (webviewContext, getURIScheme (options).c_str(), onResourceRequested, this, nullptr);
            if (! options.webviewGuiDeferInitialResourceNavigation)
                navigate ({});]=])

    set(MACOS_OLD [=[        if (options->fetchResource)
            navigate ({});]=])
    set(MACOS_NEW [=[        if (options->fetchResource && ! options->webviewGuiDeferInitialResourceNavigation)
            navigate ({});]=])

    set(WINDOWS_OLD [=[        if (options.fetchResource)
            navigate ({});]=])
    set(WINDOWS_NEW [=[        if (options.fetchResource && ! options.webviewGuiDeferInitialResourceNavigation)
            navigate ({});]=])

    foreach(platform IN ITEMS LINUX MACOS WINDOWS)
        string(FIND "${content}" "${${platform}_OLD}" offset)
        if(offset EQUAL -1)
            message(FATAL_ERROR
                "Pinned CHOC ${platform} startup navigation changed; refusing to build without revalidating bootstrap-before-navigation ordering")
        endif()

        string(REPLACE
            "${${platform}_OLD}"
            "${${platform}_NEW}"
            content
            "${content}")
    endforeach()

    set(${content_var} "${content}" PARENT_SCOPE)
endfunction()

# WebView2's AddScriptToExecuteOnDocumentCreated is asynchronous. For the
# webview-gui opt-in profile, hold the initial navigation until every document-
# start registration has completed successfully. Direct CHOC users keep the
# upstream fire-and-forget behaviour and never allocate these completion objects.
function(webview_gui_await_choc_windows_init_script_registration content_var)
    set(content "${${content_var}}")

    set(INTERFACE_OLD [=[MIDL_INTERFACE("49511172-cc67-4bca-9923-137112f4c4cc")
ICoreWebView2ExecuteScriptCompletedHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke (HRESULT, LPCWSTR) = 0;
};]=])
    set(INTERFACE_NEW [=[MIDL_INTERFACE("b99369f3-9b11-47b5-bc6f-8e7895fcea17")
ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke (HRESULT, LPCWSTR) = 0;
};

MIDL_INTERFACE("49511172-cc67-4bca-9923-137112f4c4cc")
ICoreWebView2ExecuteScriptCompletedHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke (HRESULT, LPCWSTR) = 0;
};]=])

    string(FIND "${content}" "${INTERFACE_OLD}" interface_offset)
    if(interface_offset EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC WebView2 completion interfaces changed; refusing to build without revalidating init-script registration gating")
    endif()
    string(REPLACE
        "${INTERFACE_OLD}"
        "${INTERFACE_NEW}"
        content
        "${content}")

    set(LIFECYCLE_OLD [=[    std::shared_ptr<DeletionChecker> deletionChecker { std::make_shared<DeletionChecker>() };

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
    }]=])
    set(LIFECYCLE_NEW [=[    std::shared_ptr<DeletionChecker> deletionChecker { std::make_shared<DeletionChecker>() };
    std::size_t pendingInitScriptRegistrations = 0;
    bool initScriptRegistrationFailed = false;
    std::optional<std::string> deferredNavigation;

    struct InitScriptCompletedCallback final : public ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler
    {
        explicit InitScriptCompletedCallback (Pimpl& p)
            : ownerPimpl (p), deletionCheckerRef (p.deletionChecker) {}

        HRESULT STDMETHODCALLTYPE QueryInterface (REFIID refID, void** result) override
        {
            if (refID == IID { 0xb99369f3, 0x9b11, 0x47b5, { 0xbc, 0x6f, 0x8e, 0x78, 0x95, 0xfc, 0xea, 0x17 } }
                || refID == IID_IUnknown)
            {
                *result = this;
                AddRef();
                return S_OK;
            }

            *result = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override  { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const auto newCount = --refCount;
            if (newCount == 0) delete this;
            return newCount;
        }

        HRESULT STDMETHODCALLTYPE Invoke (HRESULT hr, LPCWSTR) override
        {
            if (! deletionCheckerRef->deleted)
                ownerPimpl.initScriptRegistrationCompleted (SUCCEEDED (hr));
            return S_OK;
        }

        Pimpl& ownerPimpl;
        std::shared_ptr<DeletionChecker> deletionCheckerRef;
        std::atomic<ULONG> refCount { 1 };
    };

    bool navigateNow (const std::string& url)
    {
        if (! coreWebView)
            return false;

        const auto& target = url.empty() ? defaultURI : url;
        return coreWebView->Navigate (createUTF16StringFromUTF8 (target).c_str()) == S_OK;
    }

    bool navigate (const std::string& url)
    {
        if (! coreWebView)
            return false;

        if (options.webviewGuiDeferInitialResourceNavigation)
        {
            if (initScriptRegistrationFailed)
                return false;

            if (pendingInitScriptRegistrations != 0)
            {
                deferredNavigation = url;
                return true;
            }
        }

        return navigateNow (url);
    }

    void initScriptRegistrationCompleted (bool succeeded)
    {
        if (pendingInitScriptRegistrations == 0)
            return;

        --pendingInitScriptRegistrations;
        if (! succeeded)
        {
            initScriptRegistrationFailed = true;
            deferredNavigation.reset();
            return;
        }

        if (pendingInitScriptRegistrations == 0 && deferredNavigation && ! initScriptRegistrationFailed)
        {
            auto url = std::move (*deferredNavigation);
            deferredNavigation.reset();
            navigateNow (url);
        }
    }

    bool addInitScript (const std::string& script)
    {
        if (! coreWebView)
            return false;

        const auto utf16Script = createUTF16StringFromUTF8 (script);
        if (! options.webviewGuiDeferInitialResourceNavigation)
            return coreWebView->AddScriptToExecuteOnDocumentCreated (utf16Script.c_str(), nullptr) == S_OK;

        ++pendingInitScriptRegistrations;
        auto* callback = new InitScriptCompletedCallback (*this);
        const auto hr = coreWebView->AddScriptToExecuteOnDocumentCreated (utf16Script.c_str(), callback);
        callback->Release();

        if (hr != S_OK)
        {
            initScriptRegistrationCompleted (false);
            return false;
        }

        return true;
    }]=])

    string(FIND "${content}" "${LIFECYCLE_OLD}" lifecycle_offset)
    if(lifecycle_offset EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC WebView2 init-script lifecycle changed; refusing to build without revalidating completion-before-navigation gating")
    endif()
    string(REPLACE
        "${LIFECYCLE_OLD}"
        "${LIFECYCLE_NEW}"
        content
        "${content}")

    set(${content_var} "${content}" PARENT_SCOPE)
endfunction()
