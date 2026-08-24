#include <cstdio>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

struct PluginInfo {
    unsigned int abiVersion;
    int variant;
    int supportsNone;
    const char* webviewRevision;
    const char* chocRevision;
};

struct Snapshot {
    int variant = 0;
    bool supportsNone = false;
    std::string webviewRevision;
    std::string chocRevision;
};

using EntryPoint = const PluginInfo* (*)();

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

bool isHexDigit(char value)
{
    return (value >= '0' && value <= '9')
        || (value >= 'a' && value <= 'f')
        || (value >= 'A' && value <= 'F');
}

bool isFullGitSha(const char* value)
{
    if (!value)
        return false;

    for (int i = 0; i < 40; ++i) {
        if (value[i] == '\0' || !isHexDigit(value[i]))
            return false;
    }
    return value[40] == '\0';
}

bool readSnapshot(ModuleHandle handle, Snapshot& snapshot)
{
    const auto entry = reinterpret_cast<EntryPoint>(
        loadSymbol(handle, "webview_gui_plugin_test_entry"));
    if (!entry)
        return false;

    const auto* info = entry();
    if (!info || info->abiVersion != 1u
        || !isFullGitSha(info->webviewRevision)
        || !isFullGitSha(info->chocRevision))
        return false;

    snapshot.variant = info->variant;
    snapshot.supportsNone = info->supportsNone != 0;
    snapshot.webviewRevision = info->webviewRevision;
    snapshot.chocRevision = info->chocRevision;
    return true;
}

bool sameSnapshot(const Snapshot& a, const Snapshot& b)
{
    return a.variant == b.variant
        && a.supportsNone == b.supportsNone
        && a.webviewRevision == b.webviewRevision
        && a.chocRevision == b.chocRevision;
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

    Snapshot initialA;
    Snapshot initialB;
    if (!readSnapshot(moduleA, initialA) || !readSnapshot(moduleB, initialB)
        || initialA.variant != 1 || initialB.variant != 2
        || initialA.supportsNone || initialB.supportsNone) {
        std::fprintf(stderr, "module-local webview-gui configurations cross-routed\n");
        closeModule(moduleA);
        closeModule(moduleB);
        return 4;
    }

    if (initialA.webviewRevision == initialB.webviewRevision
        || initialA.chocRevision == initialB.chocRevision) {
        std::fprintf(stderr,
                     "qualification modules did not use distinct pinned webview-gui and CHOC revision SHAs\n");
        closeModule(moduleA);
        closeModule(moduleB);
        return 5;
    }

    closeModule(moduleA);
    moduleA = nullptr;

    Snapshot liveB;
    if (!readSnapshot(moduleB, liveB) || !sameSnapshot(initialB, liveB)) {
        std::fprintf(stderr, "module B changed after unloading module A\n");
        closeModule(moduleB);
        return 6;
    }

    moduleA = openModule(argv[1]);
    Snapshot reloadedA;
    Snapshot recheckedB;
    if (!moduleA
        || !readSnapshot(moduleA, reloadedA)
        || !readSnapshot(moduleB, recheckedB)
        || !sameSnapshot(initialA, reloadedA)
        || !sameSnapshot(initialB, recheckedB)) {
        std::fprintf(stderr, "module isolation failed after reloading module A\n");
        closeModule(moduleA);
        closeModule(moduleB);
        return 7;
    }

    closeModule(moduleA);
    closeModule(moduleB);
    return 0;
}
