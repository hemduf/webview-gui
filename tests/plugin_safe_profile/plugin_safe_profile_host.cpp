#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using EntryPoint = int (*)();

#if defined(_WIN32)
using ModuleHandle = HMODULE;

ModuleHandle openModule(const char* path)
{
    return path ? LoadLibraryA(path) : nullptr;
}

void closeModule(ModuleHandle handle)
{
    if (handle)
        FreeLibrary(handle);
}

void* loadSymbol(ModuleHandle handle, const char* name)
{
    return handle ? reinterpret_cast<void*>(GetProcAddress(handle, name)) : nullptr;
}
#else
using ModuleHandle = void*;

ModuleHandle openModule(const char* path)
{
    return path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : nullptr;
}

void closeModule(ModuleHandle handle)
{
    if (handle)
        dlclose(handle);
}

void* loadSymbol(ModuleHandle handle, const char* name)
{
    return handle ? dlsym(handle, name) : nullptr;
}
#endif

EntryPoint entryPoint(ModuleHandle handle)
{
    return reinterpret_cast<EntryPoint>(loadSymbol(handle, "webview_gui_plugin_test_entry"));
}

bool expectEntry(ModuleHandle handle, int expected)
{
    const auto entry = entryPoint(handle);
    return entry != nullptr && entry() == expected;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "expected module A and module B paths\n");
        return 2;
    }

    auto moduleA = openModule(argv[1]);
    auto moduleB = openModule(argv[2]);
    if (!moduleA || !moduleB) {
        std::fprintf(stderr, "failed to load both plug-in qualification modules\n");
        closeModule(moduleA);
        closeModule(moduleB);
        return 3;
    }

    if (!expectEntry(moduleA, 1) || !expectEntry(moduleB, 2)) {
        std::fprintf(stderr, "module-local webview-gui configurations cross-routed\n");
        closeModule(moduleA);
        closeModule(moduleB);
        return 4;
    }

    closeModule(moduleA);
    moduleA = nullptr;

    if (!expectEntry(moduleB, 2)) {
        std::fprintf(stderr, "module B changed after unloading module A\n");
        closeModule(moduleB);
        return 5;
    }

    moduleA = openModule(argv[1]);
    if (!moduleA || !expectEntry(moduleA, 1) || !expectEntry(moduleB, 2)) {
        std::fprintf(stderr, "module isolation failed after reloading module A\n");
        closeModule(moduleA);
        closeModule(moduleB);
        return 6;
    }

    closeModule(moduleA);
    closeModule(moduleB);
    return 0;
}
