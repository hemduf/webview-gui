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
