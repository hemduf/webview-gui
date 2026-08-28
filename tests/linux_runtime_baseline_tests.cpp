#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__linux__)
#error Linux-only test
#endif

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <chrono>
#include <thread>

namespace {

void pumpEvents(int iterations = 64)
{
    for (int i = 0; i < iterations; ++i) {
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace

TEST_CASE("GTK initialization baseline is sanitizer-observable")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));
    pumpEvents();
}

TEST_CASE("raw WebKitGTK context lifetime is sanitizer-observable")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));

    auto* context = webkit_web_context_new();
    REQUIRE(context != nullptr);

    bool finalized = false;
    g_object_weak_ref(
        G_OBJECT(context),
        +[](gpointer data, GObject*) {
            *static_cast<bool*>(data) = true;
        },
        &finalized);

    g_object_unref(context);
    pumpEvents();

    CHECK(finalized);
}
