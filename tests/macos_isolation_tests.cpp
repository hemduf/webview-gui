#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__APPLE__)
#error macOS-only test
#endif

#include <dlfcn.h>
#include <objc/runtime.h>

#include <array>
#include <cstdlib>
#include <string>

namespace {

using CreateRuntimeClassNamesFn = bool (*)(char*, std::size_t, char*, std::size_t);
using CreateRetainedStateFn = void* (*)();
using DestroyRetainedStateFn = void (*)(void*);
using RetainWebViewsFn = bool (*)(void*, std::size_t, char*, std::size_t);
using ReleaseWebViewsFn = void (*)(void*);
using ExerciseRetainedWebViewsFn = bool (*)(void*, std::size_t);
using ExerciseRetainedMessagesFn = bool (*)(void*, std::size_t);

struct RuntimeNames {
    std::string webview;
    std::string delegate;
};

struct Module {
    void* handle = nullptr;
    CreateRuntimeClassNamesFn createRuntimeClassNames = nullptr;
    CreateRetainedStateFn createRetainedState = nullptr;
    DestroyRetainedStateFn destroyRetainedState = nullptr;
    RetainWebViewsFn retainWebViews = nullptr;
    ReleaseWebViewsFn releaseWebViews = nullptr;
    ExerciseRetainedWebViewsFn exerciseRetainedWebViews = nullptr;
    ExerciseRetainedMessagesFn exerciseRetainedMessages = nullptr;
    void* retainedState = nullptr;
    void* imageBase = nullptr;

    explicit Module(const char* path)
    {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle) {
            auto createSymbol = dlsym(handle, "webview_gui_test_create_runtime_class_names");
            createRuntimeClassNames = reinterpret_cast<CreateRuntimeClassNamesFn>(createSymbol);

            Dl_info info{};
            if (createSymbol != nullptr && dladdr(createSymbol, &info) != 0)
                imageBase = info.dli_fbase;

            createRetainedState = reinterpret_cast<CreateRetainedStateFn>(
                dlsym(handle, "webview_gui_test_create_retained_state"));
            destroyRetainedState = reinterpret_cast<DestroyRetainedStateFn>(
                dlsym(handle, "webview_gui_test_destroy_retained_state"));
            retainWebViews = reinterpret_cast<RetainWebViewsFn>(
                dlsym(handle, "webview_gui_test_retain_webviews"));
            releaseWebViews = reinterpret_cast<ReleaseWebViewsFn>(
                dlsym(handle, "webview_gui_test_release_webviews"));
            exerciseRetainedWebViews = reinterpret_cast<ExerciseRetainedWebViewsFn>(
                dlsym(handle, "webview_gui_test_exercise_retained_webviews"));
            exerciseRetainedMessages = reinterpret_cast<ExerciseRetainedMessagesFn>(
                dlsym(handle, "webview_gui_test_exchange_retained_messages"));

            if (createRetainedState && destroyRetainedState
                && retainWebViews && releaseWebViews)
                retainedState = createRetainedState();
        }
    }

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    ~Module() { close(); }

    RuntimeNames createNames() const
    {
        std::array<char, 256> webview{};
        std::array<char, 256> delegate{};
        if (!createRuntimeClassNames
            || !createRuntimeClassNames(webview.data(), webview.size(), delegate.data(), delegate.size()))
            return {};

        return {webview.data(), delegate.data()};
    }

    [[nodiscard]] bool hasRetainedState() const noexcept
    {
        return retainedState != nullptr;
    }

    std::string retain(std::size_t count) const
    {
        std::array<char, 256> delegate{};
        if (!retainedState || !retainWebViews
            || !retainWebViews(retainedState, count, delegate.data(), delegate.size()))
            return {};
        return delegate.data();
    }

    bool exercise(std::size_t passes) const
    {
        return retainedState != nullptr
            && exerciseRetainedWebViews != nullptr
            && exerciseRetainedWebViews(retainedState, passes);
    }

    bool exchangeMessages(std::size_t messagesPerView) const
    {
        return retainedState != nullptr
            && exerciseRetainedMessages != nullptr
            && exerciseRetainedMessages(retainedState, messagesPerView);
    }

    void release() const
    {
        if (retainedState && releaseWebViews)
            releaseWebViews(retainedState);
    }

    void close()
    {
        if (handle) {
            // All C++ state created by the module must be destroyed while its
            // code and sanitizer metadata are still loaded. Keeping this state
            // outside module-static storage also avoids reinitialising a
            // poisoned DSO global after dlclose/dlopen under ASan.
            if (retainedState && releaseWebViews)
                releaseWebViews(retainedState);
            if (retainedState && destroyRetainedState)
                destroyRetainedState(retainedState);
            retainedState = nullptr;

            dlclose(handle);
            handle = nullptr;
            createRuntimeClassNames = nullptr;
            createRetainedState = nullptr;
            destroyRetainedState = nullptr;
            retainWebViews = nullptr;
            releaseWebViews = nullptr;
            exerciseRetainedWebViews = nullptr;
            exerciseRetainedMessages = nullptr;
            imageBase = nullptr;
        }
    }
};

bool isCHOCDelegateClass(const std::string& name)
{
    return name.rfind("CHOCWebViewDelegate_", 0) == 0;
}

bool classHasIMPFromImage(const std::string& name, const void* imageBase)
{
    auto cls = (Class) objc_getClass(name.c_str());
    if (cls == nullptr || imageBase == nullptr) return false;

    unsigned int count = 0;
    Method* methods = class_copyMethodList(cls, &count);
    bool found = false;

    for (unsigned int i = 0; i < count; ++i) {
        Dl_info info{};
        auto imp = reinterpret_cast<const void*>(method_getImplementation(methods[i]));
        if (imp != nullptr && dladdr(imp, &info) != 0 && info.dli_fbase == imageBase) {
            found = true;
            break;
        }
    }

    std::free(methods);
    return found;
}

bool classOwnIMPsAreOutsideImage(const std::string& name, const void* imageBase)
{
    auto cls = (Class) objc_getClass(name.c_str());
    if (cls == nullptr || imageBase == nullptr) return false;

    unsigned int count = 0;
    Method* methods = class_copyMethodList(cls, &count);
    bool safe = count != 0;

    for (unsigned int i = 0; safe && i < count; ++i) {
        Dl_info info{};
        auto imp = reinterpret_cast<const void*>(method_getImplementation(methods[i]));
        safe = imp != nullptr && dladdr(imp, &info) != 0 && info.dli_fbase != imageBase;
    }

    std::free(methods);
    return safe;
}

constexpr bool isAddressSanitizerBuild() noexcept
{
#if defined(__has_feature)
 #if __has_feature(address_sanitizer)
    return true;
 #endif
#endif
#if defined(__SANITIZE_ADDRESS__)
    return true;
#else
    return false;
#endif
}

} // namespace

TEST_CASE("plugin-safe CHOC modules use the system WKWebView class")
{
    Module moduleA{MODULE_A_PATH};
    Module moduleB{MODULE_B_PATH};

    REQUIRE(moduleA.handle != nullptr);
    REQUIRE(moduleB.handle != nullptr);
    REQUIRE(moduleA.createRuntimeClassNames != nullptr);
    REQUIRE(moduleB.createRuntimeClassNames != nullptr);

    const auto a = moduleA.createNames();
    const auto b = moduleB.createNames();

    REQUIRE_FALSE(a.webview.empty());
    REQUIRE_FALSE(b.webview.empty());
    REQUIRE_FALSE(a.delegate.empty());
    REQUIRE_FALSE(b.delegate.empty());

    CHECK(a.webview == "WKWebView");
    CHECK(b.webview == "WKWebView");

    REQUIRE(isCHOCDelegateClass(a.delegate));
    REQUIRE(isCHOCDelegateClass(b.delegate));
    CHECK(a.delegate != b.delegate);
}

TEST_CASE("unloading a CHOC module neutralises delegate IMPs and reuses the class on reload")
{
    Module moduleA{MODULE_A_PATH};
    Module moduleB{MODULE_B_PATH};

    REQUIRE(moduleA.handle != nullptr);
    REQUIRE(moduleB.handle != nullptr);
    REQUIRE(moduleA.imageBase != nullptr);
    REQUIRE(moduleB.imageBase != nullptr);

    const auto a = moduleA.createNames();
    const auto b = moduleB.createNames();

    REQUIRE(a.webview == "WKWebView");
    REQUIRE(b.webview == "WKWebView");
    REQUIRE(isCHOCDelegateClass(a.delegate));
    REQUIRE(isCHOCDelegateClass(b.delegate));
    REQUIRE(a.delegate != b.delegate);

    CHECK(objc_getClass(a.delegate.c_str()) != nullptr);
    CHECK(objc_getClass(b.delegate.c_str()) != nullptr);
    CHECK(classHasIMPFromImage(a.delegate, moduleA.imageBase));
    CHECK(classHasIMPFromImage(b.delegate, moduleB.imageBase));

    const auto unloadedImageA = moduleA.imageBase;
    moduleA.close();

    // The Objective-C runtime may retain the class metadata for process lifetime,
    // but no method may continue pointing into the unloaded plug-in image.
    CHECK(objc_getClass(a.delegate.c_str()) != nullptr);
    CHECK(classOwnIMPsAreOutsideImage(a.delegate, unloadedImageA));

    const auto bAfter = moduleB.createNames();
    CHECK(bAfter.webview == b.webview);
    CHECK(bAfter.delegate == b.delegate);
    CHECK(classHasIMPFromImage(b.delegate, moduleB.imageBase));

    Module reloadedA{MODULE_A_PATH};
    REQUIRE(reloadedA.handle != nullptr);
    REQUIRE(reloadedA.imageBase != nullptr);

    const auto aReloaded = reloadedA.createNames();
    CHECK(aReloaded.webview == a.webview);
    CHECK(aReloaded.delegate == a.delegate);
    CHECK(classHasIMPFromImage(a.delegate, reloadedA.imageBase));

    const auto reloadedImageA = reloadedA.imageBase;
    reloadedA.close();
    CHECK(classOwnIMPsAreOutsideImage(a.delegate, reloadedImageA));
}

TEST_CASE("32 live WebViews remain interactive when a peer module unloads")
{
    constexpr std::size_t viewsPerModule = 16;

    Module moduleA{MODULE_A_PATH};
    Module moduleB{MODULE_B_PATH};

    REQUIRE(moduleA.handle != nullptr);
    REQUIRE(moduleB.handle != nullptr);
    REQUIRE(moduleA.hasRetainedState());
    REQUIRE(moduleB.hasRetainedState());
    REQUIRE(moduleA.exerciseRetainedWebViews != nullptr);
    REQUIRE(moduleB.exerciseRetainedWebViews != nullptr);

    const auto delegateA = moduleA.retain(viewsPerModule);
    const auto delegateB = moduleB.retain(viewsPerModule);
    REQUIRE(isCHOCDelegateClass(delegateA));
    REQUIRE(isCHOCDelegateClass(delegateB));

    CHECK(moduleA.exercise(2));
    CHECK(moduleB.exercise(2));

    const auto unloadedImageA = moduleA.imageBase;
    moduleA.close();
    CHECK(classOwnIMPsAreOutsideImage(delegateA, unloadedImageA));

    // This must do real work in every still-live B WebView, not merely inspect
    // class metadata, after A's code image has gone away.
    CHECK(moduleB.exercise(2));
}

TEST_CASE("32 live WebViews remain isolated across repeated module unload and reload")
{
    constexpr std::size_t viewsPerModule = 16;
    // CI #318 attempt 1 exposed an ASan global-buffer-overflow while a module
    // was reloaded and its retained-view fixture storage was reconstructed.
    // Exercise enough unload/reload cycles to make that stale-storage failure a
    // deterministic release gate instead of relying on a single rerun.
    constexpr int cycles = 20;

    std::string stableDelegateA;
    std::string stableDelegateB;

    for (int cycle = 0; cycle < cycles; ++cycle) {
        CAPTURE(cycle);

        Module moduleA{MODULE_A_PATH};
        Module moduleB{MODULE_B_PATH};

        REQUIRE(moduleA.handle != nullptr);
        REQUIRE(moduleB.handle != nullptr);
        REQUIRE(moduleA.imageBase != nullptr);
        REQUIRE(moduleB.imageBase != nullptr);
        REQUIRE(moduleA.retainWebViews != nullptr);
        REQUIRE(moduleA.releaseWebViews != nullptr);
        REQUIRE(moduleB.retainWebViews != nullptr);
        REQUIRE(moduleB.releaseWebViews != nullptr);
        REQUIRE(moduleA.exerciseRetainedMessages != nullptr);
        REQUIRE(moduleB.exerciseRetainedMessages != nullptr);
        REQUIRE(moduleA.hasRetainedState());
        REQUIRE(moduleB.hasRetainedState());

        using ExerciseRetainedHostLifecycleFn = bool (*)(void*);
        auto exerciseHostLifecycleA = reinterpret_cast<ExerciseRetainedHostLifecycleFn>(
            dlsym(moduleA.handle, "webview_gui_test_exercise_retained_host_lifecycle"));
        auto exerciseHostLifecycleB = reinterpret_cast<ExerciseRetainedHostLifecycleFn>(
            dlsym(moduleB.handle, "webview_gui_test_exercise_retained_host_lifecycle"));
        REQUIRE(exerciseHostLifecycleA != nullptr);
        REQUIRE(exerciseHostLifecycleB != nullptr);

        const auto delegateA = moduleA.retain(viewsPerModule);
        const auto delegateB = moduleB.retain(viewsPerModule);

        REQUIRE(isCHOCDelegateClass(delegateA));
        REQUIRE(isCHOCDelegateClass(delegateB));
        CHECK(delegateA != delegateB);
        CHECK(classHasIMPFromImage(delegateA, moduleA.imageBase));
        CHECK(classHasIMPFromImage(delegateB, moduleB.imageBase));

        // Each outer cycle owns 32 simultaneous WebViews (16 per independently
        // loaded module). Across 20 unload/reload cycles this qualifies 640
        // editor lifecycles through the native host sequence before destruction.
        CHECK(exerciseHostLifecycleA(moduleA.retainedState));
        CHECK(exerciseHostLifecycleB(moduleB.retainedState));

        // The macOS 26 arm64 ASan runtime reports a reproducible
        // global-buffer-overflow inside libFontRegistry while WebKit launches a
        // content process from evaluateJavaScript(), before repository code is
        // reached. Keep ASan focused on the 32-view lifecycle/unload gate; the
        // dedicated public bridge-roundtrip test still executes the real WKWebView
        // JS bridge under ASan, while this multi-module exchange runs in the
        // normal macOS Debug job.
        if (!isAddressSanitizerBuild()) {
            // The same retained editors must exchange bridge messages before any
            // module unload. Different message counts make accidental cross-routing
            // observable rather than allowing equal aggregate counts to hide it.
            CHECK(moduleA.exchangeMessages(2));
            CHECK(moduleB.exchangeMessages(3));
        }

        if (cycle == 0) {
            stableDelegateA = delegateA;
            stableDelegateB = delegateB;
        } else {
            CHECK(delegateA == stableDelegateA);
            CHECK(delegateB == stableDelegateB);
        }

        moduleA.release();
        CHECK(classHasIMPFromImage(delegateA, moduleA.imageBase));
        const auto unloadedImageA = moduleA.imageBase;
        moduleA.close();
        CHECK(objc_getClass(delegateA.c_str()) != nullptr);
        CHECK(classOwnIMPsAreOutsideImage(delegateA, unloadedImageA));

        // B remains alive with 16 retained WKWebViews after A is gone. In the
        // normal Debug gate it must also continue exchanging bridge messages;
        // ASan keeps exercising the lifecycle/IMP isolation without triggering
        // the system libFontRegistry failure documented above.
        const auto bAfter = moduleB.createNames();
        CHECK(bAfter.webview == "WKWebView");
        CHECK(bAfter.delegate == delegateB);
        CHECK(classHasIMPFromImage(delegateB, moduleB.imageBase));
        if (!isAddressSanitizerBuild())
            CHECK(moduleB.exchangeMessages(2));

        moduleB.release();
        CHECK(classHasIMPFromImage(delegateB, moduleB.imageBase));
        const auto unloadedImageB = moduleB.imageBase;
        moduleB.close();
        CHECK(objc_getClass(delegateB.c_str()) != nullptr);
        CHECK(classOwnIMPsAreOutsideImage(delegateB, unloadedImageB));
    }
}
