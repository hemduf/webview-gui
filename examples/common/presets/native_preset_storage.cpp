#include "native_preset_storage.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

namespace webview_gui::examples::presets {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kStorageNamespace = "webview-gui";
constexpr std::string_view kPresetDirectoryName = "presets";
constexpr std::string_view kPresetSuffix = ".wvpreset";
constexpr std::size_t kMaxSaveIdentityAttempts = 10000u;

std::atomic<std::uint64_t> gTemporaryFileCounter{0u};

[[nodiscard]] NativePresetStorageStatus makeStatus(
    NativePresetStorageError error = NativePresetStorageError::None,
    fs::path path = {},
    std::error_code systemError = {},
    PresetCodecError codecError = PresetCodecError::None) noexcept {
    NativePresetStorageStatus status;
    status.error = error;
    status.path = std::move(path);
    status.systemError = systemError;
    status.codecError = codecError;
    return status;
}

[[nodiscard]] bool validTargetPluginId(std::string_view id) noexcept {
    if (id.empty())
        return false;
    for (const unsigned char c : id) {
        const bool asciiAlpha = (c >= 'a' && c <= 'z') ||
                                (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!asciiAlpha && !digit && c != '.' && c != '-' && c != '_')
            return false;
    }
    return id != "." && id != "..";
}

[[nodiscard]] bool validIdentity(std::string_view identity) noexcept {
    if (identity.empty() || identity == "." || identity == "..")
        return false;
    if (identity.find('/') != std::string_view::npos ||
        identity.find('\\') != std::string_view::npos ||
        identity.find(':') != std::string_view::npos ||
        identity.find('\0') != std::string_view::npos)
        return false;
    const fs::path path{std::string{identity}};
    return path.filename() == path && path.extension() == kPresetSuffix;
}

[[nodiscard]] std::string slugForDisplayName(std::string_view displayName) {
    std::string slug;
    slug.reserve(std::min<std::size_t>(displayName.size(), 80u));
    bool pendingDash = false;

    for (const unsigned char c : displayName) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            if (pendingDash && !slug.empty())
                slug.push_back('-');
            pendingDash = false;
            char normalized = static_cast<char>(c);
            if (normalized >= 'A' && normalized <= 'Z')
                normalized = static_cast<char>(normalized - 'A' + 'a');
            slug.push_back(normalized);
        } else {
            pendingDash = true;
        }

        if (slug.size() >= 64u)
            break;
    }

    if (slug.empty())
        slug = "preset";
    return slug;
}

[[nodiscard]] std::string identityForAttempt(std::string_view slug,
                                             std::size_t attempt) {
    std::string identity{slug};
    if (attempt > 1u) {
        identity.push_back('-');
        identity += std::to_string(attempt);
    }
    identity += kPresetSuffix;
    return identity;
}

[[nodiscard]] std::uint64_t processId() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

[[nodiscard]] fs::path temporaryPathFor(const fs::path &destination) {
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto counter = gTemporaryFileCounter.fetch_add(1u, std::memory_order_relaxed);
    std::string name = destination.filename().string();
    name += ".tmp-";
    name += std::to_string(processId());
    name.push_back('-');
    name += std::to_string(tick);
    name.push_back('-');
    name += std::to_string(counter);
    return destination.parent_path() / name;
}

void bestEffortRemove(const fs::path &path) noexcept {
    std::error_code ignored;
    fs::remove(path, ignored);
}

[[nodiscard]] NativePresetStorageStatus readFileBytes(const fs::path &path,
                                                       std::string &bytes) noexcept {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return makeStatus(NativePresetStorageError::OpenFailed, path);
        bytes.assign(std::istreambuf_iterator<char>{input},
                     std::istreambuf_iterator<char>{});
        if (input.bad())
            return makeStatus(NativePresetStorageError::ReadFailed, path);
        return {};
    } catch (...) {
        return makeStatus(NativePresetStorageError::ReadFailed, path);
    }
}

[[nodiscard]] NativePresetStorageStatus commitTemporaryFile(
    const fs::path &temporary,
    const fs::path &destination,
    bool replaceExisting) noexcept {
#if defined(_WIN32)
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (replaceExisting)
        flags |= MOVEFILE_REPLACE_EXISTING;

    if (::MoveFileExW(temporary.c_str(), destination.c_str(), flags) != 0)
        return {};

    const auto windowsError = ::GetLastError();
    if (!replaceExisting &&
        (windowsError == ERROR_ALREADY_EXISTS || windowsError == ERROR_FILE_EXISTS))
        return makeStatus(NativePresetStorageError::Collision,
                          destination,
                          std::error_code(static_cast<int>(windowsError),
                                          std::system_category()));
    return makeStatus(NativePresetStorageError::ReplaceFailed,
                      destination,
                      std::error_code(static_cast<int>(windowsError),
                                      std::system_category()));
#else
    if (replaceExisting) {
        if (::rename(temporary.c_str(), destination.c_str()) == 0)
            return {};
        return makeStatus(NativePresetStorageError::ReplaceFailed,
                          destination,
                          std::error_code(errno, std::generic_category()));
    }

    if (::link(temporary.c_str(), destination.c_str()) != 0) {
        const int linkError = errno;
        if (linkError == EEXIST)
            return makeStatus(NativePresetStorageError::Collision,
                              destination,
                              std::error_code(linkError, std::generic_category()));
        return makeStatus(NativePresetStorageError::ReplaceFailed,
                          destination,
                          std::error_code(linkError, std::generic_category()));
    }

    std::error_code cleanupError;
    if (!fs::remove(temporary, cleanupError) || cleanupError)
        return makeStatus(NativePresetStorageError::CleanupFailed,
                          temporary,
                          cleanupError);
    return {};
#endif
}

} // namespace

NativePresetPathResult resolveNativePresetBaseRoot(
    NativePresetPlatform platform,
    const NativePresetEnvironment &environment) noexcept {
    try {
        switch (platform) {
            case NativePresetPlatform::MacOS:
                if (environment.home.empty())
                    return {makeStatus(NativePresetStorageError::MissingEnvironment), {}};
                return {{}, fs::path{environment.home} / "Library" / "Application Support"};

            case NativePresetPlatform::Windows:
                if (environment.appData.empty())
                    return {makeStatus(NativePresetStorageError::MissingEnvironment), {}};
                return {{}, fs::path{environment.appData}};

            case NativePresetPlatform::Linux:
                if (!environment.xdgConfigHome.empty())
                    return {{}, fs::path{environment.xdgConfigHome}};
                if (environment.home.empty())
                    return {makeStatus(NativePresetStorageError::MissingEnvironment), {}};
                return {{}, fs::path{environment.home} / ".config"};

            case NativePresetPlatform::Unsupported:
                return {makeStatus(NativePresetStorageError::UnsupportedPlatform), {}};
        }
    } catch (...) {
        return {makeStatus(NativePresetStorageError::InvalidBaseRoot), {}};
    }
    return {makeStatus(NativePresetStorageError::UnsupportedPlatform), {}};
}

NativePresetPathResult resolveCurrentNativePresetBaseRoot() noexcept {
#if defined(_WIN32)
    PWSTR roaming = nullptr;
    const HRESULT result = ::SHGetKnownFolderPath(FOLDERID_RoamingAppData,
                                                   KF_FLAG_DEFAULT,
                                                   nullptr,
                                                   &roaming);
    if (FAILED(result) || roaming == nullptr) {
        if (roaming)
            ::CoTaskMemFree(roaming);
        return {makeStatus(
                    NativePresetStorageError::MissingEnvironment,
                    {},
                    std::error_code(static_cast<int>(result), std::system_category())),
                {}};
    }
    try {
        fs::path path{roaming};
        ::CoTaskMemFree(roaming);
        return {{}, std::move(path)};
    } catch (...) {
        ::CoTaskMemFree(roaming);
        return {makeStatus(NativePresetStorageError::InvalidBaseRoot), {}};
    }
#elif defined(__APPLE__)
    NativePresetEnvironment environment;
    if (const char *home = std::getenv("HOME"))
        environment.home = home;
    return resolveNativePresetBaseRoot(NativePresetPlatform::MacOS, environment);
#elif defined(__linux__)
    NativePresetEnvironment environment;
    if (const char *home = std::getenv("HOME"))
        environment.home = home;
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"))
        environment.xdgConfigHome = xdg;
    return resolveNativePresetBaseRoot(NativePresetPlatform::Linux, environment);
#else
    return {makeStatus(NativePresetStorageError::UnsupportedPlatform), {}};
#endif
}

NativePresetPathResult nativePresetScopedRoot(const fs::path &baseRoot,
                                               std::string_view targetPluginId) noexcept {
    try {
        if (baseRoot.empty())
            return {makeStatus(NativePresetStorageError::InvalidBaseRoot), {}};
        if (!validTargetPluginId(targetPluginId))
            return {makeStatus(NativePresetStorageError::InvalidTargetPluginId), {}};
        return {{},
                baseRoot / std::string{kStorageNamespace} /
                    std::string{kPresetDirectoryName} /
                    std::string{targetPluginId}};
    } catch (...) {
        return {makeStatus(NativePresetStorageError::InvalidBaseRoot), {}};
    }
}

NativePresetStorage::NativePresetStorage(fs::path baseRoot,
                                         std::string targetPluginId,
                                         NativePresetStorageOptions options)
    : baseRoot_(std::move(baseRoot)),
      targetPluginId_(std::move(targetPluginId)),
      options_(options) {
    const auto scoped = nativePresetScopedRoot(baseRoot_, targetPluginId_);
    scopeStatus_ = scoped.status;
    root_ = scoped.path;
}

NativePresetStorageStatus NativePresetStorage::ensureReady() const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return ensureReadyUnlocked();
}

NativePresetStorageStatus NativePresetStorage::ensureReadyUnlocked() const noexcept {
    if (!scopeStatus_.ok())
        return scopeStatus_;

    try {
        std::error_code error;
        const bool exists = fs::exists(root_, error);
        if (error)
            return makeStatus(NativePresetStorageError::CreateDirectoryFailed,
                              root_,
                              error);
        if (exists) {
            if (!fs::is_directory(root_, error) || error)
                return makeStatus(NativePresetStorageError::CreateDirectoryFailed,
                                  root_,
                                  error);
            return {};
        }

        if (!fs::create_directories(root_, error) && error)
            return makeStatus(NativePresetStorageError::CreateDirectoryFailed,
                              root_,
                              error);
        if (!fs::is_directory(root_, error) || error)
            return makeStatus(NativePresetStorageError::CreateDirectoryFailed,
                              root_,
                              error);
        return {};
    } catch (...) {
        return makeStatus(NativePresetStorageError::CreateDirectoryFailed, root_);
    }
}

NativePresetListResult NativePresetStorage::list() const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    NativePresetListResult result;
    result.status = ensureReadyUnlocked();
    if (!result.status.ok())
        return result;

    try {
        std::error_code iterationError;
        fs::directory_iterator iterator{root_, iterationError};
        if (iterationError) {
            result.status = makeStatus(NativePresetStorageError::EnumerateFailed,
                                       root_,
                                       iterationError);
            return result;
        }

        const fs::directory_iterator end{};
        for (; iterator != end; iterator.increment(iterationError)) {
            if (iterationError) {
                result.status = makeStatus(NativePresetStorageError::EnumerateFailed,
                                           root_,
                                           iterationError);
                return result;
            }

            const auto &entry = *iterator;
            std::error_code typeError;
            if (!entry.is_regular_file(typeError) || typeError)
                continue;
            if (entry.path().extension() != kPresetSuffix)
                continue;

            std::string bytes;
            const auto readStatus = readFileBytes(entry.path(), bytes);
            if (!readStatus.ok()) {
                result.diagnostics.push_back({readStatus});
                continue;
            }

            const auto metadata = parsePresetMetadata(bytes, targetPluginId_);
            if (!metadata.ok()) {
                const auto storageError =
                    metadata.error == PresetCodecError::WrongTargetPlugin
                        ? NativePresetStorageError::WrongTargetPlugin
                        : NativePresetStorageError::ParseFailed;
                result.diagnostics.push_back(
                    {makeStatus(storageError, entry.path(), {}, metadata.error)});
                continue;
            }

            result.entries.push_back({entry.path().filename().string(),
                                      entry.path(),
                                      metadata.metadata});
        }

        std::sort(result.entries.begin(), result.entries.end(),
                  [](const auto &a, const auto &b) {
                      return a.identity < b.identity;
                  });
        return result;
    } catch (...) {
        result.status = makeStatus(NativePresetStorageError::EnumerateFailed, root_);
        return result;
    }
}

NativePresetStorageStatus NativePresetStorage::validateDocumentForStorage(
    const PresetDocument &document,
    PresetSerializeResult &serialized) const noexcept {
    if (document.metadata.targetPluginId != targetPluginId_)
        return makeStatus(NativePresetStorageError::WrongTargetPlugin, root_);

    try {
        serialized = serializePresetDocument(document);
    } catch (...) {
        return makeStatus(NativePresetStorageError::SerializeFailed, root_);
    }

    if (!serialized.ok())
        return makeStatus(NativePresetStorageError::SerializeFailed,
                          root_,
                          {},
                          serialized.error);
    return {};
}

bool NativePresetStorage::shouldFail(NativePresetWriteStage stage) const noexcept {
    return options_.shouldFailWrite != nullptr &&
           options_.shouldFailWrite(stage, options_.faultUserData);
}

NativePresetStorageStatus NativePresetStorage::writeCanonicalFile(
    const fs::path &destination,
    std::string_view bytes,
    bool replaceExisting) const noexcept {
    fs::path temporary;
    try {
        temporary = temporaryPathFor(destination);
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return makeStatus(NativePresetStorageError::OpenFailed, temporary);

        if (shouldFail(NativePresetWriteStage::AfterTemporaryOpen)) {
            output.close();
            bestEffortRemove(temporary);
            return makeStatus(NativePresetStorageError::WriteFailed, temporary);
        }

        const auto firstSize = bytes.size() / 2u;
        output.write(bytes.data(), static_cast<std::streamsize>(firstSize));
        if (!output) {
            output.close();
            bestEffortRemove(temporary);
            return makeStatus(NativePresetStorageError::WriteFailed, temporary);
        }

        if (shouldFail(NativePresetWriteStage::AfterPartialWrite)) {
            output.close();
            bestEffortRemove(temporary);
            return makeStatus(NativePresetStorageError::WriteFailed, temporary);
        }

        output.write(bytes.data() + firstSize,
                     static_cast<std::streamsize>(bytes.size() - firstSize));
        if (!output) {
            output.close();
            bestEffortRemove(temporary);
            return makeStatus(NativePresetStorageError::WriteFailed, temporary);
        }

        output.flush();
        if (!output) {
            output.close();
            bestEffortRemove(temporary);
            return makeStatus(NativePresetStorageError::FlushFailed, temporary);
        }
        output.close();

        if (shouldFail(NativePresetWriteStage::BeforeReplace)) {
            bestEffortRemove(temporary);
            return makeStatus(NativePresetStorageError::ReplaceFailed, destination);
        }

        auto commitStatus = commitTemporaryFile(temporary,
                                                destination,
                                                replaceExisting);
        if (!commitStatus.ok())
            bestEffortRemove(temporary);
        return commitStatus;
    } catch (...) {
        if (!temporary.empty())
            bestEffortRemove(temporary);
        return makeStatus(NativePresetStorageError::WriteFailed,
                          temporary.empty() ? destination : temporary);
    }
}

NativePresetSaveResult NativePresetStorage::saveNew(
    const PresetDocument &document) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    NativePresetSaveResult result;

    PresetSerializeResult serialized;
    result.status = validateDocumentForStorage(document, serialized);
    if (!result.status.ok())
        return result;

    result.status = ensureReadyUnlocked();
    if (!result.status.ok())
        return result;

    try {
        const auto slug = slugForDisplayName(document.metadata.name);
        for (std::size_t attempt = 1u; attempt <= kMaxSaveIdentityAttempts; ++attempt) {
            const auto identity = identityForAttempt(slug, attempt);
            const auto destination = root_ / identity;

            std::error_code existsError;
            if (fs::exists(destination, existsError)) {
                if (existsError) {
                    result.status = makeStatus(NativePresetStorageError::WriteFailed,
                                               destination,
                                               existsError);
                    return result;
                }
                continue;
            }

            auto writeStatus = writeCanonicalFile(destination,
                                                  serialized.bytes,
                                                  false);
            if (writeStatus.error == NativePresetStorageError::Collision)
                continue;
            if (!writeStatus.ok()) {
                result.status = std::move(writeStatus);
                return result;
            }

            result.identity = identity;
            result.path = destination;
            return result;
        }

        result.status = makeStatus(NativePresetStorageError::Collision, root_);
        return result;
    } catch (...) {
        result.status = makeStatus(NativePresetStorageError::WriteFailed, root_);
        return result;
    }
}

NativePresetSaveResult NativePresetStorage::replace(
    std::string_view identity,
    const PresetDocument &document) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    NativePresetSaveResult result;

    if (!validIdentity(identity)) {
        result.status = makeStatus(NativePresetStorageError::InvalidIdentity, root_);
        return result;
    }

    PresetSerializeResult serialized;
    result.status = validateDocumentForStorage(document, serialized);
    if (!result.status.ok())
        return result;

    result.status = ensureReadyUnlocked();
    if (!result.status.ok())
        return result;

    try {
        const fs::path destination = root_ / std::string{identity};
        std::error_code typeError;
        if (!fs::is_regular_file(destination, typeError) || typeError) {
            result.status = makeStatus(NativePresetStorageError::NotFound,
                                       destination,
                                       typeError);
            return result;
        }

        result.status = writeCanonicalFile(destination,
                                           serialized.bytes,
                                           true);
        if (!result.status.ok())
            return result;

        result.identity.assign(identity.data(), identity.size());
        result.path = destination;
        return result;
    } catch (...) {
        result.status = makeStatus(NativePresetStorageError::WriteFailed, root_);
        return result;
    }
}

NativePresetLoadResult NativePresetStorage::load(std::string_view identity) const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    NativePresetLoadResult result;

    if (!validIdentity(identity)) {
        result.status = makeStatus(NativePresetStorageError::InvalidIdentity, root_);
        return result;
    }

    result.status = ensureReadyUnlocked();
    if (!result.status.ok())
        return result;

    try {
        const fs::path source = root_ / std::string{identity};
        std::error_code typeError;
        if (!fs::is_regular_file(source, typeError) || typeError) {
            result.status = makeStatus(NativePresetStorageError::NotFound,
                                       source,
                                       typeError);
            return result;
        }

        std::string bytes;
        result.status = readFileBytes(source, bytes);
        if (!result.status.ok())
            return result;

        const auto parsed = parsePresetDocument(bytes, targetPluginId_);
        if (!parsed.ok()) {
            result.status = makeStatus(
                parsed.error == PresetCodecError::WrongTargetPlugin
                    ? NativePresetStorageError::WrongTargetPlugin
                    : NativePresetStorageError::ParseFailed,
                source,
                {},
                parsed.error);
            return result;
        }

        result.document = parsed.document;
        return result;
    } catch (...) {
        result.status = makeStatus(NativePresetStorageError::ReadFailed, root_);
        return result;
    }
}

} // namespace webview_gui::examples::presets
