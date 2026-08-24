#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__APPLE__)
#error macOS-only test
#endif

#include <dlfcn.h>
#include <objc/runtime.h>

#include <array>
#include <string>

namespace {

using CreateRuntimeClassNamesFn = bool (*)(char*, std::size_t, char*, std::size_t);
using RetainWebViewsFn = bool (*)(std::size_t, char*, std::size_t);
using ReleaseWebViewsFn = void (*)();

struct RuntimeNames {
    std::string webview;
    std::string delegate;
};

struct Module {
    void* handle = nullptr;
    CreateRuntimeClassNamesFn createRuntimeClassNames = nullptr;
    RetainWebViewsFn retainWebViews = nullptr;
    ReleaseWebViewsFn releaseWebViews = nullptr;

    explicit Module(const char* path)
    {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle) {
            createRuntimeClassNames = reinterpret_cast<CreateRuntimeClassNamesFn>(
                dlsym(handle, "webview_gui_test_create_runtime_class_names"));
            retainWebViews = reinterpret_cast<RetainWebViewsFn>(
                dlsym(handle, "webview_gui_test_retain_webviews"));
            releaseWebViews = reinterpret_cast<ReleaseWebViewsFn>(
                dlsym(handle, "webview_gui_test_release_webviews"));
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

    std::string retain(std::size_t count) const
    {
        std::array<char, 256> delegate{};
        if (!retainWebViews || !retainWebViews(count, delegate.data(), delegate.size()))
            return {};
        return delegate.data();
    }

    void release() const
    {
        if (releaseWebViews)
            releaseWebViews();
    }

    void close()
    {
        if (handle) {
            dlclose(handle);
            handle = nullptr;
            createRuntimeClassNames = nullptr;
            retainWebViews = nullptr;
            releaseWebViews = nullptr;
        }
    }
};

bool isCHOCDelegateClass(const std::string& name)
{
    return name.rfind("CHOCWebViewDelegate_", 0) == 0;
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

TEST_CASE("the last CHOC WebView disposes its delegate class before module unload")
{
    Module moduleA{MODULE_A_PATH};
    Module moduleB{MODULE_B_PATH};

    REQUIRE(moduleA.handle != nullptr);
    REQUIRE(moduleB.handle != nullptr);

    const auto a = moduleA.createNames();
    const auto b = moduleB.createNames();

    REQUIRE(a.webview == "WKWebView");
    REQUIRE(b.webview == "WKWebView");
    REQUIRE(isCHOCDelegateClass(a.delegate));
    REQUIRE(isCHOCDelegateClass(b.delegate));

    CHECK(objc_getClass("WKWebView") != nullptr);

    // createNames() destroys its temporary WebView before it returns. A dynamic
    // delegate class must therefore already be gone while the module is still
    // loaded; waiting for dlclose makes objc_disposeClassPair race WebKit/ASan.
    CHECK(objc_getClass(a.delegate.c_str()) == nullptr);
    CHECK(objc_getClass(b.delegate.c_str()) == nullptr);

    moduleA.close();

    CHECK(objc_getClass("WKWebView") != nullptr);
    CHECK(objc_getClass(a.delegate.c_str()) == nullptr);

    const auto bAfter = moduleB.createNames();
    CHECK(bAfter.webview == b.webview);
    CHECK(bAfter.delegate != b.delegate);
    CHECK(objc_getClass(bAfter.delegate.c_str()) == nullptr);
}

TEST_CASE("32 live WebViews remain isolated while one module unloads and reloads")
{
    constexpr std::size_t viewsPerModule = 16;
    constexpr int cycles = 5;

    for (int cycle = 0; cycle < cycles; ++cycle) {
        CAPTURE(cycle);

        std::string delegateA;
        std::string delegateB;

        {
            Module moduleA{MODULE_A_PATH};
            Module moduleB{MODULE_B_PATH};

            REQUIRE(moduleA.handle != nullptr);
            REQUIRE(moduleB.handle != nullptr);
            REQUIRE(moduleA.retainWebViews != nullptr);
            REQUIRE(moduleA.releaseWebViews != nullptr);
            REQUIRE(moduleB.retainWebViews != nullptr);
            REQUIRE(moduleB.releaseWebViews != nullptr);

            delegateA = moduleA.retain(viewsPerModule);
            delegateB = moduleB.retain(viewsPerModule);

            REQUIRE(isCHOCDelegateClass(delegateA));
            REQUIRE(isCHOCDelegateClass(delegateB));
            CHECK(delegateA != delegateB);
            CHECK(objc_getClass(delegateA.c_str()) != nullptr);
            CHECK(objc_getClass(delegateB.c_str()) != nullptr);

            moduleA.release();
            CHECK(objc_getClass(delegateA.c_str()) == nullptr);
            moduleA.close();
            CHECK(objc_getClass(delegateA.c_str()) == nullptr);

            // B remains alive with 16 retained WKWebViews and must still be usable.
            const auto bAfter = moduleB.createNames();
            CHECK(bAfter.webview == "WKWebView");
            CHECK(bAfter.delegate == delegateB);
            CHECK(objc_getClass(delegateB.c_str()) != nullptr);

            moduleB.release();
            CHECK(objc_getClass(delegateB.c_str()) == nullptr);
            moduleB.close();
        }

        CHECK(objc_getClass(delegateA.c_str()) == nullptr);
        CHECK(objc_getClass(delegateB.c_str()) == nullptr);
    }
}
