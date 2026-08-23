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

struct RuntimeNames {
    std::string webview;
    std::string delegate;
};

struct Module {
    void* handle = nullptr;
    CreateRuntimeClassNamesFn createRuntimeClassNames = nullptr;

    explicit Module(const char* path)
    {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle)
            createRuntimeClassNames = reinterpret_cast<CreateRuntimeClassNamesFn>(
                dlsym(handle, "webview_gui_test_create_runtime_class_names"));
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

    void close()
    {
        if (handle) {
            dlclose(handle);
            handle = nullptr;
            createRuntimeClassNames = nullptr;
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

TEST_CASE("unloading one CHOC module disposes its generated delegate class")
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

    // Apple's WKWebView is process-owned and remains valid. The CHOC delegate
    // class contains IMPs emitted into module A, so it must be removed before
    // that module can be considered unload-safe.
    CHECK(objc_getClass("WKWebView") != nullptr);
    CHECK(objc_getClass(a.delegate.c_str()) != nullptr);

    moduleA.close();

    CHECK(objc_getClass("WKWebView") != nullptr);
    CHECK(objc_getClass(a.delegate.c_str()) == nullptr);

    // Module B remains loaded and must still create a WebView using its own
    // delegate class after A has been unloaded.
    const auto bAfter = moduleB.createNames();
    CHECK(bAfter.webview == b.webview);
    CHECK(bAfter.delegate == b.delegate);
    CHECK(objc_getClass(b.delegate.c_str()) != nullptr);
}
