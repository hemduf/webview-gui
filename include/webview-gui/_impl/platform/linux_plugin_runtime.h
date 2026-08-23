#pragma once

#if !defined(__linux__)
#error linux_plugin_runtime.h is Linux-only
#endif

#include "../plugin_support.h"

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <webkit2/webkit2.h>

#include <cstdint>
#include <string_view>

namespace webview_gui::detail {

class GtkXEmbedHost {
public:
    GtkXEmbedHost() = default;
    GtkXEmbedHost(const GtkXEmbedHost&) = delete;
    GtkXEmbedHost& operator=(const GtkXEmbedHost&) = delete;

    ~GtkXEmbedHost()
    {
        detach();
    }

    bool attach(GtkWidget* webview, std::uintptr_t parentXid)
    {
        if (!webview || parentXid == 0 || plug != nullptr)
            return false;

        plug = gtk_plug_new(static_cast<Window>(parentXid));
        if (!plug) return false;

        child = webview;
        policyHandler = g_signal_connect(
            webview,
            "decide-policy",
            G_CALLBACK(+[](WebKitWebView*, WebKitPolicyDecision* decision,
                           WebKitPolicyDecisionType type, gpointer) -> gboolean
            {
                if (!decision)
                    return FALSE;

                if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION
                    || type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION)
                {
                    auto* navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
                    auto* action = webkit_navigation_policy_decision_get_navigation_action(navigation);
                    auto* request = action ? webkit_navigation_action_get_request(action) : nullptr;
                    const char* uri = request ? webkit_uri_request_get_uri(request) : nullptr;

                    if (uri && isTrustedPluginURL(uri)) {
                        webkit_policy_decision_use(decision);
                    } else {
                        webkit_policy_decision_ignore(decision);
                    }
                    return TRUE;
                }

                return FALSE;
            }),
            nullptr);

        gtk_container_add(GTK_CONTAINER(plug), webview);
        gtk_widget_show_all(plug);
        return true;
    }

    void detach()
    {
        if (child && policyHandler != 0) {
            g_signal_handler_disconnect(child, policyHandler);
            policyHandler = 0;
        }

        if (plug) {
            if (child && gtk_widget_get_parent(child) == plug)
                gtk_container_remove(GTK_CONTAINER(plug), child);
            gtk_widget_destroy(plug);
        }

        child = nullptr;
        plug = nullptr;
    }

    bool resize(int width, int height)
    {
        if (!plug || !child || width < 0 || height < 0)
            return false;

        gtk_widget_set_size_request(child, width, height);
        gtk_window_resize(GTK_WINDOW(plug), width, height);
        return true;
    }

    bool setVisible(bool visible)
    {
        if (!plug) return false;
        if (visible)
            gtk_widget_show_all(plug);
        else
            gtk_widget_hide(plug);
        return true;
    }

    [[nodiscard]] GtkWidget* plugWidget() const noexcept { return plug; }
    [[nodiscard]] GtkWidget* childWidget() const noexcept { return child; }

private:
    GtkWidget* plug = nullptr;
    GtkWidget* child = nullptr;
    gulong policyHandler = 0;
};

} // namespace webview_gui::detail
