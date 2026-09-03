#include "native_preset_storage.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace webview_gui::examples::presets {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kStorageNamespace = "webview-gui";
constexpr std::string_view kPresetDirectoryName = "presets";
constexpr std::string_view kPresetSuffix = ".wvpreset";
constexpr std::size_t kMaxSaveIdentityAttempts = 10000u;
constexpr std::size_t kMaxTemporaryAttempts = 128u;
constexpr std::size_t kMaxIdentityBytes = 192u;

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
    if (id.empty() || id.size() > kMaxIdentityBytes ||
        id == "." || id == ".." || id.find("..") != std::string_view::npos)
        return false;
    for (const unsigned char c : id) {
        const bool asciiAlpha = (c >= 'a' && c <= 'z') ||
                                (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!asciiAlpha && !digit && c != '.' && c != '-' && c != '_')
            return false;
    }
    return true;
}

[[nodiscard]] bool validIdentity(std::string_view identity) noexcept {
    if (identity.empty() || identity.size() > kMaxIdentityBytes ||
        identity == "." || identity == ".." ||
        identity.find("..") != std::string_view::npos ||
        identity.find('%') != std::string_view::npos ||
        identity.find('/') != std::string_view::npos ||
        identity.find('\\') != std::string_view::npos ||
        identity.find(':') != std::string_view::npos ||
        identity.find('\0') != std::string_view::npos)
        return false;

    const fs::path path{std::string{identity}};
    if (path.filename() != path || path.extension() != kPresetSuffix)
        return false;

    const auto stem = path.stem().string();
    if (stem.empty() || stem.front() == '.')
        return false;
    for (const unsigned char c : stem) {
        const bool asciiAlpha = (c >= 'a' && c <= 'z') ||
                                (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!asciiAlpha && !digit && c != '-' && c != '_')
            return false;
    }
    return true;
}

[[nodiscard]] bool windowsAbsolutePathText(std::string_view text) noexcept {
    if (text.size() >= 3u) {
        const unsigned char first = static_cast<unsigned char>(text[0]);
        const bool alpha = (first >= 'a' && first <= 'z') ||
                           (first >= 'A' && first <= 'Z');
        if (alpha && text[1] == ':' && (text[2] == '/' || text[2] == '\\'))
            return true;
    }
    return text.size() >= 2u &&
           ((text[0] == '/' && text[1] == '/') ||
            (text[0] == '\\' && text[1] == '\\'));
}

[[nodiscard]] bool environmentPathIsAbsolute(NativePresetPlatform platform,
                                             std::string_view text) noexcept {
    if (text.empty())
        return false;
    if (platform == NativePresetPlatform::Windows)
        return windowsAbsolutePathText(text);
    try {
        return fs::path{std::string{text}}.is_absolute();
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool pathContainedBy(const fs::path &candidate,
                                   const fs::path &base) noexcept {
    try {
        const auto relative = candidate.lexically_relative(base);
        if (relative.empty())
            return candidate == base;
        for (const auto &component : relative) {
            if (component == "..")
                return false;
        }
        return !relative.is_absolute();
    } catch (...) {
        return false;
    }
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

[[nodiscard]] fs::path temporaryPathFor(const fs::path &destination,
                                        std::size_t attempt) {
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
    name.push_back('-');
    name += std::to_string(attempt);
    return destination.parent_path() / name;
}

void bestEffortRemove(const fs::path &path) noexcept {
    std::error_code ignored;
    fs::remove(path, ignored);
}

void syncParentDirectoryBestEffort(const fs::path &path) noexcept {
#if !defined(_WIN32)
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    const int fd = ::open(path.c_str(), flags);
    if (fd >= 0) {
        while (::fsync(fd) != 0 && errno == EINTR) {
        }
        while (::close(fd) != 0 && errno == EINTR) {
        }
    }
#else
    (void)path;
#endif
}

[[nodiscard]] NativePresetStorageStatus readFileBytes(const fs::path &path,
                                                       std::string &bytes) noexcept {
    bytes.clear();
#if defined(_WIN32)
    const HANDLE file = ::CreateFileW(path.c_str(),
                                      GENERIC_READ,
                                      FILE_SHARE_READ,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                      nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto error = ::GetLastError();
        return makeStatus(
            NativePresetStorageError::OpenFailed,
            path,
            std::error_code(static_cast<int>(error), std::system_category()));
    }

    BY_HANDLE_FILE_INFORMATION information{};
    if (::GetFileInformationByHandle(file, &information) == 0) {
        const auto error = ::GetLastError();
        ::CloseHandle(file);
        return makeStatus(
            NativePresetStorageError::ReadFailed,
            path,
            std::error_code(static_cast<int>(error), std::system_category()));
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
        ::CloseHandle(file);
        return makeStatus(NativePresetStorageError::OutsideRoot, path);
    }

    LARGE_INTEGER size{};
    if (::GetFileSizeEx(file, &size) == 0 || size.QuadPart < 0) {
        const auto error = ::GetLastError();
        ::CloseHandle(file);
        return makeStatus(
            NativePresetStorageError::ReadFailed,
            path,
            std::error_code(static_cast<int>(error), std::system_category()));
    }
    if (static_cast<std::uint64_t>(size.QuadPart) > detail::kMaxPresetBytes) {
        ::CloseHandle(file);
        return makeStatus(NativePresetStorageError::ParseFailed,
                          path,
                          {},
                          PresetCodecError::InputTooLarge);
    }

    try {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
    } catch (...) {
        ::CloseHandle(file);
        return makeStatus(NativePresetStorageError::ReadFailed, path);
    }

    std::size_t offset = 0u;
    while (offset < bytes.size()) {
        DWORD read = 0u;
        const auto remaining = static_cast<DWORD>(bytes.size() - offset);
        if (::ReadFile(file, bytes.data() + offset, remaining, &read, nullptr) == 0 ||
            read == 0u) {
            const auto error = ::GetLastError();
            ::CloseHandle(file);
            bytes.clear();
            return makeStatus(
                NativePresetStorageError::ReadFailed,
                path,
                std::error_code(static_cast<int>(error), std::system_category()));
        }
        offset += static_cast<std::size_t>(read);
    }
    ::CloseHandle(file);
    return {};
#else
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::open(path.c_str(), flags);
    if (fd < 0) {
        const int openError = errno;
#ifdef ELOOP
        if (openError == ELOOP)
            return makeStatus(NativePresetStorageError::OutsideRoot,
                              path,
                              std::error_code(openError, std::generic_category()));
#endif
        return makeStatus(NativePresetStorageError::OpenFailed,
                          path,
                          std::error_code(openError, std::generic_category()));
    }

    struct stat statBuffer {};
    if (::fstat(fd, &statBuffer) != 0) {
        const int statError = errno;
        ::close(fd);
        return makeStatus(NativePresetStorageError::ReadFailed,
                          path,
                          std::error_code(statError, std::generic_category()));
    }
    if (!S_ISREG(statBuffer.st_mode)) {
        ::close(fd);
        return makeStatus(NativePresetStorageError::OutsideRoot, path);
    }
    if (statBuffer.st_size < 0 ||
        static_cast<std::uint64_t>(statBuffer.st_size) > detail::kMaxPresetBytes) {
        ::close(fd);
        return makeStatus(NativePresetStorageError::ParseFailed,
                          path,
                          {},
                          PresetCodecError::InputTooLarge);
    }

    try {
        bytes.resize(static_cast<std::size_t>(statBuffer.st_size));
    } catch (...) {
        ::close(fd);
        return makeStatus(NativePresetStorageError::ReadFailed, path);
    }

    std::size_t offset = 0u;
    while (offset < bytes.size()) {
        const auto read = ::read(fd, bytes.data() + offset, bytes.size() - offset);
        if (read < 0 && errno == EINTR)
            continue;
        if (read <= 0) {
            const int readError = read < 0 ? errno : EIO;
            ::close(fd);
            bytes.clear();
            return makeStatus(NativePresetStorageError::ReadFailed,
                              path,
                              std::error_code(readError, std::generic_category()));
        }
        offset += static_cast<std::size_t>(read);
    }
    while (::close(fd) != 0 && errno == EINTR) {
    }
    return {};
#endif
}

[[nodiscard]] NativePresetStorageStatus commitTemporaryFile(
    const fs::path &temporary,
    const fs::path &destination,
    bool replaceExisting) noexcept {
#if defined(_WIN32)
    if (replaceExisting) {
        if (::ReplaceFileW(destination.c_str(),
                           temporary.c_str(),
                           nullptr,
                           REPLACEFILE_WRITE_THROUGH,
                           nullptr,
                           nullptr) != 0)
            return {};

        const auto windowsError = ::GetLastError();
        if (windowsError == ERROR_FILE_NOT_FOUND ||
            windowsError == ERROR_PATH_NOT_FOUND)
            return makeStatus(
                NativePresetStorageError::NotFound,
                destination,
                std::error_code(static_cast<int>(windowsError), std::system_category()));
        return makeStatus(
            NativePresetStorageError::ReplaceFailed,
            destination,
            std::error_code(static_cast<int>(windowsError), std::system_category()));
    }

    if (::MoveFileExW(temporary.c_str(),
                      destination.c_str(),
                      MOVEFILE_WRITE_THROUGH) != 0)
        return {};

    const auto windowsError = ::GetLastError();
    if (windowsError == ERROR_ALREADY_EXISTS || windowsError == ERROR_FILE_EXISTS)
        return makeStatus(
            NativePresetStorageError::AlreadyExists,
            destination,
            std::error_code(static_cast<int>(windowsError), std::system_category()));
    return makeStatus(
        NativePresetStorageError::ReplaceFailed,
        destination,
        std::error_code(static_cast<int>(windowsError), std::system_category()));
#else
    if (replaceExisting) {
        if (::rename(temporary.c_str(), destination.c_str()) == 0) {
            syncParentDirectoryBestEffort(destination.parent_path());
            return {};
        }
        return makeStatus(NativePresetStorageError::ReplaceFailed,
                          destination,
                          std::error_code(errno, std::generic_category()));
    }

    if (::link(temporary.c_str(), destination.c_str()) != 0) {
        const int linkError = errno;
        if (linkError == EEXIST)
            return makeStatus(NativePresetStorageError::AlreadyExists,
                              destination,
                              std::error_code(linkError, std::generic_category()));
        return makeStatus(NativePresetStorageError::ReplaceFailed,
                          destination,
                          std::error_code(linkError, std::generic_category()));
    }

    if (::unlink(temporary.c_str()) != 0) {
        const int cleanupError = errno;
        (void)::unlink(destination.c_str());
        syncParentDirectoryBestEffort(destination.parent_path());
        return makeStatus(NativePresetStorageError::CleanupFailed,
                          temporary,
                          std::error_code(cleanupError, std::generic_category()));
    }
    syncParentDirectoryBestEffort(destination.parent_path());
    return {};
#endif
}

[[nodiscard]] NativePresetStorageStatus writeTemporaryFileExclusive(
    const fs::path &temporary,
    std::string_view bytes,
    const NativePresetStorageOptions &options) noexcept {
    const auto shouldFail = [&options](NativePresetWriteStage stage) noexcept {
        return options.shouldFailWrite != nullptr &&
               options.shouldFailWrite(stage, options.faultUserData);
    };

#if defined(_WIN32)
    const HANDLE file = ::CreateFileW(temporary.c_str(),
                                      GENERIC_WRITE,
                                      0,
                                      nullptr,
                                      CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                                      nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto error = ::GetLastError();
        if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
            return makeStatus(
                NativePresetStorageError::AlreadyExists,
                temporary,
                std::error_code(static_cast<int>(error), std::system_category()));
        return makeStatus(
            NativePresetStorageError::OpenFailed,
            temporary,
            std::error_code(static_cast<int>(error), std::system_category()));
    }

    if (shouldFail(NativePresetWriteStage::AfterTemporaryOpen)) {
        ::CloseHandle(file);
        bestEffortRemove(temporary);
        return makeStatus(NativePresetStorageError::WriteFailed, temporary);
    }

    const auto writeRange = [file, &temporary](const char *data,
                                               std::size_t size) noexcept {
        std::size_t offset = 0u;
        while (offset < size) {
            DWORD written = 0u;
            const DWORD remaining = static_cast<DWORD>(size - offset);
            if (::WriteFile(file, data + offset, remaining, &written, nullptr) == 0 ||
                written == 0u) {
                const auto error = ::GetLastError();
                return makeStatus(
                    NativePresetStorageError::WriteFailed,
                    temporary,
                    std::error_code(static_cast<int>(error), std::system_category()));
            }
            offset += static_cast<std::size_t>(written);
        }
        return NativePresetStorageStatus{};
    };

    const auto firstSize = bytes.size() / 2u;
    auto status = writeRange(bytes.data(), firstSize);
    if (!status.ok()) {
        ::CloseHandle(file);
        bestEffortRemove(temporary);
        return status;
    }
    if (shouldFail(NativePresetWriteStage::AfterPartialWrite)) {
        ::CloseHandle(file);
        bestEffortRemove(temporary);
        return makeStatus(NativePresetStorageError::WriteFailed, temporary);
    }
    status = writeRange(bytes.data() + firstSize, bytes.size() - firstSize);
    if (!status.ok()) {
        ::CloseHandle(file);
        bestEffortRemove(temporary);
        return status;
    }
    if (::FlushFileBuffers(file) == 0) {
        const auto error = ::GetLastError();
        ::CloseHandle(file);
        bestEffortRemove(temporary);
        return makeStatus(
            NativePresetStorageError::FlushFailed,
            temporary,
            std::error_code(static_cast<int>(error), std::system_category()));
    }
    if (::CloseHandle(file) == 0) {
        const auto error = ::GetLastError();
        bestEffortRemove(temporary);
        return makeStatus(
            NativePresetStorageError::FlushFailed,
            temporary,
            std::error_code(static_cast<int>(error), std::system_category()));
    }
    return {};
#else
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::open(temporary.c_str(), flags, 0600);
    if (fd < 0) {
        const int openError = errno;
        if (openError == EEXIST)
            return makeStatus(NativePresetStorageError::AlreadyExists,
                              temporary,
                              std::error_code(openError, std::generic_category()));
        return makeStatus(NativePresetStorageError::OpenFailed,
                          temporary,
                          std::error_code(openError, std::generic_category()));
    }

    const auto cleanup = [fd, &temporary]() noexcept {
        while (::close(fd) != 0 && errno == EINTR) {
        }
        bestEffortRemove(temporary);
    };

    if (shouldFail(NativePresetWriteStage::AfterTemporaryOpen)) {
        cleanup();
        return makeStatus(NativePresetStorageError::WriteFailed, temporary);
    }

    const auto writeRange = [fd, &temporary](const char *data,
                                             std::size_t size) noexcept {
        std::size_t offset = 0u;
        while (offset < size) {
            const auto written = ::write(fd, data + offset, size - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                const int writeError = written < 0 ? errno : EIO;
                return makeStatus(NativePresetStorageError::WriteFailed,
                                  temporary,
                                  std::error_code(writeError, std::generic_category()));
            }
            offset += static_cast<std::size_t>(written);
        }
        return NativePresetStorageStatus{};
    };

    const auto firstSize = bytes.size() / 2u;
    auto status = writeRange(bytes.data(), firstSize);
    if (!status.ok()) {
        cleanup();
        return status;
    }
    if (shouldFail(NativePresetWriteStage::AfterPartialWrite)) {
        cleanup();
        return makeStatus(NativePresetStorageError::WriteFailed, temporary);
    }
    status = writeRange(bytes.data() + firstSize, bytes.size() - firstSize);
    if (!status.ok()) {
        cleanup();
        return status;
    }

    while (::fsync(fd) != 0) {
        if (errno == EINTR)
            continue;
        const int flushError = errno;
        cleanup();
        return makeStatus(NativePresetStorageError::FlushFailed,
                          temporary,
                          std::error_code(flushError, std::generic_category()));
    }
    while (::close(fd) != 0) {
        if (errno == EINTR)
            continue;
        const int closeError = errno;
        bestEffortRemove(temporary);
        return makeStatus(NativePresetStorageError::FlushFailed,
                          temporary,
                          std::error_code(closeError, std::generic_category()));
    }
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
                if (!environmentPathIsAbsolute(platform, environment.home))
                    return {makeStatus(NativePresetStorageError::InvalidBaseRoot), {}};
                return {{}, fs::path{environment.home} / "Library" / "Application Support"};

            case NativePresetPlatform::Windows:
                if (environment.appData.empty())
                    return {makeStatus(NativePresetStorageError::MissingEnvironment), {}};
                if (!environmentPathIsAbsolute(platform, environment.appData))
                    return {makeStatus(NativePresetStorageError::InvalidBaseRoot), {}};
                return {{}, fs::path{environment.appData}};

            case NativePresetPlatform::Linux:
                if (!environment.xdgConfigHome.empty()) {
                    if (!environmentPathIsAbsolute(platform, environment.xdgConfigHome))
                        return {makeStatus(NativePresetStorageError::InvalidBaseRoot), {}};
                    return {{}, fs::path{environment.xdgConfigHome}};
                }
                if (environment.home.empty())
                    return {makeStatus(NativePresetStorageError::MissingEnvironment), {}};
                if (!environmentPathIsAbsolute(platform, environment.home))
                    return {makeStatus(NativePresetStorageError::InvalidBaseRoot), {}};
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

NativePresetPathResult resolveCurrentNativePresetScopedRoot(
    std::string_view targetPluginId) noexcept {
    const auto base = resolveCurrentNativePresetBaseRoot();
    if (!base.ok())
        return base;
    return nativePresetScopedRoot(base.path, targetPluginId);
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

NativePresetStorageStatus NativePresetStorage::validateRootContainmentUnlocked() const noexcept {
    if (!scopeStatus_.ok())
        return scopeStatus_;
    try {
        if (!baseRoot_.is_absolute())
            return makeStatus(NativePresetStorageError::InvalidBaseRoot, baseRoot_);

        std::error_code error;
        const auto canonicalBase = fs::weakly_canonical(baseRoot_, error);
        if (error)
            return makeStatus(NativePresetStorageError::InvalidBaseRoot,
                              baseRoot_,
                              error);
        const auto canonicalRoot = fs::weakly_canonical(root_, error);
        if (error)
            return makeStatus(NativePresetStorageError::OutsideRoot, root_, error);
        if (!pathContainedBy(canonicalRoot, canonicalBase))
            return makeStatus(NativePresetStorageError::OutsideRoot, root_);

        const auto rootStatus = fs::symlink_status(root_, error);
        if (!error && fs::is_symlink(rootStatus))
            return makeStatus(NativePresetStorageError::OutsideRoot, root_);
        return {};
    } catch (...) {
        return makeStatus(NativePresetStorageError::OutsideRoot, root_);
    }
}

NativePresetStorageStatus NativePresetStorage::ensureReadyUnlocked() const noexcept {
    auto containment = validateRootContainmentUnlocked();
    if (!containment.ok())
        return containment;

    try {
        std::error_code error;
        const auto status = fs::symlink_status(root_, error);
        if (!error && status.type() != fs::file_type::not_found) {
            if (fs::is_symlink(status))
                return makeStatus(NativePresetStorageError::OutsideRoot, root_);
            if (!fs::is_directory(status))
                return makeStatus(NativePresetStorageError::CreateDirectoryFailed,
                                  root_);
            return validateRootContainmentUnlocked();
        }

        error.clear();
        if (!fs::create_directories(root_, error) && error)
            return makeStatus(NativePresetStorageError::CreateDirectoryFailed,
                              root_,
                              error);
        error.clear();
        const auto createdStatus = fs::symlink_status(root_, error);
        if (error || !fs::is_directory(createdStatus) || fs::is_symlink(createdStatus))
            return makeStatus(NativePresetStorageError::CreateDirectoryFailed,
                              root_,
                              error);
        return validateRootContainmentUnlocked();
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
            if (entry.path().extension() != kPresetSuffix)
                continue;

            std::error_code typeError;
            const auto type = entry.symlink_status(typeError);
            if (typeError) {
                result.diagnostics.push_back(
                    {makeStatus(NativePresetStorageError::EnumerateFailed,
                                entry.path(),
                                typeError)});
                continue;
            }
            if (fs::is_symlink(type)) {
                result.diagnostics.push_back(
                    {makeStatus(NativePresetStorageError::OutsideRoot, entry.path())});
                continue;
            }
            if (!fs::is_regular_file(type))
                continue;

            const auto identity = entry.path().filename().string();
            if (!validIdentity(identity)) {
                result.diagnostics.push_back(
                    {makeStatus(NativePresetStorageError::InvalidIdentity, entry.path())});
                continue;
            }

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

            result.entries.push_back({identity, entry.path(), metadata.metadata});
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
    for (std::size_t attempt = 0u; attempt < kMaxTemporaryAttempts; ++attempt) {
        const auto temporary = temporaryPathFor(destination, attempt);
        auto writeStatus = writeTemporaryFileExclusive(temporary, bytes, options_);
        if (writeStatus.error == NativePresetStorageError::AlreadyExists)
            continue;
        if (!writeStatus.ok())
            return writeStatus;

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
    }
    return makeStatus(NativePresetStorageError::Collision, destination);
}

NativePresetSaveResult NativePresetStorage::saveAsUnlocked(
    std::string_view identity,
    const PresetDocument &document,
    bool overwrite,
    NativePresetStorageError invalidNameError) noexcept {
    NativePresetSaveResult result;
    if (!validIdentity(identity)) {
        result.status = makeStatus(invalidNameError, root_);
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
        const auto type = fs::symlink_status(destination, typeError);
        const bool exists = !typeError && type.type() != fs::file_type::not_found;
        if (exists && fs::is_symlink(type)) {
            result.status = makeStatus(NativePresetStorageError::OutsideRoot,
                                       destination);
            return result;
        }
        if (!overwrite && exists) {
            result.status = makeStatus(NativePresetStorageError::AlreadyExists,
                                       destination);
            return result;
        }
        if (overwrite && (!exists || !fs::is_regular_file(type))) {
            result.status = makeStatus(NativePresetStorageError::NotFound,
                                       destination,
                                       typeError);
            return result;
        }

        result.status = writeCanonicalFile(destination, serialized.bytes, overwrite);
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

NativePresetSaveResult NativePresetStorage::saveNew(
    const PresetDocument &document) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};

    try {
        const auto slug = slugForDisplayName(document.metadata.name);
        for (std::size_t attempt = 1u; attempt <= kMaxSaveIdentityAttempts; ++attempt) {
            const auto identity = identityForAttempt(slug, attempt);
            auto result = saveAsUnlocked(identity,
                                         document,
                                         false,
                                         NativePresetStorageError::InvalidName);
            if (result.status.error == NativePresetStorageError::AlreadyExists)
                continue;
            return result;
        }
        NativePresetSaveResult result;
        result.status = makeStatus(NativePresetStorageError::Collision, root_);
        return result;
    } catch (...) {
        NativePresetSaveResult result;
        result.status = makeStatus(NativePresetStorageError::WriteFailed, root_);
        return result;
    }
}

NativePresetSaveResult NativePresetStorage::saveAs(
    std::string_view identity,
    const PresetDocument &document,
    bool overwrite) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return saveAsUnlocked(identity,
                          document,
                          overwrite,
                          NativePresetStorageError::InvalidName);
}

NativePresetSaveResult NativePresetStorage::replace(
    std::string_view identity,
    const PresetDocument &document) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return saveAsUnlocked(identity,
                          document,
                          true,
                          NativePresetStorageError::InvalidIdentity);
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
        const auto type = fs::symlink_status(source, typeError);
        if (!typeError && fs::is_symlink(type)) {
            result.status = makeStatus(NativePresetStorageError::OutsideRoot, source);
            return result;
        }
        if (typeError || !fs::is_regular_file(type)) {
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

NativePresetStorageStatus NativePresetStorage::remove(
    std::string_view identity) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!validIdentity(identity))
        return makeStatus(NativePresetStorageError::InvalidIdentity, root_);

    auto ready = ensureReadyUnlocked();
    if (!ready.ok())
        return ready;

    try {
        const fs::path destination = root_ / std::string{identity};
        std::error_code typeError;
        const auto type = fs::symlink_status(destination, typeError);
        if (!typeError && fs::is_symlink(type))
            return makeStatus(NativePresetStorageError::OutsideRoot, destination);
        if (typeError || !fs::is_regular_file(type))
            return makeStatus(NativePresetStorageError::NotFound,
                              destination,
                              typeError);

        std::error_code removeError;
        if (!fs::remove(destination, removeError) || removeError)
            return makeStatus(NativePresetStorageError::DeleteFailed,
                              destination,
                              removeError);
        syncParentDirectoryBestEffort(root_);
        return {};
    } catch (...) {
        return makeStatus(NativePresetStorageError::DeleteFailed, root_);
    }
}

} // namespace webview_gui::examples::presets
