include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/WebviewGuiChocBootstrapLifecyclePatch.cmake")

# Apply ownership/lifetime fixes to the pinned CHOC WebKitGTK backend without
# modifying the submodule. Every replacement is exact and drift-checked so a
# future CHOC update fails configuration until the patch is revalidated.
function(webview_gui_apply_choc_linux_lifetime_patch input_file output_file)
    if(NOT EXISTS "${input_file}")
        message(FATAL_ERROR "Pinned CHOC WebView header not found: ${input_file}")
    endif()

    file(READ "${input_file}" WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT)
    webview_gui_disable_choc_automatic_resource_navigation(
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT)

    # WebKitWebContext is a normal GObject returned with transfer-full ownership,
    # not a GInitiallyUnowned object. ref_sink() therefore adds an unmatched ref.
    set(WEBVIEW_GUI_CHOC_CONTEXT_OLD [=[        webviewContext = webkit_web_context_new();
        g_object_ref_sink (G_OBJECT (webviewContext));
        webview = webkit_web_view_new_with_context (webviewContext);]=])
    set(WEBVIEW_GUI_CHOC_CONTEXT_NEW [=[        webviewContext = webkit_web_context_new();
        webview = webkit_web_view_new_with_context (webviewContext);]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_CHOC_CONTEXT_OLD}"
        WEBVIEW_GUI_CHOC_CONTEXT_OFFSET)
    if(WEBVIEW_GUI_CHOC_CONTEXT_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC Linux WebKit context ownership changed; refusing to build without revalidating the lifetime patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_CHOC_CONTEXT_OLD}"
        "${WEBVIEW_GUI_CHOC_CONTEXT_NEW}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    # Tear down callbacks/scripts while the borrowed content-manager pointer is
    # still valid, then synchronously destroy the GtkWidget before dropping our
    # final GObject reference. gtk_widget_destroy() breaks WebKit/GTK widget-side
    # ownership and signal relationships that a plain unref can leave alive until
    # a later main-loop iteration, which is unsafe for an unloadable plug-in.
    set(WEBVIEW_GUI_CHOC_DESTRUCTOR_OLD [=[    ~Pimpl()
    {
        deletionChecker->deleted = true;

        if (signalHandlerID != 0 && webview != nullptr)
            g_signal_handler_disconnect (manager, signalHandlerID);

        g_clear_object (&webview);
        g_clear_object (&webviewContext);
    }]=])
    set(WEBVIEW_GUI_CHOC_DESTRUCTOR_NEW [=[    ~Pimpl()
    {
        deletionChecker->deleted = true;

        if (manager != nullptr)
        {
            if (signalHandlerID != 0
                && g_signal_handler_is_connected (manager, signalHandlerID))
                g_signal_handler_disconnect (manager, signalHandlerID);

            webkit_user_content_manager_unregister_script_message_handler (manager, "external");
            webkit_user_content_manager_remove_all_scripts (manager);
        }

        if (webview != nullptr)
        {
            webkit_web_view_stop_loading (WEBKIT_WEB_VIEW (webview));
            gtk_widget_destroy (GTK_WIDGET (webview));
        }

        signalHandlerID = 0;
        manager = nullptr;
        g_clear_object (&webview);
        g_clear_object (&webviewContext);
    }]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_CHOC_DESTRUCTOR_OLD}"
        WEBVIEW_GUI_CHOC_DESTRUCTOR_OFFSET)
    if(WEBVIEW_GUI_CHOC_DESTRUCTOR_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC Linux WebKit teardown changed; refusing to build without revalidating the lifetime patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_CHOC_DESTRUCTOR_OLD}"
        "${WEBVIEW_GUI_CHOC_DESTRUCTOR_NEW}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    # On the failure path WebKit returns nullptr and owns a newly allocated
    # GError. Upstream only frees error in the success branch, leaking failures.
    set(WEBVIEW_GUI_CHOC_JS_ERROR_OLD [=[        else
        {
            errorMessage = "Failed to fetch result";
        }

        (*completionHandler) (errorMessage, value);]=])
    set(WEBVIEW_GUI_CHOC_JS_ERROR_NEW [=[        else
        {
            if (error != nullptr)
            {
                errorMessage = error->message;
                g_error_free (error);
            }
            else
            {
                errorMessage = "Failed to fetch result";
            }
        }

        (*completionHandler) (errorMessage, value);]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_CHOC_JS_ERROR_OLD}"
        WEBVIEW_GUI_CHOC_JS_ERROR_OFFSET)
    if(WEBVIEW_GUI_CHOC_JS_ERROR_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC Linux JavaScript completion path changed; refusing to build without revalidating the GError lifetime patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_CHOC_JS_ERROR_OLD}"
        "${WEBVIEW_GUI_CHOC_JS_ERROR_NEW}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    # add_script() retains the WebKitUserScript but does not consume the caller's
    # reference returned by webkit_user_script_new(). Drop that caller reference.
    set(WEBVIEW_GUI_CHOC_INIT_SCRIPT_OLD [=[    bool addInitScript (const std::string& js)
    {
        if (manager != nullptr)
        {
            webkit_user_content_manager_add_script (manager, webkit_user_script_new (js.c_str(),
                                                                                     WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                                                                                     WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                                                                                     nullptr, nullptr));
            return true;
        }

        return false;
    }]=])
    set(WEBVIEW_GUI_CHOC_INIT_SCRIPT_NEW [=[    bool addInitScript (const std::string& js)
    {
        if (manager != nullptr)
        {
            auto* script = webkit_user_script_new (js.c_str(),
                                                   WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                                                   WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                                                   nullptr, nullptr);

            if (script == nullptr)
                return false;

            webkit_user_content_manager_add_script (manager, script);
            webkit_user_script_unref (script);
            return true;
        }

        return false;
    }]=])

    string(FIND "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}"
        "${WEBVIEW_GUI_CHOC_INIT_SCRIPT_OLD}"
        WEBVIEW_GUI_CHOC_INIT_SCRIPT_OFFSET)
    if(WEBVIEW_GUI_CHOC_INIT_SCRIPT_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Pinned CHOC Linux init-script ownership changed; refusing to build without revalidating the WebKitUserScript lifetime patch")
    endif()
    string(REPLACE
        "${WEBVIEW_GUI_CHOC_INIT_SCRIPT_OLD}"
        "${WEBVIEW_GUI_CHOC_INIT_SCRIPT_NEW}"
        WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT
        "${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")

    get_filename_component(WEBVIEW_GUI_CHOC_PATCH_OUTPUT_DIR "${output_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${WEBVIEW_GUI_CHOC_PATCH_OUTPUT_DIR}")
    file(WRITE "${output_file}"
        "#define WEBVIEW_GUI_CHOC_LINUX_LIFETIME_GUARD 1\n${WEBVIEW_GUI_CHOC_WEBVIEW_CONTENT}")
endfunction()
