#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(_WIN32)
#error Windows-only test
#endif

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using RetainWebViewsFn = bool (*)(std::size_t, std::uintptr_t*, std::uintptr_t*, wchar_t*, std::size_t, bool*, bool*);
using ExerciseHostLifecycleFn = bool (*)(std::uintptr_t, std::size_t);
using ExchangeMessagesFn = bool (*)(std::size_t);
using ReleaseWebViewsFn = void (*)();

struct Module {
    HMODULE handle = nullptr;
    RetainWebViewsFn retainWebViews = nullptr;
    ExerciseHostLifecycleFn exerciseHostLifecycle = nullptr;
    ExchangeMessagesFn exchangeMessages = nullptr;
    ReleaseWebViewsFn releaseWebViews = nullptr;

    explicit Module(const char* path)
    {
        handle = LoadLibraryA(path);
        if (!handle)
            return;

        retainWebViews = reinterpret_cast<RetainWebViewsFn>(
            GetProcAddress(handle, "webview_gui_test_retain_windows_webviews"));
        exerciseHostLifecycle = reinterpret_cast<ExerciseHostLifecycleFn>(
            GetProcAddress(handle, "webview_gui_test_exercise_windows_host_lifecycle"));
        exchangeMessages = reinterpret_cast<ExchangeMessagesFn>(
            GetProcAddress(handle, "webview_gui_test_exchange_windows_messages"));
        releaseWebViews = reinterpret_cast<ReleaseWebViewsFn>(
            GetProcAddress(handle, "webview_gui_test_release_windows_webviews"));
    }

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    ~Module() { close(); }

    bool retain(std::size_t count) const
    {
        if (!retainWebViews)
            return false;

        std::uintptr_t module = 0;
        std::uintptr_t firstWindowInstance = 0;
        std::array<wchar_t, 256> className{};
        bool allOwnedByModule = false;
        bool allClassNamesUnique = false;

        return retainWebViews(count,
                              &module,
                              &firstWindowInstance,
                              className.data(),
                              className.size(),
                              &allOwnedByModule,
                              &allClassNamesUnique)
            && module != 0
            && firstWindowInstance == module
            && className[0] != L'\0'
            && allOwnedByModule
            && allClassNamesUnique;
    }

    bool exercise(HWND host, std::size_t passes) const
    {
        return exerciseHostLifecycle
            && exerciseHostLifecycle(reinterpret_cast<std::uintptr_t>(host), passes);
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

        // Destroy every module-owned editor and its COM apartment while this
        // DLL's code is still resident. Only then may the host unload it.
        release();
        FreeLibrary(handle);
        handle = nullptr;
        retainWebViews = nullptr;
        exerciseHostLifecycle = nullptr;
        exchangeMessages = nullptr;
        releaseWebViews = nullptr;
    }
};

class HostWindow {
public:
    explicit HostWindow(const wchar_t* title)
    {
        handle = CreateWindowExW(0,
                                 L"STATIC",
                                 title,
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 CW_USEDEFAULT,
                                 CW_USEDEFAULT,
                                 700,
                                 500,
                                 nullptr,
                                 nullptr,
                                 GetModuleHandleW(nullptr),
                                 nullptr);
    }

    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;

    ~HostWindow()
    {
        if (handle && IsWindow(handle))
            DestroyWindow(handle);
    }

    [[nodiscard]] HWND get() const noexcept { return handle; }

private:
    HWND handle = nullptr;
};

} // namespace

TEST_CASE("32 retained Windows editors survive 200 peer-module unload and reload lifecycle cycles with independent messages")
{
    constexpr std::size_t viewsPerModule = 16;
    constexpr std::size_t requiredCycles = 200;
    std::size_t completedCycles = 0;

    HostWindow hostA{L"webview-gui module A host"};
    HostWindow hostB{L"webview-gui module B host"};
    REQUIRE(hostA.get() != nullptr);
    REQUIRE(hostB.get() != nullptr);

    for (std::size_t cycle = 0; cycle < requiredCycles; ++cycle) {
        CAPTURE(cycle);

        Module moduleA{MODULE_A_PATH};
        Module moduleB{MODULE_B_PATH};
        REQUIRE(moduleA.handle != nullptr);
        REQUIRE(moduleB.handle != nullptr);
        REQUIRE(moduleA.retainWebViews != nullptr);
        REQUIRE(moduleB.retainWebViews != nullptr);
        REQUIRE(moduleA.exerciseHostLifecycle != nullptr);
        REQUIRE(moduleB.exerciseHostLifecycle != nullptr);
        REQUIRE(moduleA.exchangeMessages != nullptr);
        REQUIRE(moduleB.exchangeMessages != nullptr);

        REQUIRE(moduleA.retain(viewsPerModule));
        REQUIRE(moduleB.retain(viewsPerModule));
        CHECK(moduleA.exercise(hostA.get(), 1));
        CHECK(moduleB.exercise(hostB.get(), 1));
        REQUIRE_MESSAGE(moduleA.exchange(1),
                        "cold-start bridge exchange must become ready before lifecycle stress continues");
        CHECK(moduleB.exchange(1));

        moduleA.close();

        // B stays loaded with sixteen native editors and must remain usable after
        // A's DLL and every A-owned window class have been unloaded.
        CHECK(IsWindow(hostB.get()));
        CHECK(moduleB.exercise(hostB.get(), 1));
        CHECK(moduleB.exchange(1));

        Module reloadedA{MODULE_A_PATH};
        REQUIRE(reloadedA.handle != nullptr);
        REQUIRE(reloadedA.exerciseHostLifecycle != nullptr);
        REQUIRE(reloadedA.exchangeMessages != nullptr);
        REQUIRE(reloadedA.retain(viewsPerModule));
        CHECK(reloadedA.exercise(hostA.get(), 1));
        CHECK(reloadedA.exchange(1));
        CHECK(moduleB.exercise(hostB.get(), 1));
        CHECK(moduleB.exchange(1));

        reloadedA.close();
        moduleB.close();

        // Host parents outlive all plug-in editor modules and are reused across
        // every cycle. This catches child teardown that accidentally destroys or
        // corrupts host-owned HWNDs.
        CHECK(IsWindow(hostA.get()));
        CHECK(IsWindow(hostB.get()));

        ++completedCycles;
    }

    CHECK(completedCycles >= requiredCycles);
}
