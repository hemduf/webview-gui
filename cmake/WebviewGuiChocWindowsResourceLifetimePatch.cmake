function(webview_gui_apply_choc_windows_resource_lifetime_patch content_var)
    set(content "${${content_var}}")

    set(WEBVIEW_GUI_RESOURCE_CALLBACK_PROLOGUE_OLD [=[        try
        {
            if (! coreWebViewEnvironment)
                return E_FAIL;

            COMPtr<ICoreWebView2WebResourceRequest> request;]=])

    set(WEBVIEW_GUI_RESOURCE_CALLBACK_PROLOGUE_NEW [=[        try
        {
            // Resource callbacks are allowed to destroy the owning WebViewGui.
            // Snapshot every Pimpl-owned value needed after user code runs so
            // the remainder of this callback never dereferences a destroyed
            // CHOC Pimpl. The COM environment is AddRef'd by the local COMPtr.
            COMPtr<ICoreWebView2Environment> resourceEnvironment (coreWebViewEnvironment);
            if (! resourceEnvironment)
                return E_FAIL;

            const auto resourceDefaultURI = defaultURI;
            const auto resourceSetHTMLURI = setHTMLURI;
            const auto resourcePageHTML = pageHTML;
            auto resourceFetcher = options.fetchResource;
            const auto resourceUserAgent = options.customUserAgent;

            COMPtr<ICoreWebView2WebResourceRequest> request;]=])

    string(FIND "${content}"
        "${WEBVIEW_GUI_RESOURCE_CALLBACK_PROLOGUE_OLD}"
        WEBVIEW_GUI_RESOURCE_CALLBACK_PROLOGUE_OFFSET)
    if(WEBVIEW_GUI_RESOURCE_CALLBACK_PROLOGUE_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC Windows resource callback prologue changed; refusing to build without revalidating the self-destruction lifetime patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_RESOURCE_CALLBACK_PROLOGUE_OLD}"
        "${WEBVIEW_GUI_RESOURCE_CALLBACK_PROLOGUE_NEW}"
        content
        "${content}")

    set(WEBVIEW_GUI_RESOURCE_FETCH_OLD [=[            if (auto resource = fetchResourceOrPageHTML (createUTF8FromUTF16 (uri.uri)))
            {]=])

    set(WEBVIEW_GUI_RESOURCE_FETCH_NEW [=[            const auto resourceURI = createUTF8FromUTF16 (uri.uri);
            std::optional<WebView::Options::Resource> resource;

            if (resourceURI == resourceSetHTMLURI)
            {
                resource = resourcePageHTML;
            }
            else if (resourceFetcher)
            {
                if (resourceDefaultURI.empty() || resourceURI.size() + 1 < resourceDefaultURI.size())
                    return E_FAIL;

                resource = resourceFetcher (resourceURI.substr (resourceDefaultURI.size() - 1));
            }

            if (resource)
            {]=])

    string(FIND "${content}"
        "${WEBVIEW_GUI_RESOURCE_FETCH_OLD}"
        WEBVIEW_GUI_RESOURCE_FETCH_OFFSET)
    if(WEBVIEW_GUI_RESOURCE_FETCH_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC Windows resource fetch path changed; refusing to build without revalidating the self-destruction lifetime patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_RESOURCE_FETCH_OLD}"
        "${WEBVIEW_GUI_RESOURCE_FETCH_NEW}"
        content
        "${content}")

    set(WEBVIEW_GUI_RESOURCE_USER_AGENT_OLD [=[                if (! options.customUserAgent.empty())
                    headers.emplace_back ("User-Agent: " + options.customUserAgent);]=])
    set(WEBVIEW_GUI_RESOURCE_USER_AGENT_NEW [=[                if (! resourceUserAgent.empty())
                    headers.emplace_back ("User-Agent: " + resourceUserAgent);]=])

    string(FIND "${content}"
        "${WEBVIEW_GUI_RESOURCE_USER_AGENT_OLD}"
        WEBVIEW_GUI_RESOURCE_USER_AGENT_OFFSET)
    if(WEBVIEW_GUI_RESOURCE_USER_AGENT_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC Windows resource user-agent path changed; refusing to build without revalidating the self-destruction lifetime patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_RESOURCE_USER_AGENT_OLD}"
        "${WEBVIEW_GUI_RESOURCE_USER_AGENT_NEW}"
        content
        "${content}")

    set(WEBVIEW_GUI_RESOURCE_RESPONSE_200_OLD
        "coreWebViewEnvironment->CreateWebResourceResponse (stream, 200, L\"OK\", headerString.c_str(), response.getAddress())")
    set(WEBVIEW_GUI_RESOURCE_RESPONSE_200_NEW
        "resourceEnvironment->CreateWebResourceResponse (stream, 200, L\"OK\", headerString.c_str(), response.getAddress())")
    set(WEBVIEW_GUI_RESOURCE_RESPONSE_404_OLD
        "coreWebViewEnvironment->CreateWebResourceResponse (nullptr, 404, L\"Not Found\", nullptr, response.getAddress())")
    set(WEBVIEW_GUI_RESOURCE_RESPONSE_404_NEW
        "resourceEnvironment->CreateWebResourceResponse (nullptr, 404, L\"Not Found\", nullptr, response.getAddress())")

    foreach(pair IN ITEMS 200 404)
        string(FIND "${content}"
            "${WEBVIEW_GUI_RESOURCE_RESPONSE_${pair}_OLD}"
            WEBVIEW_GUI_RESOURCE_RESPONSE_OFFSET)
        if(WEBVIEW_GUI_RESOURCE_RESPONSE_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Pinned CHOC Windows ${pair} resource response path changed; refusing to build without revalidating the self-destruction lifetime patch")
        endif()
        string(REPLACE
            "${WEBVIEW_GUI_RESOURCE_RESPONSE_${pair}_OLD}"
            "${WEBVIEW_GUI_RESOURCE_RESPONSE_${pair}_NEW}"
            content
            "${content}")
    endforeach()

    set(WEBVIEW_GUI_RESOURCE_EVENT_INVOKE_OLD [=[        HRESULT STDMETHODCALLTYPE Invoke (ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) override
        {
            if (deletionCheckerRef->deleted)
                return E_FAIL;

            return ownerPimpl.onResourceRequested (args);
        }]=])

    set(WEBVIEW_GUI_RESOURCE_EVENT_INVOKE_NEW [=[        HRESULT STDMETHODCALLTYPE Invoke (ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) override
        {
            if (deletionCheckerRef->deleted)
                return E_FAIL;

            // The resource callback may synchronously destroy ownerPimpl and
            // release its eventHandler COMPtr. Hold one explicit reference until
            // Invoke() is ready to return so this handler cannot disappear from
            // underneath the active COM callback.
            AddRef();
            const auto result = ownerPimpl.onResourceRequested (args);
            Release();
            return result;
        }]=])

    string(FIND "${content}"
        "${WEBVIEW_GUI_RESOURCE_EVENT_INVOKE_OLD}"
        WEBVIEW_GUI_RESOURCE_EVENT_INVOKE_OFFSET)
    if(WEBVIEW_GUI_RESOURCE_EVENT_INVOKE_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC Windows resource event handler changed; refusing to build without revalidating the self-destruction lifetime patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_RESOURCE_EVENT_INVOKE_OLD}"
        "${WEBVIEW_GUI_RESOURCE_EVENT_INVOKE_NEW}"
        content
        "${content}")

    set(${content_var} "${content}" PARENT_SCOPE)
endfunction()
