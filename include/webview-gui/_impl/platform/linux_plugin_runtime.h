#pragma once

#if !defined(__linux__)
#error linux_plugin_runtime.h is Linux-only
#endif

#include "../plugin_support.h"
#include "../origin_policy.h"

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <webkit2/webkit2.h>

#include <cstdint>

namespace webview_gui::detail {

inline bool isValidX11Parent(std::uintptr_t parentXid)
{
    if (parentXid == 0)
        return false;

    auto* display = gdk_display_get_default();
    if (!display || !GDK_IS_X11_DISPLAY(display))
        return false;

    // Ask GDK/X11 to wrap the foreign handle instead of calling Xlib directly.
    // This performs the native existence/error check inside GDK and keeps the
    // adapter link requirements limited to the GTK/GDK libraries we already
    // depend on. The returned wrapper owns a reference even when GDK already
    // knows the window.
    auto* parent = gdk_x11_window_foreign_new_for_display(
        display,
        static_cast<Window>(parentXid));
    if (!parent)
        return false;

    g_object_unref(parent);
    return true;
}

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
        if (!webview || plug != nullptr || !isValidX11Parent(parentXid))
            return false;

        plug = gtk_plug_new(static_cast<Window>(parentXid));
        if (!plug) return false;

        // GtkPlug is a GInitiallyUnowned toplevel. Keep a strong reference for
        // the entire plug-in editor lifetime: the host may tear down the XEmbed
        // relationship asynchronously, but our adapter must never retain a
        // dangling GtkWidget* while CHOC is still alive.
        g_object_ref_sink(G_OBJECT(plug));

        child = webview;
        policyHandler = g_signal_connect(
            webview,
            "decide-policy",
            G_CALLBACK(+[](WebKitWebView* webView, WebKitPolicyDecision* decision,
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

                    if (uri && isTrustedAppleLinuxPluginURL(uri)) {
                        webkit_policy_decision_use(decision);
                    } else {
                        // Ignoring the policy decision prevents the remote
                        // document from committing. Move the visible/current
                        // URI back to an unprivileged trusted page as well so
                        // callers never observe an untrusted top-level URL while
                        // the native bridge remains installed.
                        webkit_policy_decision_ignore(decision);
                        webkit_web_view_stop_loading(webView);
                        webkit_web_view_load_uri(webView, "about:blank");
                    }

                    return TRUE;
                }

                return FALSE;
            }),
            nullptr);

        // Native plug-in hosts expect keyboard focus to remain inside the
        // embedded editor after XEmbed reparenting. Make the CHOC WebKit child
        // explicitly focusable before adding it to the GtkPlug; this is harmless
        // when WebKitGTK already sets CAN_FOCUS and protects against toolkit
        // defaults changing underneath the pinned CHOC wrapper.
        gtk_widget_set_can_focus(webview, TRUE);
        gtk_container_add(GTK_CONTAINER(plug), webview);
        gtk_widget_show_all(plug);

        // XEmbed negotiation starts when the GtkPlug owns a native GdkWindow.
        // Explicit realisation is needed in headless/hosted environments where
        // merely setting the widget visible does not synchronously realise the
        // toplevel before the host checks the native relationship.
        gtk_widget_realize(plug);
        if (!gtk_widget_get_realized(plug)) {
            detach();
            return false;
        }

        if (auto* display = gtk_widget_get_display(plug))
            gdk_display_flush(display);

        return true;
    }

    void detach()
    {
        if (child && policyHandler != 0) {
            if (G_IS_OBJECT(child)
                && g_signal_handler_is_connected(child, policyHandler))
                g_signal_handler_disconnect(child, policyHandler);
            policyHandler = 0;
        }

        if (plug) {
            if (GTK_IS_WIDGET(plug)) {
                if (child && GTK_IS_WIDGET(child)
                    && gtk_widget_get_parent(child) == plug)
                    gtk_container_remove(GTK_CONTAINER(plug), child);
                gtk_widget_destroy(plug);
            }

            g_object_unref(G_OBJECT(plug));
        }

        child = nullptr;
        plug = nullptr;
    }

    bool resize(int width, int height)
    {
        if (!plug || !child || width < 0 || height < 0)
            return false;

        gtk_widget_set_size_request(child, width, height);
        gtk_widget_set_size_request(plug, width, height);

        // A same-process GtkSocket turns GtkPlug into a normal child widget,
        // so gtk_window_resize() is no longer meaningful. Let GTK propagate the
        // requested size through the socket. Foreign XEmbed parents keep the
        // plug as a toplevel backed by a child X window and can be resized
        // directly.
        if (gtk_widget_is_toplevel(plug))
            gtk_window_resize(GTK_WINDOW(plug), width, height);
        else
            gtk_widget_queue_resize(plug);

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
