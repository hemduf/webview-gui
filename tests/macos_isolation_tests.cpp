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

TEST_CASE("32 live WebViews remain isolated across repeated module unload and reload")
{
    constexpr std::size_t viewsPerModule = 16;
    constexpr int cycles = 5;

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

        const auto delegateA = moduleA.retain(viewsPerModule);
        const auto delegateB = moduleB.retain(viewsPerModule);

        REQUIRE(isCHOCDelegateClass(delegateA));
        REQUIRE(isCHOCDelegateClass(delegateB));
        CHECK(delegateA != delegateB);
        CHECK(classHasIMPFromImage(delegateA, moduleA.imageBase));
        CHECK(classHasIMPFromImage(delegateB, moduleB.imageBase));

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

        // B remains alive with 16 retained WKWebViews and must still be usable.
        const auto bAfter = moduleB.createNames();
        CHECK(bAfter.webview == "WKWebView");
        CHECK(bAfter.delegate == delegateB);
        CHECK(classHasIMPFromImage(delegateB, moduleB.imageBase));

        moduleB.release();
        CHECK(classHasIMPFromImage(delegateB, moduleB.imageBase));
        const auto unloadedImageB = moduleB.imageBase;
        moduleB.close();
        CHECK(objc_getClass(delegateB.c_str()) != nullptr);
        CHECK(classOwnIMPsAreOutsideImage(delegateB, unloadedImageB));
    }
}
