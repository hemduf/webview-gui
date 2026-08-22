#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#if !defined(__APPLE__)
#error macOS-only test
#endif

#include <dlfcn.h>
#include <objc/runtime.h>

#include <string>

namespace {

using CreateClassNameFn = const char* (*)();

struct Module {
    void* handle = nullptr;
    CreateClassNameFn createClassName = nullptr;

    explicit Module(const char* path)
    {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle)
            createClassName = reinterpret_cast<CreateClassNameFn>(dlsym(handle, "webview_gui_test_create_webview_class_name"));
    }

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    ~Module()
    {
        close();
    }

    void close()
    {
        if (handle) {
            dlclose(handle);
            handle = nullptr;
            createClassName = nullptr;
        }
    }
};

bool isCHOCGeneratedClass(const std::string& name)
{
    return name.rfind("CHOCWebView_", 0) == 0
        || name.rfind("CHOCWebViewDelegate_", 0) == 0;
}

} // namespace

TEST_CASE("two independently linked CHOC modules create isolated WebView classes")
{
    Module moduleA{MODULE_A_PATH};
    Module moduleB{MODULE_B_PATH};

    REQUIRE(moduleA.handle != nullptr);
    REQUIRE(moduleB.handle != nullptr);
    REQUIRE(moduleA.createClassName != nullptr);
    REQUIRE(moduleB.createClassName != nullptr);

    const std::string classA = moduleA.createClassName();
    const std::string classB = moduleB.createClassName();

    REQUIRE_FALSE(classA.empty());
    REQUIRE_FALSE(classB.empty());

    // If CHOC uses generated Objective-C subclasses, independently linked
    // modules must never end up sharing the same generated class.
    if (isCHOCGeneratedClass(classA) || isCHOCGeneratedClass(classB))
        CHECK(classA != classB);
}

TEST_CASE("unloading one module leaves no CHOC-generated class owned by it")
{
    Module moduleA{MODULE_A_PATH};
    Module moduleB{MODULE_B_PATH};

    REQUIRE(moduleA.handle != nullptr);
    REQUIRE(moduleB.handle != nullptr);
    REQUIRE(moduleA.createClassName != nullptr);
    REQUIRE(moduleB.createClassName != nullptr);

    const std::string classA = moduleA.createClassName();
    const std::string classB = moduleB.createClassName();

    REQUIRE_FALSE(classA.empty());
    REQUIRE_FALSE(classB.empty());

    moduleA.close();

    // A vanilla system WKWebView class is expected to remain process-global.
    // A generated CHOC class whose IMPs were emitted into module A is not safe
    // to leave registered after that module is unloaded.
    if (isCHOCGeneratedClass(classA))
        CHECK(objc_getClass(classA.c_str()) == nullptr);

    // Module B must remain usable after A has been closed.
    const std::string classBAfter = moduleB.createClassName();
    CHECK(classBAfter == classB);
}
