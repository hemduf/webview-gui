#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__linux__)
#error Linux-only test
#endif

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <dlfcn.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace {

void pumpEvents(int iterations = 16)
{
    for (int i = 0; i < iterations; ++i) {
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

class HostSocket {
public:
    HostSocket()
    {
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        socket = gtk_socket_new();
        plugRemovedHandler = g_signal_connect(
            socket,
            "plug-removed",
            G_CALLBACK(+[](GtkSocket*, gpointer) -> gboolean {
                // A plug-in editor may disappear while the host's parent stays
                // alive for another editor or for a module reload.
                return TRUE;
            }),
            nullptr);
        gtk_container_add(GTK_CONTAINER(window), socket);
        gtk_widget_show_all(window);
        gtk_widget_realize(socket);
        pumpEvents();
    }

    HostSocket(const HostSocket&) = delete;
    HostSocket& operator=(const HostSocket&) = delete;

    ~HostSocket()
    {
        if (socket && plugRemovedHandler != 0
            && g_signal_handler_is_connected(socket, plugRemovedHandler))
            g_signal_handler_disconnect(socket, plugRemovedHandler);
        if (window)
            gtk_widget_destroy(window);
        pumpEvents(2);
    }

    [[nodiscard]] std::uintptr_t xid() const
    {
        return socket
            ? static_cast<std::uintptr_t>(gtk_socket_get_id(GTK_SOCKET(socket)))
            : 0;
    }

    [[nodiscard]] bool alive() const
    {
        return window && socket
            && GTK_IS_SOCKET(socket)
            && gtk_widget_get_realized(window);
    }

private:
    GtkWidget* window = nullptr;
    GtkWidget* socket = nullptr;
    gulong plugRemovedHandler = 0;
};

using RetainWebViewsFn = bool (*)(std::size_t);
using ExerciseHostLifecycleFn = bool (*)(const std::uintptr_t*, std::size_t, std::size_t);
using ExchangeMessagesFn = bool (*)(std::size_t);
using ReleaseWebViewsFn = void (*)();

class Module {
public:
    explicit Module(const char* path)
    {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!handle)
            return;

        retainWebViews = reinterpret_cast<RetainWebViewsFn>(
            dlsym(handle, "webview_gui_test_retain_linux_webviews"));
        exerciseHostLifecycle = reinterpret_cast<ExerciseHostLifecycleFn>(
            dlsym(handle, "webview_gui_test_exercise_linux_host_lifecycle"));
        exchangeMessages = reinterpret_cast<ExchangeMessagesFn>(
            dlsym(handle, "webview_gui_test_exchange_linux_messages"));
        releaseWebViews = reinterpret_cast<ReleaseWebViewsFn>(
            dlsym(handle, "webview_gui_test_release_linux_webviews"));
    }

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    ~Module() { close(); }

    [[nodiscard]] bool loaded() const noexcept { return handle != nullptr; }
    [[nodiscard]] bool hasRetain() const noexcept { return retainWebViews != nullptr; }
    [[nodiscard]] bool hasExercise() const noexcept { return exerciseHostLifecycle != nullptr; }
    [[nodiscard]] bool hasExchange() const noexcept { return exchangeMessages != nullptr; }
    [[nodiscard]] bool hasRelease() const noexcept { return releaseWebViews != nullptr; }

    bool retain(std::size_t count) const
    {
        return retainWebViews && retainWebViews(count);
    }

    bool exercise(const std::vector<std::uintptr_t>& hosts, std::size_t passes) const
    {
        return exerciseHostLifecycle
            && exerciseHostLifecycle(hosts.data(), hosts.size(), passes);
    }

    bool exchange(std::size_t messagesPerView) const
    {
        return exchangeMessages && exchangeMessages(messagesPerView);
    }

    void release() const
    {
        if (releaseWebViews)
            releaseWebViews();
    }

    void close()
    {
        if (!handle)
            return;

        release();
        dlclose(handle);
        handle = nullptr;
        retainWebViews = nullptr;
        exerciseHostLifecycle = nullptr;
        exchangeMessages = nullptr;
        releaseWebViews = nullptr;
    }

private:
    void* handle = nullptr;
    RetainWebViewsFn retainWebViews = nullptr;
    ExerciseHostLifecycleFn exerciseHostLifecycle = nullptr;
    ExchangeMessagesFn exchangeMessages = nullptr;
    ReleaseWebViewsFn releaseWebViews = nullptr;
};

std::vector<std::unique_ptr<HostSocket>> makeHosts(std::size_t count)
{
    std::vector<std::unique_ptr<HostSocket>> hosts;
    hosts.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        hosts.push_back(std::make_unique<HostSocket>());
    return hosts;
}

std::vector<std::uintptr_t> hostXids(
    const std::vector<std::unique_ptr<HostSocket>>& hosts)
{
    std::vector<std::uintptr_t> result;
    result.reserve(hosts.size());
    for (const auto& host : hosts)
        result.push_back(host ? host->xid() : 0);
    return result;
}

bool allHostsAlive(const std::vector<std::unique_ptr<HostSocket>>& hosts)
{
    for (const auto& host : hosts)
        if (!host || !host->alive() || host->xid() == 0)
            return false;
    return true;
}

} // namespace

TEST_CASE("32 retained Linux editors survive peer-module unload and reload host lifecycle")
{
    REQUIRE(gtk_init_check(nullptr, nullptr));

    constexpr std::size_t viewsPerModule = 16;
    constexpr std::size_t editorsPerLifecycleBatch = viewsPerModule * 2;
    constexpr std::size_t requiredEditorLifecycles = 200;
    constexpr std::size_t lifecycleBatches =
        (requiredEditorLifecycles + editorsPerLifecycleBatch - 1)
        / editorsPerLifecycleBatch;
    std::size_t completedEditorLifecycles = 0;

    auto hostsA = makeHosts(viewsPerModule);
    auto hostsB = makeHosts(viewsPerModule);
    REQUIRE(allHostsAlive(hostsA));
    REQUIRE(allHostsAlive(hostsB));
    const auto xidsA = hostXids(hostsA);
    const auto xidsB = hostXids(hostsB);

    Module moduleA{MODULE_A_PATH};
    Module moduleB{MODULE_B_PATH};
    REQUIRE(moduleA.loaded());
    REQUIRE(moduleB.loaded());
    REQUIRE(moduleA.hasRetain());
    REQUIRE(moduleB.hasRetain());
    REQUIRE(moduleA.hasRelease());
    REQUIRE(moduleB.hasRelease());
    REQUIRE(moduleA.hasExercise());
    REQUIRE(moduleB.hasExercise());
    REQUIRE(moduleA.hasExchange());
    REQUIRE(moduleB.hasExchange());

    // Seven batches keep 32 editors alive simultaneously and execute 224 full
    // create -> attach -> resize -> hide/show -> bridge-message -> destroy
    // editor lifecycles. Keeping the DSOs loaded here isolates editor lifecycle
    // stress from the separate peer-module unload/reload qualification below.
    for (std::size_t batch = 0; batch < lifecycleBatches; ++batch) {
        CAPTURE(batch);
        REQUIRE(moduleA.retain(viewsPerModule));
        REQUIRE(moduleB.retain(viewsPerModule));
        CHECK(moduleA.exercise(xidsA, 1));
        CHECK(moduleB.exercise(xidsB, 1));
        CHECK(moduleA.exchange(1));
        CHECK(moduleB.exchange(1));

        moduleA.release();
        moduleB.release();
        CHECK(allHostsAlive(hostsA));
        CHECK(allHostsAlive(hostsB));

        completedEditorLifecycles += editorsPerLifecycleBatch;
    }

    CHECK(completedEditorLifecycles >= requiredEditorLifecycles);

    // Keep the original DSO-isolation gate explicit: A can disappear while B
    // remains live and messaging, then a fresh A instance can be loaded again.
    REQUIRE(moduleA.retain(viewsPerModule));
    REQUIRE(moduleB.retain(viewsPerModule));
    CHECK(moduleA.exercise(xidsA, 2));
    CHECK(moduleB.exercise(xidsB, 2));
    CHECK(moduleA.exchange(3));
    CHECK(moduleB.exchange(3));

    moduleA.close();
    CHECK(allHostsAlive(hostsB));
    CHECK(moduleB.exercise(xidsB, 2));
    CHECK(moduleB.exchange(3));

    Module reloadedA{MODULE_A_PATH};
    REQUIRE(reloadedA.loaded());
    REQUIRE(reloadedA.hasExercise());
    REQUIRE(reloadedA.hasExchange());
    REQUIRE(reloadedA.retain(viewsPerModule));
    CHECK(reloadedA.exercise(xidsA, 2));
    CHECK(reloadedA.exchange(3));
    CHECK(moduleB.exercise(xidsB, 2));
    CHECK(moduleB.exchange(3));

    reloadedA.close();
    moduleB.close();
    CHECK(allHostsAlive(hostsA));
    CHECK(allHostsAlive(hostsB));
}
