#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__linux__)
#error Linux-only test
#endif

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

struct HostSocket {
    GtkWidget* window = nullptr;
    GtkWidget* socket = nullptr;

    HostSocket()
    {
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        socket = gtk_socket_new();
        gtk_container_add(GTK_CONTAINER(window), socket);
        gtk_widget_show_all(window);
        gtk_widget_realize(socket);
        pumpEvents();
    }

    ~HostSocket()
    {
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

} // namespace

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
        pumpEvents();

        REQUIRE(adapter.plugWidget() != nullptr);
        CHECK(gtk_widget_get_parent(child) == adapter.plugWidget());
        CHECK(gtk_plug_get_embedded(GTK_PLUG(adapter.plugWidget())));
        CHECK(gtk_socket_get_plug_window(GTK_SOCKET(host.socket)) != nullptr);

        REQUIRE(adapter.resize(640, 360));
        pumpEvents(4);

        GtkAllocation allocation{};
        gtk_widget_get_allocation(child, &allocation);
        CHECK(allocation.width == 640);
        CHECK(allocation.height == 360);

        REQUIRE(adapter.setVisible(false));
        CHECK_FALSE(gtk_widget_get_visible(adapter.plugWidget()));
        REQUIRE(adapter.setVisible(true));
        CHECK(gtk_widget_get_visible(adapter.plugWidget()));
    }

    pumpEvents();

    // Destroying the plug-in-side GtkPlug must not destroy the host parent.
    CHECK(gtk_widget_get_window(host.window) != nullptr);
    CHECK(gtk_widget_get_realized(host.window));
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

    HostSocket host;
    REQUIRE(adapter.attach(child, host.xid()));
    CHECK_FALSE(adapter.attach(child, host.xid()));
}

TEST_CASE("native WebKitGTK policy blocks remote top-level navigation")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));

    HostSocket host;
    choc::ui::WebView view{makeWebViewOptions()};
    REQUIRE(view.loadedOK());
    auto* child = static_cast<GtkWidget*>(view.getViewHandle());
    REQUIRE(child != nullptr);

    webview_gui::detail::GtkXEmbedHost adapter;
    REQUIRE(adapter.attach(child, host.xid()));
    pumpEvents(64);

    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(child), "https://example.com/should-not-load");
    pumpEvents(96);

    const char* current = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(child));
    const std::string currentURI = current ? current : "";
    CHECK(currentURI.find("https://example.com") != 0);
}
