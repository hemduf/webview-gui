#include "native_preset_storage.h"

#include "example_plugin_ids.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

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
               ("webview-gui-preset-storage-" + std::to_string(stamp));
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
    document.metadata.creator = "webview-gui";
    document.metadata.description = "Native user preset fixture";
    document.metadata.tags = {"user"};
    document.metadata.features = {"audio-effect", "utility"};
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

bool containsTemporaryFile(const fs::path &root) {
    std::error_code error;
    for (const auto &entry : fs::directory_iterator(root, error)) {
        if (error)
            return true;
        if (entry.path().filename().string().find(".tmp-") != std::string::npos)
            return true;
    }
    return false;
}

struct FailAtStage {
    presets::NativePresetWriteStage stage =
        presets::NativePresetWriteStage::AfterPartialWrite;
};

bool failWriteStage(presets::NativePresetWriteStage stage, void *userData) noexcept {
    auto *failure = static_cast<FailAtStage *>(userData);
    return failure != nullptr && stage == failure->stage;
}

bool hasDiagnostic(const presets::NativePresetListResult &listed,
                   presets::NativePresetStorageError error,
                   const fs::path &path) {
    return std::any_of(
        listed.diagnostics.begin(), listed.diagnostics.end(),
        [&](const auto &diagnostic) {
            return diagnostic.status.error == error &&
                   diagnostic.status.path == path;
        });
}

} // namespace

int main() {
    TempTree tree;

    presets::NativePresetStorage gainStorage{tree.path, ids::kGainPluginId};
    presets::NativePresetStorage polyStorage{tree.path, ids::kPolySynthPluginId};

    assert(gainStorage.root() != polyStorage.root());
    assert(gainStorage.root().filename() == ids::kGainPluginId);
    assert(polyStorage.root().filename() == ids::kPolySynthPluginId);

    const auto ready = gainStorage.ensureReady();
    assert(ready.ok());
    assert(fs::is_directory(gainStorage.root()));

    const auto original = makeGainPreset("Same Display Name", -6.0);
    const auto firstSave = gainStorage.saveNew(original);
    assert(firstSave.ok());
    assert(!firstSave.identity.empty());
    assert(firstSave.path.extension() == ".wvpreset");
    assert(fs::is_regular_file(firstSave.path));
    assert(firstSave.path.parent_path() == gainStorage.root());

    const auto canonicalOriginal = presets::serializePresetDocument(original);
    assert(canonicalOriginal.ok());
    assert(readFile(firstSave.path) == canonicalOriginal.bytes);

    // Display metadata is not storage identity: duplicate names create distinct files.
    const auto secondSave = gainStorage.saveNew(original);
    assert(secondSave.ok());
    assert(secondSave.identity != firstSave.identity);
    assert(secondSave.path != firstSave.path);
    assert(fs::is_regular_file(secondSave.path));

    // Explicit Save As is no-clobber unless overwrite is requested.
    const auto explicitSave = gainStorage.saveAs("explicit.wvpreset", original);
    assert(explicitSave.ok());
    assert(explicitSave.identity == "explicit.wvpreset");
    const auto explicitOriginalBytes = readFile(explicitSave.path);

    auto explicitReplacement = makeGainPreset("Explicit Replacement", 4.0);
    const auto refusedOverwrite =
        gainStorage.saveAs("explicit.wvpreset", explicitReplacement, false);
    assert(!refusedOverwrite.ok());
    assert(refusedOverwrite.status.error ==
           presets::NativePresetStorageError::AlreadyExists);
    assert(readFile(explicitSave.path) == explicitOriginalBytes);

    const auto allowedOverwrite =
        gainStorage.saveAs("explicit.wvpreset", explicitReplacement, true);
    assert(allowedOverwrite.ok());
    assert(allowedOverwrite.identity == "explicit.wvpreset");
    const auto explicitReplacementBytes =
        presets::serializePresetDocument(explicitReplacement);
    assert(explicitReplacementBytes.ok());
    assert(readFile(explicitSave.path) == explicitReplacementBytes.bytes);

    // Save As names are filenames, never paths. Traversal-like, encoded and
    // platform-specific separator/device tricks fail before filesystem access.
    const std::string_view invalidSaveNames[] = {
        "../escape.wvpreset",
        "..\\escape.wvpreset",
        "/tmp/escape.wvpreset",
        "C:\\temp\\escape.wvpreset",
        "%2e%2e%2fescape.wvpreset",
        "folder%5cescape.wvpreset",
        "NUL.wvpreset",
        "notes.txt",
    };
    for (const auto invalidName : invalidSaveNames) {
        const auto invalidSave = gainStorage.saveAs(invalidName, original);
        assert(!invalidSave.ok());
        assert(invalidSave.status.error ==
               presets::NativePresetStorageError::InvalidName);
    }

    // Enumeration is deterministic and only includes real .wvpreset regular files.
    {
        std::ofstream(gainStorage.root() / "notes.txt") << "ignored";
        std::ofstream(gainStorage.root() / "backup.wvpreset.bak") << "ignored";
        fs::create_directories(gainStorage.root() / "directory.wvpreset");
    }

    const auto listed = gainStorage.list();
    assert(listed.ok());
    assert(listed.entries.size() == 3u);
    assert(std::is_sorted(
        listed.entries.begin(), listed.entries.end(),
        [](const auto &a, const auto &b) { return a.identity < b.identity; }));
    for (const auto &entry : listed.entries) {
        assert(entry.path.extension() == ".wvpreset");
        assert(entry.metadata.targetPluginId == ids::kGainPluginId);
    }

    // Full load uses the production parser and expected target scope.
    const auto loaded = gainStorage.load(firstSave.identity);
    assert(loaded.ok());
    assert(loaded.document.has_value());
    assert(loaded.document->metadata.targetPluginId == ids::kGainPluginId);
    assert(loaded.document->parameters.size() == 2u);
    assert(loaded.document->parameters[0].stableParameterId == 0x1000u);
    assert(loaded.document->parameters[0].value == -6.0);

    // Successful replace preserves storage identity, writes canonical bytes and
    // leaves no temporary file behind.
    auto replacement = makeGainPreset("Renamed Display Metadata", 3.0);
    const auto replaced = gainStorage.replace(firstSave.identity, replacement);
    assert(replaced.ok());
    assert(replaced.identity == firstSave.identity);
    assert(replaced.path == firstSave.path);
    const auto canonicalReplacement = presets::serializePresetDocument(replacement);
    assert(canonicalReplacement.ok());
    assert(readFile(firstSave.path) == canonicalReplacement.bytes);
    assert(!containsTemporaryFile(gainStorage.root()));

    // Simulated mid-write failure is deterministic and failure-atomic: the old
    // preset remains byte-for-byte intact and the temporary file is cleaned.
    const auto bytesBeforeFailure = readFile(firstSave.path);
    FailAtStage failure;
    presets::NativePresetStorageOptions faultOptions;
    faultOptions.shouldFailWrite = &failWriteStage;
    faultOptions.faultUserData = &failure;
    presets::NativePresetStorage faultingStorage{
        tree.path, ids::kGainPluginId, faultOptions};

    auto failedReplacement = makeGainPreset("Should Not Commit", 9.0);
    const auto failedWrite =
        faultingStorage.replace(firstSave.identity, failedReplacement);
    assert(!failedWrite.ok());
    assert(failedWrite.status.error == presets::NativePresetStorageError::WriteFailed);
    assert(!failedWrite.status.path.empty());
    assert(readFile(firstSave.path) == bytesBeforeFailure);
    assert(!containsTemporaryFile(gainStorage.root()));

    // Failure after a fully written/closed temp but before rename is also atomic.
    failure.stage = presets::NativePresetWriteStage::BeforeReplace;
    const auto failedBeforeReplace =
        faultingStorage.replace(firstSave.identity, failedReplacement);
    assert(!failedBeforeReplace.ok());
    assert(failedBeforeReplace.status.error ==
           presets::NativePresetStorageError::ReplaceFailed);
    assert(readFile(firstSave.path) == bytesBeforeFailure);
    assert(!containsTemporaryFile(gainStorage.root()));

    // Serialization failure never touches the previous destination.
    auto invalidDocument = failedReplacement;
    invalidDocument.parameters[0].value =
        std::numeric_limits<double>::infinity();
    const auto failedSerialize =
        gainStorage.saveAs(firstSave.identity, invalidDocument, true);
    assert(!failedSerialize.ok());
    assert(failedSerialize.status.error ==
           presets::NativePresetStorageError::SerializeFailed);
    assert(readFile(firstSave.path) == bytesBeforeFailure);

    // A non-creatable scoped root reports context rather than throwing.
    TempTree blockedTree;
    fs::create_directories(blockedTree.path / "webview-gui");
    fs::remove_all(blockedTree.path / "webview-gui");
    {
        std::ofstream blocker(blockedTree.path / "webview-gui");
        blocker << "not a directory";
    }
    presets::NativePresetStorage blockedStorage{
        blockedTree.path, ids::kGainPluginId};
    const auto blockedReady = blockedStorage.ensureReady();
    assert(!blockedReady.ok());
    assert(blockedReady.error ==
           presets::NativePresetStorageError::CreateDirectoryFailed);
    assert(!blockedReady.path.empty());

    // Scope enforcement and identity validation prevent target crossover/traversal.
    auto wrongTarget = original;
    wrongTarget.metadata.targetPluginId = ids::kPolySynthPluginId;
    const auto wrongSave = gainStorage.saveNew(wrongTarget);
    assert(!wrongSave.ok());
    assert(wrongSave.status.error ==
           presets::NativePresetStorageError::WrongTargetPlugin);

    const std::string_view invalidLoadIdentities[] = {
        "../escape.wvpreset",
        "..\\escape.wvpreset",
        "/tmp/escape.wvpreset",
        "C:\\temp\\escape.wvpreset",
        "%2e%2e%2fescape.wvpreset",
        "notes.txt",
    };
    for (const auto invalidIdentity : invalidLoadIdentities) {
        const auto traversal = gainStorage.load(invalidIdentity);
        assert(!traversal.ok());
        assert(traversal.status.error ==
               presets::NativePresetStorageError::InvalidIdentity);
    }

    // A preset symlink/reparse-point is never followed outside the scoped root.
    TempTree outsideTree;
    const auto outsidePreset = outsideTree.path / "outside.wvpreset";
    {
        std::ofstream outside(outsidePreset, std::ios::binary);
        outside << canonicalOriginal.bytes;
    }
    const auto linkedPreset = gainStorage.root() / "linked.wvpreset";
    std::error_code symlinkError;
    fs::create_symlink(outsidePreset, linkedPreset, symlinkError);
    if (!symlinkError) {
        const auto linkedLoad = gainStorage.load("linked.wvpreset");
        assert(!linkedLoad.ok());
        assert(linkedLoad.status.error ==
               presets::NativePresetStorageError::OutsideRoot);

        const auto listedWithSymlink = gainStorage.list();
        assert(listedWithSymlink.ok());
        assert(hasDiagnostic(listedWithSymlink,
                             presets::NativePresetStorageError::OutsideRoot,
                             linkedPreset));

        const auto linkedRemove = gainStorage.remove("linked.wvpreset");
        assert(!linkedRemove.ok());
        assert(linkedRemove.error == presets::NativePresetStorageError::OutsideRoot);
        std::error_code ignored;
        fs::remove(linkedPreset, ignored);
    }

    // An intermediate symlink may never be followed. Depending on the native
    // no-follow primitive, the OS can report it either as an explicit escape or
    // as an invalid/non-directory component; both are deterministic safe rejects.
    TempTree redirectedBase;
    TempTree redirectedTarget;
    std::error_code directorySymlinkError;
    fs::create_directory_symlink(redirectedTarget.path,
                                 redirectedBase.path / "webview-gui",
                                 directorySymlinkError);
    if (!directorySymlinkError) {
        presets::NativePresetStorage redirectedStorage{
            redirectedBase.path, ids::kGainPluginId};
        const auto redirectedReady = redirectedStorage.ensureReady();
        assert(!redirectedReady.ok());
        assert(redirectedReady.error == presets::NativePresetStorageError::OutsideRoot ||
               redirectedReady.error ==
                   presets::NativePresetStorageError::CreateDirectoryFailed);
    }

    // Malformed real .wvpreset files return the production codec error context.
    const auto malformedPath = gainStorage.root() / "broken.wvpreset";
    {
        std::ofstream malformed(malformedPath, std::ios::binary);
        malformed << "not-a-wvpreset";
    }
    const auto malformedLoad = gainStorage.load("broken.wvpreset");
    assert(!malformedLoad.ok());
    assert(malformedLoad.status.error ==
           presets::NativePresetStorageError::ParseFailed);
    assert(malformedLoad.status.codecError != presets::PresetCodecError::None);
    assert(malformedLoad.status.path == malformedPath);

    // Oversized files are rejected before allocation/read and surfaced as list diagnostics.
    const auto oversizedPath = gainStorage.root() / "oversized.wvpreset";
    {
        std::ofstream oversized(oversizedPath, std::ios::binary);
        std::string chunk(4096u, 'x');
        std::size_t remaining = presets::kMaxNativePresetFileBytes + 1u;
        while (remaining != 0u) {
            const auto count = std::min<std::size_t>(remaining, chunk.size());
            oversized.write(chunk.data(), static_cast<std::streamsize>(count));
            remaining -= count;
        }
    }
    const auto oversizedLoad = gainStorage.load("oversized.wvpreset");
    assert(!oversizedLoad.ok());
    assert(oversizedLoad.status.error ==
           presets::NativePresetStorageError::InputTooLarge);
    assert(oversizedLoad.status.path == oversizedPath);

    const auto listedWithInvalidFiles = gainStorage.list();
    assert(listedWithInvalidFiles.ok());
    assert(hasDiagnostic(listedWithInvalidFiles,
                         presets::NativePresetStorageError::ParseFailed,
                         malformedPath));
    assert(hasDiagnostic(listedWithInvalidFiles,
                         presets::NativePresetStorageError::InputTooLarge,
                         oversizedPath));

    // Delete supports downstream browser semantics and is deterministic.
    const auto removed = gainStorage.remove(explicitSave.identity);
    assert(removed.ok());
    assert(!fs::exists(explicitSave.path));
    const auto removeAgain = gainStorage.remove(explicitSave.identity);
    assert(!removeAgain.ok());
    assert(removeAgain.error == presets::NativePresetStorageError::NotFound);

    return 0;
}
