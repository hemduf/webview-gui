#include "native_preset_storage.h"

#include "example_plugin_ids.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace presets = webview_gui::examples::presets;
namespace ids = webview_gui::examples::plugin_ids;

namespace {

struct TempTree {
    fs::path path;

    TempTree() {
        const auto stamp = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        path = fs::temp_directory_path() /
               ("webview-gui-preset-race-" + std::to_string(stamp));
        fs::remove_all(path);
        fs::create_directories(path);
    }

    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

presets::PresetDocument makeGainPreset(std::string name, double gainDb) {
    presets::PresetDocument document;
    document.metadata.targetPluginId = ids::kGainPluginId;
    document.metadata.name = std::move(name);
    document.parameters = {
        {0x1000u, gainDb},
        {0x1001u, 0.0},
    };
    return document;
}

std::string readFile(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

struct RootSwapContext {
    fs::path base;
    fs::path outside;
    fs::path movedNamespace;
    bool swapped = false;
};

bool swapNamespaceAfterPin(presets::NativePresetWriteStage stage,
                           void *userData) noexcept {
    if (stage != presets::NativePresetWriteStage::AfterRootPinned)
        return false;
    auto *context = static_cast<RootSwapContext *>(userData);
    if (!context || context->swapped)
        return false;

    try {
        const auto namespacePath = context->base / "webview-gui";
        context->movedNamespace = context->base / "webview-gui-pinned";
        fs::rename(namespacePath, context->movedNamespace);

        std::error_code error;
        fs::create_directory_symlink(context->outside, namespacePath, error);
        if (error) {
            fs::rename(context->movedNamespace, namespacePath);
            context->movedNamespace.clear();
            return false;
        }
        context->swapped = true;
    } catch (...) {
        return false;
    }
    // Mutation is the test action; returning false lets the storage operation
    // continue against the already-pinned directory capability.
    return false;
}

struct CleanupFailureContext {
    bool invoked = false;
};

bool failTemporaryCleanup(presets::NativePresetWriteStage stage,
                          void *userData) noexcept {
    auto *context = static_cast<CleanupFailureContext *>(userData);
    if (stage == presets::NativePresetWriteStage::BeforeTemporaryCleanup) {
        if (context)
            context->invoked = true;
        return true;
    }
    return false;
}

} // namespace

int main() {
    const auto document = makeGainPreset("Race Safe", -9.0);
    const auto canonical = presets::serializePresetDocument(document);
    assert(canonical.ok());

    // After the plug-in root is pinned, replacing an intermediate path component
    // with an outside symlink/reparse path cannot redirect the pending write.
    {
        TempTree base;
        TempTree outside;
        fs::create_directories(outside.path / "presets" / ids::kGainPluginId);

        presets::NativePresetStorage bootstrap{base.path, ids::kGainPluginId};
        assert(bootstrap.ensureReady().ok());

        RootSwapContext context{base.path, outside.path, {}, false};
        presets::NativePresetStorageOptions options;
        options.shouldFailWrite = &swapNamespaceAfterPin;
        options.faultUserData = &context;
        presets::NativePresetStorage storage{base.path, ids::kGainPluginId, options};

        const auto saved = storage.saveAs("race.wvpreset", document, false);
        assert(saved.ok());

        if (context.swapped) {
            const auto pinnedDestination =
                context.movedNamespace / "presets" / ids::kGainPluginId /
                "race.wvpreset";
            const auto outsideDestination =
                outside.path / "presets" / ids::kGainPluginId / "race.wvpreset";
            assert(fs::is_regular_file(pinnedDestination));
            assert(readFile(pinnedDestination) == canonical.bytes);
            assert(!fs::exists(outsideDestination));

            std::error_code ignored;
            fs::remove(base.path / "webview-gui", ignored);
            fs::rename(context.movedNamespace, base.path / "webview-gui", ignored);
        } else {
            // Some Windows runners do not grant symbolic-link creation. The
            // handle-relative implementation is still compiled/used there; only
            // the adversarial filesystem mutation fixture is unavailable.
            assert(fs::is_regular_file(storage.root() / "race.wvpreset"));
        }
    }

#if !defined(_WIN32)
    // For POSIX no-clobber Save As, successful linkat() is the transaction's
    // commit point. A later failure to remove the private temp name must not
    // roll back the committed destination or turn success into failure.
    {
        TempTree base;
        CleanupFailureContext context;
        presets::NativePresetStorageOptions options;
        options.shouldFailWrite = &failTemporaryCleanup;
        options.faultUserData = &context;
        presets::NativePresetStorage storage{base.path, ids::kGainPluginId, options};

        const auto saved = storage.saveAs("committed.wvpreset", document, false);
        assert(saved.ok());
        assert(context.invoked);
        assert(fs::is_regular_file(storage.root() / "committed.wvpreset"));
        assert(readFile(storage.root() / "committed.wvpreset") == canonical.bytes);

        bool foundTemporary = false;
        for (const auto &entry : fs::directory_iterator(storage.root())) {
            if (entry.path().filename().string().find(".tmp-") == std::string::npos)
                continue;
            foundTemporary = true;
            std::error_code ignored;
            fs::remove(entry.path(), ignored);
        }
        assert(foundTemporary);
    }
#endif

    return 0;
}
