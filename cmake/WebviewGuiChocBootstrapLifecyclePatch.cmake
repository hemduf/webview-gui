include_guard(GLOBAL)

# CHOC's resource-backed WebView performs an implicit navigation during native
# initialisation, before webviewIsReady. webview-gui installs its security
# document-start scripts from webviewIsReady, so the private plug-in profile must
# defer that implicit navigation. The wrapper then performs the only initial
# navigation after every init script is registered.
function(webview_gui_disable_choc_automatic_resource_navigation content_var)
    set(content "${${content_var}}")

    set(LINUX_OLD [=[            webkit_web_context_register_uri_scheme (webviewContext, getURIScheme (options).c_str(), onResourceRequested, this, nullptr);
            navigate ({});]=])
    set(LINUX_NEW [=[            webkit_web_context_register_uri_scheme (webviewContext, getURIScheme (options).c_str(), onResourceRequested, this, nullptr);
            // WEBVIEW_GUI_CHOC_BOOTSTRAP_NAVIGATION_DEFERRED]=])

    set(MACOS_OLD [=[        if (options->fetchResource)
            navigate ({});]=])
    set(MACOS_NEW [=[        // WEBVIEW_GUI_CHOC_BOOTSTRAP_NAVIGATION_DEFERRED]=])

    set(WINDOWS_OLD [=[        if (options.fetchResource)
            navigate ({});]=])
    set(WINDOWS_NEW [=[        // WEBVIEW_GUI_CHOC_BOOTSTRAP_NAVIGATION_DEFERRED]=])

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

    string(FIND "${content}" "navigate ({});" remaining_automatic_navigation)
    if(NOT remaining_automatic_navigation EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC contains an unreviewed automatic resource navigation; refusing to build")
    endif()

    set(${content_var} "${content}" PARENT_SCOPE)
endfunction()
