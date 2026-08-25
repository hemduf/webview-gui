#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(_WIN32)
#error Windows-only test
#endif

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>

namespace {

using RetainWebViewsFn = bool (*)(std::size_t, std::uintptr_t*, std::uintptr_t*, wchar_t*, std::size_t, bool*, bool*);
using ExerciseHostLifecycleFn = bool (*)(std::uintptr_t, std::size_t);
using ReleaseWebViewsFn = void (*)();

struct Module {
    HMODULE handle = nullptr;
    RetainWebViewsFn retainWebViews = nullptr;
    ExerciseHostLifecycleFn exerciseHostLifecycle = nullptr;
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
        FreeLibrary(handle);
        handle = nullptr;
        retainWebViews = nullptr;
        exerciseHostLifecycle = nullptr;
        releaseWebViews = nullptr;
    }
};

HWND createHostWindow(const wchar_t* title)
{
    return CreateWindowExW(0,
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

} // namespace

TEST_CASE("32 retained Windows editors survive peer-module unload and reload host lifecycle")
{
    constexpr std::size_t viewsPerModule = 16;

    Module moduleA{MODULE_A_PATH};
    Module moduleB{MODULE_B_PATH};
    REQUIRE(moduleA.handle != nullptr);
    REQUIRE(moduleB.handle != nullptr);
    REQUIRE(moduleA.retainWebViews != nullptr);
    REQUIRE(moduleB.retainWebViews != nullptr);

    // RED: #15 requires the full host-style lifecycle for every retained editor,
    // including while a peer module unloads. The module fixture must expose that
    // operation explicitly rather than letting this test observe only one HWND.
    REQUIRE(moduleA.exerciseHostLifecycle != nullptr);
    REQUIRE(moduleB.exerciseHostLifecycle != nullptr);

    const auto hostA = createHostWindow(L"webview-gui module A host");
    const auto hostB = createHostWindow(L"webview-gui module B host");
    REQUIRE(hostA != nullptr);
    REQUIRE(hostB != nullptr);

    REQUIRE(moduleA.retain(viewsPerModule));
    REQUIRE(moduleB.retain(viewsPerModule));
    CHECK(moduleA.exercise(hostA, 2));
    CHECK(moduleB.exercise(hostB, 2));

    moduleA.release();
    moduleA.close();

    // B stays loaded with sixteen native editors and must remain usable after
    // A's DLL and every A-owned window class have been unloaded.
    CHECK(IsWindow(hostB));
    CHECK(moduleB.exercise(hostB, 2));

    Module reloadedA{MODULE_A_PATH};
    REQUIRE(reloadedA.handle != nullptr);
    REQUIRE(reloadedA.exerciseHostLifecycle != nullptr);
    REQUIRE(reloadedA.retain(viewsPerModule));
    CHECK(reloadedA.exercise(hostA, 2));
    CHECK(moduleB.exercise(hostB, 2));

    reloadedA.release();
    reloadedA.close();
    moduleB.release();
    moduleB.close();

    CHECK(IsWindow(hostA));
    CHECK(IsWindow(hostB));
    CHECK(DestroyWindow(hostA));
    CHECK(DestroyWindow(hostB));
}
