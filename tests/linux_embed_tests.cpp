#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__linux__)
#error Linux-only test
#endif

#include "webview-gui/webview-gui.h"
#include "webview-gui/_impl/platform/choc_plugin_webview.h"
#include "webview-gui/_impl/platform/linux_plugin_runtime.h"

#include <gtk/gtk.h>
#include <gtk/gtkx.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace {

void pumpEvents(int iterations = 32)
{
    for (int i = 0; i < iterations; ++i) {
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

template <typename Predicate>
bool pumpUntil(Predicate&& predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);

    return predicate();
}

struct HostSocket {
    GtkWidget* window = nullptr;
    GtkWidget* socket = nullptr;
    gulong plugRemovedHandler = 0;

    HostSocket()
    {
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        socket = gtk_socket_new();
        plugRemovedHandler = g_signal_connect(
            socket,
            "plug-removed",
            G_CALLBACK(+[](GtkSocket*, gpointer) -> gboolean {
                // GtkSocket's default handler destroys the socket when its plug
                // disappears. A plugin host keeps its native parent alive so a
                // plugin editor may be destroyed/recreated independently.
                return TRUE;
            }),
            nullptr);
        gtk_container_add(GTK_CONTAINER(window), socket);
        gtk_widget_show_all(window);
        gtk_widget_realize(socket);
        pumpEvents();
    }

    ~HostSocket()
    {
        if (socket && plugRemovedHandler != 0
            && g_signal_handler_is_connected(socket, plugRemovedHandler))
            g_signal_handler_disconnect(socket, plugRemovedHandler);
        if (window)
            gtk_widget_destroy(window);
        pumpEvents(4);
    }

    [[nodiscard]] std::uintptr_t xid() const
    {
        return static_cast<std::uintptr_t>(gtk_socket_get_id(GTK_SOCKET(socket)));
    }
};

choc::ui::WebView::Options makeWebViewOptions()
{
    choc::ui::WebView::Options options;
    options.customSchemeURI = "choc://choc.choc/";
    options.fetchResource = [](const std::string& path)
        -> std::optional<choc::ui::WebView::Options::Resource>
    {
        if (path == "/" || path == "/index.html")
            return choc::ui::WebView::Options::Resource{
                "<!doctype html><html><body>webview-gui</body></html>",
                "text/html"};
        return std::nullopt;
    };
    return options;
}

bool smokeFocusedKeyInput(GtkWidget* child)
{
    if (!child || !gtk_widget_get_realized(child) || !gtk_widget_get_can_focus(child))
        return false;

    // A visibility change only schedules mapping. GTK explicitly requires a
    // widget to be both realised and mapped before grab_focus() can succeed.
    // Wait for the host/XEmbed round-trip instead of racing it on bare Xvfb.
    if (!pumpUntil([&] { return gtk_widget_get_mapped(child); },
                   std::chrono::seconds(2)))
        return false;

    gtk_test_widget_wait_for_draw(child);

    bool observed = false;
    const auto handler = g_signal_connect(
        child,
        "key-press-event",
        G_CALLBACK(+[](GtkWidget*, GdkEventKey*, gpointer data) -> gboolean {
            *static_cast<bool*>(data) = true;
            return FALSE;
        }),
        &observed);

    gtk_widget_grab_focus(child);
    pumpEvents(4);
    if (!gtk_widget_is_focus(child)) {
        g_signal_handler_disconnect(child, handler);
        return false;
    }

    auto* window = gtk_widget_get_window(child);
    if (!window) {
        g_signal_handler_disconnect(child, handler);
        return false;
    }

    // gtk_test_widget_send_key() routes through the XTest/display focus path,
    // which is intentionally absent when CI runs under a bare Xvfb without a
    // window manager. Dispatch a complete GTK key event to the focused native
    // WebKit widget instead. The separate is_focus check above still verifies
    // the XEmbed editor can actually acquire keyboard focus.
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    auto* key = reinterpret_cast<GdkEventKey*>(event);
    key->window = GDK_WINDOW(g_object_ref(window));
    key->send_event = TRUE;
    key->time = GDK_CURRENT_TIME;
    key->state = GdkModifierType(0);
    key->keyval = GDK_KEY_a;
    key->hardware_keycode = 0;
    key->group = 0;
    key->is_modifier = 0;
    gtk_widget_event(child, event);
    gdk_event_free(event);
    pumpEvents(2);

    g_signal_handler_disconnect(child, handler);
    return observed;
}

} // namespace

TEST_CASE("public support negotiation exposes only Linux X11 embedding")
{
    CHECK(WebviewGui::supports(WebviewGui::X11EMBED));
    CHECK_FALSE(WebviewGui::supports(WebviewGui::HWND));
    CHECK_FALSE(WebviewGui::supports(WebviewGui::COCOA));
    CHECK_FALSE(WebviewGui::supports(WebviewGui::NONE));
}

TEST_CASE("CHOC WebKitGTK view embeds into an XEmbed GtkSocket host")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));

    HostSocket host;
    REQUIRE(host.window != nullptr);
    REQUIRE(host.socket != nullptr);
    REQUIRE(host.xid() != 0);

    choc::ui::WebView view{makeWebViewOptions()};
    REQUIRE(view.loadedOK());
    auto* child = static_cast<GtkWidget*>(view.getViewHandle());
    REQUIRE(child != nullptr);

    {
        webview_gui::detail::GtkXEmbedHost adapter;
        REQUIRE(adapter.attach(child, host.xid()));

        REQUIRE(adapter.plugWidget() != nullptr);
        REQUIRE(pumpUntil([&] {
            // GtkPlug/GtkSocket uses a direct same-process GTK child path when
            // both endpoints live in this test process. In that mode
            // GtkSocket::plug_window is intentionally not populated; the
            // authoritative signals are GtkPlug::embedded plus GTK parenting.
            return gtk_plug_get_embedded(GTK_PLUG(adapter.plugWidget()))
                && gtk_widget_get_parent(adapter.plugWidget()) == host.socket;
        }, std::chrono::seconds(2)));

        CHECK(gtk_widget_get_parent(child) == adapter.plugWidget());
        CHECK(gtk_plug_get_embedded(GTK_PLUG(adapter.plugWidget())));
        CHECK(gtk_widget_get_parent(adapter.plugWidget()) == host.socket);

        REQUIRE(adapter.resize(640, 360));
        REQUIRE(pumpUntil([&] {
            GtkAllocation allocation{};
            gtk_widget_get_allocation(child, &allocation);
            return allocation.width == 640 && allocation.height == 360;
        }, std::chrono::seconds(2)));

        GtkAllocation allocation{};
        gtk_widget_get_allocation(child, &allocation);
        CHECK(allocation.width == 640);
        CHECK(allocation.height == 360);

        REQUIRE(adapter.setVisible(false));
        CHECK_FALSE(gtk_widget_get_visible(adapter.plugWidget()));
        REQUIRE(adapter.setVisible(true));
        CHECK(gtk_widget_get_visible(adapter.plugWidget()));
        CHECK(smokeFocusedKeyInput(child));
    }

    pumpEvents();

    // Destroying the plug-in-side GtkPlug must not destroy the host parent.
    CHECK(gtk_widget_get_window(host.window) != nullptr);
    CHECK(gtk_widget_get_realized(host.window));
    CHECK(GTK_IS_SOCKET(host.socket));
}

TEST_CASE("XEmbed host survives repeated WebView attach and destroy cycles")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));

    HostSocket host;
    REQUIRE(host.xid() != 0);

    for (int iteration = 0; iteration < 3; ++iteration) {
        choc::ui::WebView view{makeWebViewOptions()};
        REQUIRE(view.loadedOK());
        auto* child = static_cast<GtkWidget*>(view.getViewHandle());
        REQUIRE(child != nullptr);

        {
            webview_gui::detail::GtkXEmbedHost adapter;
            REQUIRE(adapter.attach(child, host.xid()));
            REQUIRE(pumpUntil([&] {
                return gtk_plug_get_embedded(GTK_PLUG(adapter.plugWidget()))
                    && gtk_widget_get_parent(adapter.plugWidget()) == host.socket;
            }, std::chrono::seconds(2)));

            const int width = 480 + iteration * 16;
            const int height = 280 + iteration * 12;
            REQUIRE(adapter.resize(width, height));
            REQUIRE(pumpUntil([&] {
                GtkAllocation allocation{};
                gtk_widget_get_allocation(child, &allocation);
                return allocation.width == width && allocation.height == height;
            }, std::chrono::seconds(2)));
            REQUIRE(adapter.setVisible(false));
            REQUIRE(adapter.setVisible(true));
            CHECK(smokeFocusedKeyInput(child));
        }

        pumpEvents();
        CHECK(GTK_IS_SOCKET(host.socket));
        CHECK(gtk_widget_get_realized(host.window));
    }
}

TEST_CASE("XEmbed adapter rejects invalid parents and duplicate attachment")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));

    choc::ui::WebView view{makeWebViewOptions()};
    REQUIRE(view.loadedOK());
    auto* child = static_cast<GtkWidget*>(view.getViewHandle());
    REQUIRE(child != nullptr);

    webview_gui::detail::GtkXEmbedHost adapter;
    CHECK_FALSE(adapter.attach(child, 0));

    std::uintptr_t staleParent = 0;
    {
        HostSocket staleHost;
        staleParent = staleHost.xid();
        REQUIRE(staleParent != 0);
    }
    pumpEvents();
    CHECK_FALSE(adapter.attach(child, staleParent));

    HostSocket host;
    REQUIRE(adapter.attach(child, host.xid()));
    CHECK_FALSE(adapter.attach(child, host.xid()));
}

TEST_CASE("native WebKitGTK policy blocks remote top-level navigation before commit")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));

    HostSocket host;
    choc::ui::WebView view{makeWebViewOptions()};
    REQUIRE(view.loadedOK());
    auto* child = static_cast<GtkWidget*>(view.getViewHandle());
    REQUIRE(child != nullptr);

    webview_gui::detail::GtkXEmbedHost adapter;
    REQUIRE(adapter.attach(child, host.xid()));
    REQUIRE(pumpUntil([&] {
        return gtk_plug_get_embedded(GTK_PLUG(adapter.plugWidget()));
    }, std::chrono::seconds(2)));

    REQUIRE(pumpUntil([&] {
        return !webkit_web_view_is_loading(WEBKIT_WEB_VIEW(child));
    }, std::chrono::seconds(2)));

    bool remoteCommitted = false;
    const auto loadHandler = g_signal_connect(
        child,
        "load-changed",
        G_CALLBACK(+[](WebKitWebView* webView, WebKitLoadEvent event, gpointer data) {
            if (event != WEBKIT_LOAD_COMMITTED)
                return;

            const char* uri = webkit_web_view_get_uri(webView);
            const std::string committedURI = uri ? uri : "";
            if (committedURI.find("https://example.com") == 0)
                *static_cast<bool*>(data) = true;
        }),
        &remoteCommitted);

    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(child), "https://example.com/should-not-load");
    pumpEvents(96);

    CHECK_FALSE(remoteCommitted);

    if (loadHandler != 0 && g_signal_handler_is_connected(child, loadHandler))
        g_signal_handler_disconnect(child, loadHandler);
}

#ifndef WEBVIEW_GUI_CHOC_LINUX_LIFETIME_GUARD
#error Linux tests must compile against the generated CHOC lifetime patch
#endif

namespace {

class WeakObjectRef {
public:
    WeakObjectRef()
    {
        g_weak_ref_init(&ref, nullptr);
    }

    WeakObjectRef(const WeakObjectRef&) = delete;
    WeakObjectRef& operator=(const WeakObjectRef&) = delete;

    ~WeakObjectRef()
    {
        g_weak_ref_clear(&ref);
    }

    void reset(GObject* object)
    {
        g_weak_ref_set(&ref, object);
    }

    [[nodiscard]] bool expired()
    {
        auto* object = g_weak_ref_get(&ref);
        if (!object)
            return true;

        g_object_unref(object);
        return false;
    }

private:
    GWeakRef ref{};
};

} // namespace

TEST_CASE("CHOC Linux lifetime releases GtkPlug and WebKit objects across cycles")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));

    HostSocket host;
    REQUIRE(host.xid() != 0);

    for (int iteration = 0; iteration < 4; ++iteration) {
        WeakObjectRef plugRef;
        WeakObjectRef webViewRef;
        WeakObjectRef managerRef;
        WeakObjectRef contextRef;

        {
            choc::ui::WebView view{makeWebViewOptions()};
            REQUIRE(view.loadedOK());

            auto* child = static_cast<GtkWidget*>(view.getViewHandle());
            REQUIRE(child != nullptr);
            auto* nativeView = WEBKIT_WEB_VIEW(child);
            auto* manager = webkit_web_view_get_user_content_manager(nativeView);
            auto* context = webkit_web_view_get_context(nativeView);
            REQUIRE(manager != nullptr);
            REQUIRE(context != nullptr);

            webViewRef.reset(G_OBJECT(child));
            managerRef.reset(G_OBJECT(manager));
            contextRef.reset(G_OBJECT(context));

            // Exercise the caller-owned WebKitUserScript reference. In the
            // sanitizer job these repeated scripts make an omitted unref visible
            // to LeakSanitizer instead of relying on process RSS heuristics.
            for (int script = 0; script < 8; ++script) {
                REQUIRE(view.addInitScript(
                    "globalThis.__webviewGuiLifetimeProbe = "
                    + std::to_string(iteration * 8 + script) + ";"));
            }

            {
                webview_gui::detail::GtkXEmbedHost adapter;
                REQUIRE(adapter.attach(child, host.xid()));
                REQUIRE(adapter.plugWidget() != nullptr);
                plugRef.reset(G_OBJECT(adapter.plugWidget()));

                REQUIRE(pumpUntil([&] {
                    return gtk_plug_get_embedded(GTK_PLUG(adapter.plugWidget()))
                        && gtk_widget_get_parent(adapter.plugWidget()) == host.socket;
                }, std::chrono::seconds(2)));
            }

            REQUIRE(pumpUntil([&] { return plugRef.expired(); },
                              std::chrono::seconds(2)));
            CHECK(gtk_widget_get_parent(child) == nullptr);
        }

        REQUIRE(pumpUntil([&] {
            return webViewRef.expired()
                && managerRef.expired()
                && contextRef.expired();
        }, std::chrono::seconds(2)));
    }
}

TEST_CASE("CHOC Linux JavaScript error completions release GLib errors")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));

    HostSocket host;
    choc::ui::WebView view{makeWebViewOptions()};
    REQUIRE(view.loadedOK());

    auto* child = static_cast<GtkWidget*>(view.getViewHandle());
    REQUIRE(child != nullptr);

    webview_gui::detail::GtkXEmbedHost adapter;
    REQUIRE(adapter.attach(child, host.xid()));
    REQUIRE(pumpUntil([&] {
        return gtk_plug_get_embedded(GTK_PLUG(adapter.plugWidget()));
    }, std::chrono::seconds(2)));
    REQUIRE(pumpUntil([&] {
        return !webkit_web_view_is_loading(WEBKIT_WEB_VIEW(child));
    }, std::chrono::seconds(2)));

    // Upstream CHOC previously lost the GError returned by the failed finish()
    // call. The assertions exercise that path; Linux ASan+LSan verifies the
    // repeated failures leave no unreachable GError allocations behind.
    for (int iteration = 0; iteration < 16; ++iteration) {
        bool completed = false;
        std::string errorMessage;

        REQUIRE(view.evaluateJavascript(
            "(() => {",
            [&](const std::string& error, const choc::value::ValueView&) {
                errorMessage = error;
                completed = true;
            }));

        REQUIRE(pumpUntil([&] { return completed; }, std::chrono::seconds(2)));
        CHECK_FALSE(errorMessage.empty());
    }
}
