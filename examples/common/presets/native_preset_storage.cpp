#include "native_preset_storage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <winternl.h>
#else
#include <dirent.h>
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
        id == "." || id == ".." || id.front() == '.' || id.back() == '.' ||
        id.find("..") != std::string_view::npos)
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

[[nodiscard]] bool windowsReservedStem(std::string_view stem) noexcept {
    if (stem.empty())
        return false;

    char normalized[5]{};
    if (stem.size() >= sizeof(normalized))
        return false;
    for (std::size_t i = 0u; i < stem.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(stem[i]);
        normalized[i] = (c >= 'a' && c <= 'z')
                            ? static_cast<char>(c - 'a' + 'A')
                            : static_cast<char>(c);
    }
    const std::string_view upper{normalized, stem.size()};
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL")
        return true;
    if (upper.size() == 4u &&
        (upper.substr(0u, 3u) == "COM" || upper.substr(0u, 3u) == "LPT") &&
        upper[3] >= '1' && upper[3] <= '9')
        return true;
    return false;
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
    if (stem.empty() || stem.front() == '.' || windowsReservedStem(stem))
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

    if (slug.empty() || windowsReservedStem(slug))
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

[[nodiscard]] std::string temporaryNameFor(std::string_view destination,
                                           std::size_t attempt) {
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto counter = gTemporaryFileCounter.fetch_add(1u, std::memory_order_relaxed);
    std::string name{destination};
    name += ".tmp-";
    name += std::to_string(processId());
    name.push_back('-');
    name += std::to_string(tick);
    name.push_back('-');
    name += std::to_string(counter);
    name.push_back('-');
    name += std::to_string(attempt);
    return name;
}

enum class RelativeEntryKind : std::uint8_t {
    Missing,
    Regular,
    Directory,
    SymlinkOrReparse,
    Other,
};

class PinnedRoot {
public:
#if defined(_WIN32)
    using Handle = HANDLE;
    static constexpr Handle kInvalid = INVALID_HANDLE_VALUE;
#else
    using Handle = int;
    static constexpr Handle kInvalid = -1;
#endif

    PinnedRoot() noexcept = default;
    explicit PinnedRoot(Handle handle) noexcept : handle_(handle) {}
    ~PinnedRoot() { reset(); }

    PinnedRoot(const PinnedRoot &) = delete;
    PinnedRoot &operator=(const PinnedRoot &) = delete;

    PinnedRoot(PinnedRoot &&other) noexcept : handle_(other.release()) {}
    PinnedRoot &operator=(PinnedRoot &&other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.release();
        }
        return *this;
    }

    [[nodiscard]] Handle get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != kInvalid; }

    [[nodiscard]] Handle release() noexcept {
        const auto value = handle_;
        handle_ = kInvalid;
        return value;
    }

    void reset(Handle replacement = kInvalid) noexcept {
        if (valid()) {
#if defined(_WIN32)
            (void)::CloseHandle(handle_);
#else
            (void)::close(handle_);
#endif
        }
        handle_ = replacement;
    }

private:
    Handle handle_ = kInvalid;
};

#if defined(_WIN32)

constexpr ULONG kNtFileOpen = 1u;
constexpr ULONG kNtFileCreate = 2u;
constexpr ULONG kNtFileOpenIf = 3u;
constexpr ULONG kNtFileDirectoryFile = 0x00000001u;
constexpr ULONG kNtFileWriteThrough = 0x00000002u;
constexpr ULONG kNtFileSynchronousIoNonAlert = 0x00000020u;
constexpr ULONG kNtFileNonDirectoryFile = 0x00000040u;
constexpr ULONG kNtFileOpenReparsePoint = 0x00200000u;
constexpr ULONG kObjCaseInsensitive = 0x00000040u;
constexpr ACCESS_MASK kDirectoryAccess =
    0x0001u | // FILE_LIST_DIRECTORY
    0x0002u | // FILE_ADD_FILE
    0x0004u | // FILE_ADD_SUBDIRECTORY
    0x0020u | // FILE_TRAVERSE
    0x0040u | // FILE_DELETE_CHILD
    0x0080u | // FILE_READ_ATTRIBUTES
    SYNCHRONIZE;

using NtCreateFileFn = NTSTATUS(NTAPI *)(PHANDLE,
                                         ACCESS_MASK,
                                         POBJECT_ATTRIBUTES,
                                         PIO_STATUS_BLOCK,
                                         PLARGE_INTEGER,
                                         ULONG,
                                         ULONG,
                                         ULONG,
                                         ULONG,
                                         PVOID,
                                         ULONG);
using RtlNtStatusToDosErrorFn = ULONG(NTAPI *)(NTSTATUS);

[[nodiscard]] NtCreateFileFn ntCreateFileFunction() noexcept {
    static const auto function = reinterpret_cast<NtCreateFileFn>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
    return function;
}

[[nodiscard]] RtlNtStatusToDosErrorFn rtlNtStatusToDosErrorFunction() noexcept {
    static const auto function = reinterpret_cast<RtlNtStatusToDosErrorFn>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError"));
    return function;
}

[[nodiscard]] std::error_code ntStatusError(NTSTATUS status) noexcept {
    if (const auto converter = rtlNtStatusToDosErrorFunction()) {
        const auto error = converter(status);
        return std::error_code(static_cast<int>(error), std::system_category());
    }
    return std::error_code(ERROR_GEN_FAILURE, std::system_category());
}

[[nodiscard]] bool ntSuccess(NTSTATUS status) noexcept {
    return status >= 0;
}

[[nodiscard]] bool handleIsReparsePoint(HANDLE handle,
                                        std::error_code &error) noexcept {
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (::GetFileInformationByHandleEx(handle,
                                       FileAttributeTagInfo,
                                       &tag,
                                       sizeof(tag)) == 0) {
        error = std::error_code(static_cast<int>(::GetLastError()),
                                std::system_category());
        return false;
    }
    error.clear();
    return (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u;
}

[[nodiscard]] HANDLE ntOpenRelative(HANDLE root,
                                    std::wstring_view name,
                                    ACCESS_MASK access,
                                    ULONG createDisposition,
                                    ULONG createOptions,
                                    ULONG attributes,
                                    std::error_code &error) noexcept {
    const auto ntCreateFile = ntCreateFileFunction();
    if (!ntCreateFile || name.empty() ||
        name.size() > (std::numeric_limits<USHORT>::max() / sizeof(wchar_t))) {
        error = std::error_code(ERROR_NOT_SUPPORTED, std::system_category());
        return INVALID_HANDLE_VALUE;
    }

    UNICODE_STRING unicodeName{};
    unicodeName.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    unicodeName.MaximumLength = unicodeName.Length;
    unicodeName.Buffer = const_cast<PWSTR>(name.data());

    OBJECT_ATTRIBUTES objectAttributes{};
    objectAttributes.Length = sizeof(objectAttributes);
    objectAttributes.RootDirectory = root;
    objectAttributes.ObjectName = &unicodeName;
    objectAttributes.Attributes = kObjCaseInsensitive;

    IO_STATUS_BLOCK ioStatus{};
    HANDLE handle = INVALID_HANDLE_VALUE;
    const auto status = ntCreateFile(&handle,
                                     access | SYNCHRONIZE,
                                     &objectAttributes,
                                     &ioStatus,
                                     nullptr,
                                     attributes,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     createDisposition,
                                     createOptions |
                                         kNtFileOpenReparsePoint |
                                         kNtFileSynchronousIoNonAlert,
                                     nullptr,
                                     0u);
    if (!ntSuccess(status)) {
        error = ntStatusError(status);
        return INVALID_HANDLE_VALUE;
    }
    error.clear();
    return handle;
}

[[nodiscard]] std::wstring widenAscii(std::string_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (const unsigned char c : text)
        result.push_back(static_cast<wchar_t>(c));
    return result;
}

[[nodiscard]] NativePresetStorageStatus openPinnedRoot(
    const fs::path &baseRoot,
    std::string_view targetPluginId,
    PinnedRoot &root) noexcept {
    root.reset();
    if (!baseRoot.is_absolute())
        return makeStatus(NativePresetStorageError::InvalidBaseRoot, baseRoot);

    const HANDLE base = ::CreateFileW(
        baseRoot.c_str(),
        kDirectoryAccess,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (base == INVALID_HANDLE_VALUE)
        return makeStatus(
            NativePresetStorageError::InvalidBaseRoot,
            baseRoot,
            std::error_code(static_cast<int>(::GetLastError()), std::system_category()));

    PinnedRoot parent{base};
    std::error_code reparseError;
    if (handleIsReparsePoint(parent.get(), reparseError) || reparseError)
        return makeStatus(NativePresetStorageError::OutsideRoot,
                          baseRoot,
                          reparseError);

    const std::array<std::string_view, 3> components{{
        kStorageNamespace,
        kPresetDirectoryName,
        targetPluginId,
    }};

    fs::path displayPath = baseRoot;
    for (const auto component : components) {
        displayPath /= std::string{component};
        std::error_code openError;
        const auto childName = widenAscii(component);
        HANDLE child = ntOpenRelative(parent.get(),
                                      childName,
                                      kDirectoryAccess,
                                      kNtFileOpenIf,
                                      kNtFileDirectoryFile,
                                      FILE_ATTRIBUTE_DIRECTORY,
                                      openError);
        if (child == INVALID_HANDLE_VALUE)
            return makeStatus(NativePresetStorageError::CreateDirectoryFailed,
                              displayPath,
                              openError);
        PinnedRoot childRoot{child};
        std::error_code childReparseError;
        if (handleIsReparsePoint(childRoot.get(), childReparseError) ||
            childReparseError)
            return makeStatus(NativePresetStorageError::OutsideRoot,
                              displayPath,
                              childReparseError);
        parent = std::move(childRoot);
    }

    root = std::move(parent);
    return {};
}

[[nodiscard]] RelativeEntryKind inspectRelativeEntry(
    const PinnedRoot &root,
    std::string_view identity,
    std::error_code &error) noexcept {
    const auto name = widenAscii(identity);
    HANDLE handle = ntOpenRelative(root.get(),
                                   name,
                                   0x0080u, // FILE_READ_ATTRIBUTES
                                   kNtFileOpen,
                                   0u,
                                   FILE_ATTRIBUTE_NORMAL,
                                   error);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error.value() == ERROR_FILE_NOT_FOUND ||
            error.value() == ERROR_PATH_NOT_FOUND) {
            error.clear();
            return RelativeEntryKind::Missing;
        }
        return RelativeEntryKind::Other;
    }
    PinnedRoot entry{handle};

    std::error_code reparseError;
    if (handleIsReparsePoint(entry.get(), reparseError)) {
        error.clear();
        return RelativeEntryKind::SymlinkOrReparse;
    }
    if (reparseError) {
        error = reparseError;
        return RelativeEntryKind::Other;
    }

    FILE_STANDARD_INFO standard{};
    if (::GetFileInformationByHandleEx(entry.get(),
                                       FileStandardInfo,
                                       &standard,
                                       sizeof(standard)) == 0) {
        error = std::error_code(static_cast<int>(::GetLastError()),
                                std::system_category());
        return RelativeEntryKind::Other;
    }
    error.clear();
    return standard.Directory ? RelativeEntryKind::Directory
                              : RelativeEntryKind::Regular;
}

[[nodiscard]] NativePresetStorageStatus readRelativeFile(
    const PinnedRoot &root,
    std::string_view identity,
    const fs::path &displayPath,
    std::string &bytes) noexcept {
    bytes.clear();
    std::error_code openError;
    const auto name = widenAscii(identity);
    HANDLE handle = ntOpenRelative(root.get(),
                                   name,
                                   GENERIC_READ,
                                   kNtFileOpen,
                                   kNtFileNonDirectoryFile,
                                   FILE_ATTRIBUTE_NORMAL,
                                   openError);
    if (handle == INVALID_HANDLE_VALUE) {
        if (openError.value() == ERROR_FILE_NOT_FOUND ||
            openError.value() == ERROR_PATH_NOT_FOUND)
            return makeStatus(NativePresetStorageError::NotFound,
                              displayPath,
                              openError);
        return makeStatus(NativePresetStorageError::OpenFailed,
                          displayPath,
                          openError);
    }
    PinnedRoot file{handle};

    std::error_code reparseError;
    if (handleIsReparsePoint(file.get(), reparseError))
        return makeStatus(NativePresetStorageError::OutsideRoot, displayPath);
    if (reparseError)
        return makeStatus(NativePresetStorageError::ReadFailed,
                          displayPath,
                          reparseError);

    LARGE_INTEGER size{};
    if (::GetFileSizeEx(file.get(), &size) == 0 || size.QuadPart < 0)
        return makeStatus(
            NativePresetStorageError::ReadFailed,
            displayPath,
            std::error_code(static_cast<int>(::GetLastError()), std::system_category()));
    if (static_cast<std::uint64_t>(size.QuadPart) > kMaxNativePresetFileBytes)
        return makeStatus(NativePresetStorageError::InputTooLarge,
                          displayPath,
                          {},
                          PresetCodecError::InputTooLarge);

    try {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
    } catch (...) {
        return makeStatus(NativePresetStorageError::ReadFailed, displayPath);
    }

    std::size_t offset = 0u;
    while (offset < bytes.size()) {
        const auto remainingSize = bytes.size() - offset;
        const DWORD remaining = remainingSize > std::numeric_limits<DWORD>::max()
                                    ? std::numeric_limits<DWORD>::max()
                                    : static_cast<DWORD>(remainingSize);
        DWORD read = 0u;
        if (::ReadFile(file.get(), bytes.data() + offset, remaining, &read, nullptr) == 0 ||
            read == 0u) {
            const auto error = std::error_code(static_cast<int>(::GetLastError()),
                                               std::system_category());
            bytes.clear();
            return makeStatus(NativePresetStorageError::ReadFailed,
                              displayPath,
                              error);
        }
        offset += static_cast<std::size_t>(read);
    }
    return {};
}

[[nodiscard]] NativePresetStorageStatus createAndWriteTemporary(
    const PinnedRoot &root,
    std::string_view temporaryIdentity,
    const fs::path &displayPath,
    std::string_view bytes,
    const NativePresetStorageOptions &options,
    HANDLE &temporaryHandle) noexcept {
    temporaryHandle = INVALID_HANDLE_VALUE;
    const auto shouldFail = [&options](NativePresetWriteStage stage) noexcept {
        return options.shouldFailWrite != nullptr &&
               options.shouldFailWrite(stage, options.faultUserData);
    };

    std::error_code openError;
    const auto name = widenAscii(temporaryIdentity);
    HANDLE handle = ntOpenRelative(root.get(),
                                   name,
                                   GENERIC_WRITE | DELETE,
                                   kNtFileCreate,
                                   kNtFileNonDirectoryFile | kNtFileWriteThrough,
                                   FILE_ATTRIBUTE_NORMAL,
                                   openError);
    if (handle == INVALID_HANDLE_VALUE) {
        if (openError.value() == ERROR_ALREADY_EXISTS ||
            openError.value() == ERROR_FILE_EXISTS)
            return makeStatus(NativePresetStorageError::AlreadyExists,
                              displayPath,
                              openError);
        return makeStatus(NativePresetStorageError::OpenFailed,
                          displayPath,
                          openError);
    }
    temporaryHandle = handle;

    if (shouldFail(NativePresetWriteStage::AfterTemporaryOpen))
        return makeStatus(NativePresetStorageError::WriteFailed, displayPath);

    const auto writeRange = [handle, &displayPath](const char *data,
                                                   std::size_t size) noexcept {
        std::size_t offset = 0u;
        while (offset < size) {
            const auto remainingSize = size - offset;
            const DWORD remaining = remainingSize > std::numeric_limits<DWORD>::max()
                                        ? std::numeric_limits<DWORD>::max()
                                        : static_cast<DWORD>(remainingSize);
            DWORD written = 0u;
            if (::WriteFile(handle,
                            data + offset,
                            remaining,
                            &written,
                            nullptr) == 0 || written == 0u)
                return makeStatus(
                    NativePresetStorageError::WriteFailed,
                    displayPath,
                    std::error_code(static_cast<int>(::GetLastError()),
                                    std::system_category()));
            offset += static_cast<std::size_t>(written);
        }
        return NativePresetStorageStatus{};
    };

    const auto firstSize = bytes.size() / 2u;
    auto status = writeRange(bytes.data(), firstSize);
    if (!status.ok())
        return status;
    if (shouldFail(NativePresetWriteStage::AfterPartialWrite))
        return makeStatus(NativePresetStorageError::WriteFailed, displayPath);
    status = writeRange(bytes.data() + firstSize, bytes.size() - firstSize);
    if (!status.ok())
        return status;

    if (::FlushFileBuffers(handle) == 0)
        return makeStatus(
            NativePresetStorageError::FlushFailed,
            displayPath,
            std::error_code(static_cast<int>(::GetLastError()), std::system_category()));
    return {};
}

[[nodiscard]] NativePresetStorageStatus renameTemporaryRelative(
    HANDLE temporaryHandle,
    const PinnedRoot &root,
    std::string_view destinationIdentity,
    const fs::path &displayPath,
    bool replaceExisting) noexcept {
    const auto destination = widenAscii(destinationIdentity);
    const auto fileNameBytes = destination.size() * sizeof(wchar_t);
    const auto allocationSize = offsetof(FILE_RENAME_INFO, FileName) + fileNameBytes;
    std::vector<std::byte> storage;
    try {
        storage.resize(allocationSize);
    } catch (...) {
        return makeStatus(NativePresetStorageError::ReplaceFailed, displayPath);
    }

    auto *renameInfo = reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
    std::memset(renameInfo, 0, allocationSize);
    renameInfo->ReplaceIfExists = replaceExisting ? TRUE : FALSE;
    // The temporary handle already names a file inside the pinned root. A simple
    // destination basename with a null RootDirectory is therefore a same-directory
    // rename and cannot re-resolve any intermediate path component.
    renameInfo->RootDirectory = nullptr;
    renameInfo->FileNameLength = static_cast<DWORD>(fileNameBytes);
    if (fileNameBytes != 0u)
        std::memcpy(renameInfo->FileName, destination.data(), fileNameBytes);

    if (::SetFileInformationByHandle(temporaryHandle,
                                     FileRenameInfo,
                                     renameInfo,
                                     static_cast<DWORD>(allocationSize)) != 0)
        return {};

    const auto error = std::error_code(static_cast<int>(::GetLastError()),
                                       std::system_category());
    if (!replaceExisting) {
        std::error_code inspectError;
        const auto kind = inspectRelativeEntry(root, destinationIdentity, inspectError);
        if (kind != RelativeEntryKind::Missing)
            return makeStatus(NativePresetStorageError::AlreadyExists,
                              displayPath,
                              error);
    }
    return makeStatus(NativePresetStorageError::ReplaceFailed,
                      displayPath,
                      error);
}

[[nodiscard]] NativePresetStorageStatus deleteRelativeEntry(
    const PinnedRoot &root,
    std::string_view identity,
    const fs::path &displayPath) noexcept {
    std::error_code openError;
    const auto name = widenAscii(identity);
    HANDLE handle = ntOpenRelative(root.get(),
                                   name,
                                   DELETE | 0x0080u,
                                   kNtFileOpen,
                                   kNtFileNonDirectoryFile,
                                   FILE_ATTRIBUTE_NORMAL,
                                   openError);
    if (handle == INVALID_HANDLE_VALUE) {
        if (openError.value() == ERROR_FILE_NOT_FOUND ||
            openError.value() == ERROR_PATH_NOT_FOUND)
            return makeStatus(NativePresetStorageError::NotFound,
                              displayPath,
                              openError);
        return makeStatus(NativePresetStorageError::DeleteFailed,
                          displayPath,
                          openError);
    }
    PinnedRoot file{handle};

    std::error_code reparseError;
    if (handleIsReparsePoint(file.get(), reparseError))
        return makeStatus(NativePresetStorageError::OutsideRoot, displayPath);
    if (reparseError)
        return makeStatus(NativePresetStorageError::DeleteFailed,
                          displayPath,
                          reparseError);

    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (::SetFileInformationByHandle(file.get(),
                                     FileDispositionInfo,
                                     &disposition,
                                     sizeof(disposition)) == 0)
        return makeStatus(
            NativePresetStorageError::DeleteFailed,
            displayPath,
            std::error_code(static_cast<int>(::GetLastError()), std::system_category()));
    return {};
}

[[nodiscard]] NativePresetStorageStatus listRelativeIdentities(
    const PinnedRoot &root,
    std::vector<std::string> &identities,
    const fs::path &displayRoot) noexcept {
    identities.clear();
    std::array<std::byte, 64u * 1024u> buffer{};
    bool restart = true;

    for (;;) {
        const auto infoClass = restart ? FileIdBothDirectoryRestartInfo
                                       : FileIdBothDirectoryInfo;
        if (::GetFileInformationByHandleEx(root.get(),
                                           infoClass,
                                           buffer.data(),
                                           static_cast<DWORD>(buffer.size())) == 0) {
            const auto lastError = ::GetLastError();
            if (lastError == ERROR_NO_MORE_FILES)
                break;
            return makeStatus(
                NativePresetStorageError::EnumerateFailed,
                displayRoot,
                std::error_code(static_cast<int>(lastError), std::system_category()));
        }
        restart = false;

        auto *entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO *>(buffer.data());
        for (;;) {
            const std::wstring_view wideName{
                entry->FileName,
                entry->FileNameLength / sizeof(wchar_t)};
            if (wideName != L"." && wideName != L"..") {
                try {
                    const auto identity = fs::path{std::wstring{wideName}}.string();
                    identities.push_back(identity);
                } catch (...) {
                    // An unrepresentable native filename cannot be a valid ASCII
                    // storage identity; omit it from the preset namespace.
                }
            }
            if (entry->NextEntryOffset == 0u)
                break;
            entry = reinterpret_cast<FILE_ID_BOTH_DIR_INFO *>(
                reinterpret_cast<std::byte *>(entry) + entry->NextEntryOffset);
        }
    }
    return {};
}

#else

[[nodiscard]] int directoryOpenFlags() noexcept {
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return flags;
}

[[nodiscard]] NativePresetStorageStatus openPinnedRoot(
    const fs::path &baseRoot,
    std::string_view targetPluginId,
    PinnedRoot &root) noexcept {
    root.reset();
    if (!baseRoot.is_absolute())
        return makeStatus(NativePresetStorageError::InvalidBaseRoot, baseRoot);

    const int baseFd = ::open(baseRoot.c_str(), directoryOpenFlags());
    if (baseFd < 0)
        return makeStatus(NativePresetStorageError::InvalidBaseRoot,
                          baseRoot,
                          std::error_code(errno, std::generic_category()));
    PinnedRoot parent{baseFd};

    const std::array<std::string_view, 3> components{{
        kStorageNamespace,
        kPresetDirectoryName,
        targetPluginId,
    }};
    fs::path displayPath = baseRoot;

    for (const auto component : components) {
        displayPath /= std::string{component};
        const std::string name{component};
        if (::mkdirat(parent.get(), name.c_str(), 0700) != 0 && errno != EEXIST)
            return makeStatus(NativePresetStorageError::CreateDirectoryFailed,
                              displayPath,
                              std::error_code(errno, std::generic_category()));

        const int childFd = ::openat(parent.get(),
                                     name.c_str(),
                                     directoryOpenFlags());
        if (childFd < 0) {
            const int openError = errno;
            const auto storageError =
#ifdef ELOOP
                openError == ELOOP ? NativePresetStorageError::OutsideRoot :
#endif
                NativePresetStorageError::CreateDirectoryFailed;
            return makeStatus(storageError,
                              displayPath,
                              std::error_code(openError, std::generic_category()));
        }
        PinnedRoot child{childFd};
        struct stat status {};
        if (::fstat(child.get(), &status) != 0 || !S_ISDIR(status.st_mode))
            return makeStatus(NativePresetStorageError::OutsideRoot,
                              displayPath,
                              std::error_code(errno, std::generic_category()));
        parent = std::move(child);
    }

    root = std::move(parent);
    return {};
}

[[nodiscard]] RelativeEntryKind inspectRelativeEntry(
    const PinnedRoot &root,
    std::string_view identity,
    std::error_code &error) noexcept {
    const std::string name{identity};
    struct stat status {};
    if (::fstatat(root.get(),
                  name.c_str(),
                  &status,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            error.clear();
            return RelativeEntryKind::Missing;
        }
        error = std::error_code(errno, std::generic_category());
        return RelativeEntryKind::Other;
    }
    error.clear();
    if (S_ISLNK(status.st_mode))
        return RelativeEntryKind::SymlinkOrReparse;
    if (S_ISREG(status.st_mode))
        return RelativeEntryKind::Regular;
    if (S_ISDIR(status.st_mode))
        return RelativeEntryKind::Directory;
    return RelativeEntryKind::Other;
}

[[nodiscard]] NativePresetStorageStatus readRelativeFile(
    const PinnedRoot &root,
    std::string_view identity,
    const fs::path &displayPath,
    std::string &bytes) noexcept {
    bytes.clear();
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const std::string name{identity};
    const int fd = ::openat(root.get(), name.c_str(), flags);
    if (fd < 0) {
        const int openError = errno;
#ifdef ELOOP
        if (openError == ELOOP)
            return makeStatus(NativePresetStorageError::OutsideRoot,
                              displayPath,
                              std::error_code(openError, std::generic_category()));
#endif
        if (openError == ENOENT)
            return makeStatus(NativePresetStorageError::NotFound,
                              displayPath,
                              std::error_code(openError, std::generic_category()));
        return makeStatus(NativePresetStorageError::OpenFailed,
                          displayPath,
                          std::error_code(openError, std::generic_category()));
    }
    PinnedRoot file{fd};

    struct stat status {};
    if (::fstat(file.get(), &status) != 0)
        return makeStatus(NativePresetStorageError::ReadFailed,
                          displayPath,
                          std::error_code(errno, std::generic_category()));
    if (!S_ISREG(status.st_mode))
        return makeStatus(NativePresetStorageError::OutsideRoot, displayPath);
    if (status.st_size < 0 ||
        static_cast<std::uint64_t>(status.st_size) > kMaxNativePresetFileBytes)
        return makeStatus(NativePresetStorageError::InputTooLarge,
                          displayPath,
                          {},
                          PresetCodecError::InputTooLarge);

    try {
        bytes.resize(static_cast<std::size_t>(status.st_size));
    } catch (...) {
        return makeStatus(NativePresetStorageError::ReadFailed, displayPath);
    }

    std::size_t offset = 0u;
    while (offset < bytes.size()) {
        const auto read = ::read(file.get(),
                                 bytes.data() + offset,
                                 bytes.size() - offset);
        if (read < 0 && errno == EINTR)
            continue;
        if (read <= 0) {
            const int readError = read < 0 ? errno : EIO;
            bytes.clear();
            return makeStatus(NativePresetStorageError::ReadFailed,
                              displayPath,
                              std::error_code(readError, std::generic_category()));
        }
        offset += static_cast<std::size_t>(read);
    }
    return {};
}

[[nodiscard]] NativePresetStorageStatus createAndWriteTemporary(
    const PinnedRoot &root,
    std::string_view temporaryIdentity,
    const fs::path &displayPath,
    std::string_view bytes,
    const NativePresetStorageOptions &options,
    int &temporaryFd) noexcept {
    temporaryFd = -1;
    const auto shouldFail = [&options](NativePresetWriteStage stage) noexcept {
        return options.shouldFailWrite != nullptr &&
               options.shouldFailWrite(stage, options.faultUserData);
    };

    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const std::string temporaryName{temporaryIdentity};
    const int fd = ::openat(root.get(), temporaryName.c_str(), flags, 0600);
    if (fd < 0) {
        const int openError = errno;
        if (openError == EEXIST)
            return makeStatus(NativePresetStorageError::AlreadyExists,
                              displayPath,
                              std::error_code(openError, std::generic_category()));
        return makeStatus(NativePresetStorageError::OpenFailed,
                          displayPath,
                          std::error_code(openError, std::generic_category()));
    }
    temporaryFd = fd;

    if (shouldFail(NativePresetWriteStage::AfterTemporaryOpen))
        return makeStatus(NativePresetStorageError::WriteFailed, displayPath);

    const auto writeRange = [fd, &displayPath](const char *data,
                                               std::size_t size) noexcept {
        std::size_t offset = 0u;
        while (offset < size) {
            const auto written = ::write(fd, data + offset, size - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                const int writeError = written < 0 ? errno : EIO;
                return makeStatus(NativePresetStorageError::WriteFailed,
                                  displayPath,
                                  std::error_code(writeError, std::generic_category()));
            }
            offset += static_cast<std::size_t>(written);
        }
        return NativePresetStorageStatus{};
    };

    const auto firstSize = bytes.size() / 2u;
    auto status = writeRange(bytes.data(), firstSize);
    if (!status.ok())
        return status;
    if (shouldFail(NativePresetWriteStage::AfterPartialWrite))
        return makeStatus(NativePresetStorageError::WriteFailed, displayPath);
    status = writeRange(bytes.data() + firstSize, bytes.size() - firstSize);
    if (!status.ok())
        return status;

    while (::fsync(fd) != 0) {
        if (errno == EINTR)
            continue;
        return makeStatus(NativePresetStorageError::FlushFailed,
                          displayPath,
                          std::error_code(errno, std::generic_category()));
    }
    return {};
}

[[nodiscard]] NativePresetStorageStatus commitTemporaryRelative(
    const PinnedRoot &root,
    std::string_view temporaryIdentity,
    std::string_view destinationIdentity,
    const fs::path &displayPath,
    bool replaceExisting,
    const NativePresetStorageOptions &options) noexcept {
    const std::string temporary{temporaryIdentity};
    const std::string destination{destinationIdentity};

    if (replaceExisting) {
        if (::renameat(root.get(),
                       temporary.c_str(),
                       root.get(),
                       destination.c_str()) == 0) {
            while (::fsync(root.get()) != 0 && errno == EINTR) {
            }
            return {};
        }
        return makeStatus(NativePresetStorageError::ReplaceFailed,
                          displayPath,
                          std::error_code(errno, std::generic_category()));
    }

    if (::linkat(root.get(),
                 temporary.c_str(),
                 root.get(),
                 destination.c_str(),
                 0) != 0) {
        const int linkError = errno;
        if (linkError == EEXIST)
            return makeStatus(NativePresetStorageError::AlreadyExists,
                              displayPath,
                              std::error_code(linkError, std::generic_category()));
        return makeStatus(NativePresetStorageError::ReplaceFailed,
                          displayPath,
                          std::error_code(linkError, std::generic_category()));
    }

    // The destination became atomically visible at successful linkat(). From
    // this point onward the save is committed and must never be rolled back just
    // because removing the private temporary name fails.
    const bool simulateCleanupFailure =
        options.shouldFailWrite != nullptr &&
        options.shouldFailWrite(NativePresetWriteStage::BeforeTemporaryCleanup,
                                options.faultUserData);
    if (!simulateCleanupFailure)
        (void)::unlinkat(root.get(), temporary.c_str(), 0);
    while (::fsync(root.get()) != 0 && errno == EINTR) {
    }
    return {};
}

[[nodiscard]] NativePresetStorageStatus deleteRelativeEntry(
    const PinnedRoot &root,
    std::string_view identity,
    const fs::path &displayPath) noexcept {
    std::error_code inspectError;
    const auto kind = inspectRelativeEntry(root, identity, inspectError);
    if (kind == RelativeEntryKind::Missing)
        return makeStatus(NativePresetStorageError::NotFound, displayPath);
    if (kind == RelativeEntryKind::SymlinkOrReparse)
        return makeStatus(NativePresetStorageError::OutsideRoot, displayPath);
    if (kind != RelativeEntryKind::Regular)
        return makeStatus(NativePresetStorageError::DeleteFailed,
                          displayPath,
                          inspectError);

    const std::string name{identity};
    if (::unlinkat(root.get(), name.c_str(), 0) != 0)
        return makeStatus(NativePresetStorageError::DeleteFailed,
                          displayPath,
                          std::error_code(errno, std::generic_category()));
    while (::fsync(root.get()) != 0 && errno == EINTR) {
    }
    return {};
}

[[nodiscard]] NativePresetStorageStatus listRelativeIdentities(
    const PinnedRoot &root,
    std::vector<std::string> &identities,
    const fs::path &displayRoot) noexcept {
    identities.clear();
    const int duplicateFd = ::dup(root.get());
    if (duplicateFd < 0)
        return makeStatus(NativePresetStorageError::EnumerateFailed,
                          displayRoot,
                          std::error_code(errno, std::generic_category()));

    DIR *directory = ::fdopendir(duplicateFd);
    if (!directory) {
        const int directoryError = errno;
        (void)::close(duplicateFd);
        return makeStatus(NativePresetStorageError::EnumerateFailed,
                          displayRoot,
                          std::error_code(directoryError, std::generic_category()));
    }

    errno = 0;
    while (const auto *entry = ::readdir(directory)) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0)
            continue;
        try {
            identities.emplace_back(entry->d_name);
        } catch (...) {
            (void)::closedir(directory);
            return makeStatus(NativePresetStorageError::EnumerateFailed,
                              displayRoot);
        }
        errno = 0;
    }
    const int enumerationError = errno;
    (void)::closedir(directory);
    if (enumerationError != 0)
        return makeStatus(NativePresetStorageError::EnumerateFailed,
                          displayRoot,
                          std::error_code(enumerationError, std::generic_category()));
    return {};
}

#endif

[[nodiscard]] NativePresetStorageStatus cleanupTemporaryRelative(
    const PinnedRoot &root,
    std::string_view temporaryIdentity) noexcept {
#if defined(_WIN32)
    return deleteRelativeEntry(root,
                               temporaryIdentity,
                               fs::path{std::string{temporaryIdentity}});
#else
    const std::string name{temporaryIdentity};
    if (::unlinkat(root.get(), name.c_str(), 0) == 0 || errno == ENOENT)
        return {};
    return makeStatus(NativePresetStorageError::CleanupFailed,
                      fs::path{name},
                      std::error_code(errno, std::generic_category()));
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

NativePresetStorageStatus NativePresetStorage::ensureReadyUnlocked() const noexcept {
    if (!scopeStatus_.ok())
        return scopeStatus_;
    PinnedRoot root;
    return openPinnedRoot(baseRoot_, targetPluginId_, root);
}

NativePresetListResult NativePresetStorage::list() const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    NativePresetListResult result;
    if (!scopeStatus_.ok()) {
        result.status = scopeStatus_;
        return result;
    }

    PinnedRoot pinnedRoot;
    result.status = openPinnedRoot(baseRoot_, targetPluginId_, pinnedRoot);
    if (!result.status.ok())
        return result;

    std::vector<std::string> identities;
    result.status = listRelativeIdentities(pinnedRoot, identities, root_);
    if (!result.status.ok())
        return result;

    for (const auto &identity : identities) {
        fs::path entryPath = root_ / identity;
        if (fs::path{identity}.extension() != kPresetSuffix)
            continue;
        if (!validIdentity(identity)) {
            result.diagnostics.push_back(
                {makeStatus(NativePresetStorageError::InvalidIdentity, entryPath)});
            continue;
        }

        std::error_code inspectError;
        const auto kind = inspectRelativeEntry(pinnedRoot, identity, inspectError);
        if (kind == RelativeEntryKind::SymlinkOrReparse) {
            result.diagnostics.push_back(
                {makeStatus(NativePresetStorageError::OutsideRoot, entryPath)});
            continue;
        }
        if (kind != RelativeEntryKind::Regular) {
            if (inspectError)
                result.diagnostics.push_back(
                    {makeStatus(NativePresetStorageError::EnumerateFailed,
                                entryPath,
                                inspectError)});
            continue;
        }

        std::string bytes;
        const auto readStatus = readRelativeFile(pinnedRoot,
                                                 identity,
                                                 entryPath,
                                                 bytes);
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
                {makeStatus(storageError, entryPath, {}, metadata.error)});
            continue;
        }
        result.entries.push_back({identity, entryPath, metadata.metadata});
    }

    std::sort(result.entries.begin(), result.entries.end(),
              [](const auto &a, const auto &b) {
                  return a.identity < b.identity;
              });
    return result;
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
    if (!scopeStatus_.ok()) {
        result.status = scopeStatus_;
        return result;
    }

    PinnedRoot pinnedRoot;
    result.status = openPinnedRoot(baseRoot_, targetPluginId_, pinnedRoot);
    if (!result.status.ok())
        return result;

    if (shouldFail(NativePresetWriteStage::AfterRootPinned)) {
        result.status = makeStatus(NativePresetStorageError::WriteFailed, root_);
        return result;
    }

    std::error_code inspectError;
    const auto kind = inspectRelativeEntry(pinnedRoot, identity, inspectError);
    if (kind == RelativeEntryKind::SymlinkOrReparse) {
        result.status = makeStatus(NativePresetStorageError::OutsideRoot,
                                   root_ / std::string{identity});
        return result;
    }
    if (!overwrite && kind != RelativeEntryKind::Missing) {
        result.status = makeStatus(NativePresetStorageError::AlreadyExists,
                                   root_ / std::string{identity},
                                   inspectError);
        return result;
    }
    if (overwrite && kind != RelativeEntryKind::Regular) {
        result.status = makeStatus(NativePresetStorageError::NotFound,
                                   root_ / std::string{identity},
                                   inspectError);
        return result;
    }

    const auto destinationPath = root_ / std::string{identity};
    for (std::size_t attempt = 0u; attempt < kMaxTemporaryAttempts; ++attempt) {
        const auto temporaryIdentity = temporaryNameFor(identity, attempt);
        const auto temporaryPath = root_ / temporaryIdentity;
#if defined(_WIN32)
        HANDLE rawTemporary = INVALID_HANDLE_VALUE;
        auto writeStatus = createAndWriteTemporary(pinnedRoot,
                                                   temporaryIdentity,
                                                   temporaryPath,
                                                   serialized.bytes,
                                                   options_,
                                                   rawTemporary);
        PinnedRoot temporary{rawTemporary};
        if (writeStatus.error == NativePresetStorageError::AlreadyExists)
            continue;
        if (!writeStatus.ok()) {
            temporary.reset();
            (void)cleanupTemporaryRelative(pinnedRoot, temporaryIdentity);
            result.status = writeStatus;
            return result;
        }
        if (shouldFail(NativePresetWriteStage::BeforeReplace)) {
            temporary.reset();
            (void)cleanupTemporaryRelative(pinnedRoot, temporaryIdentity);
            result.status = makeStatus(NativePresetStorageError::ReplaceFailed,
                                       destinationPath);
            return result;
        }
        auto commitStatus = renameTemporaryRelative(temporary.get(),
                                                    pinnedRoot,
                                                    identity,
                                                    destinationPath,
                                                    overwrite);
        temporary.reset();
        if (!commitStatus.ok())
            (void)cleanupTemporaryRelative(pinnedRoot, temporaryIdentity);
#else
        int rawTemporary = -1;
        auto writeStatus = createAndWriteTemporary(pinnedRoot,
                                                   temporaryIdentity,
                                                   temporaryPath,
                                                   serialized.bytes,
                                                   options_,
                                                   rawTemporary);
        PinnedRoot temporary{rawTemporary};
        if (writeStatus.error == NativePresetStorageError::AlreadyExists)
            continue;
        if (!writeStatus.ok()) {
            temporary.reset();
            (void)cleanupTemporaryRelative(pinnedRoot, temporaryIdentity);
            result.status = writeStatus;
            return result;
        }
        temporary.reset(); // flushed file is closed before its atomic commit
        if (shouldFail(NativePresetWriteStage::BeforeReplace)) {
            (void)cleanupTemporaryRelative(pinnedRoot, temporaryIdentity);
            result.status = makeStatus(NativePresetStorageError::ReplaceFailed,
                                       destinationPath);
            return result;
        }
        auto commitStatus = commitTemporaryRelative(pinnedRoot,
                                                    temporaryIdentity,
                                                    identity,
                                                    destinationPath,
                                                    overwrite,
                                                    options_);
        if (!commitStatus.ok())
            (void)cleanupTemporaryRelative(pinnedRoot, temporaryIdentity);
#endif
        result.status = commitStatus;
        if (!result.status.ok())
            return result;
        result.identity.assign(identity.data(), identity.size());
        result.path = destinationPath;
        return result;
    }

    result.status = makeStatus(NativePresetStorageError::Collision,
                               destinationPath);
    return result;
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
    if (!scopeStatus_.ok()) {
        result.status = scopeStatus_;
        return result;
    }

    PinnedRoot pinnedRoot;
    result.status = openPinnedRoot(baseRoot_, targetPluginId_, pinnedRoot);
    if (!result.status.ok())
        return result;

    const auto sourcePath = root_ / std::string{identity};
    std::error_code inspectError;
    const auto kind = inspectRelativeEntry(pinnedRoot, identity, inspectError);
    if (kind == RelativeEntryKind::SymlinkOrReparse) {
        result.status = makeStatus(NativePresetStorageError::OutsideRoot, sourcePath);
        return result;
    }
    if (kind != RelativeEntryKind::Regular) {
        result.status = makeStatus(NativePresetStorageError::NotFound,
                                   sourcePath,
                                   inspectError);
        return result;
    }

    std::string bytes;
    result.status = readRelativeFile(pinnedRoot, identity, sourcePath, bytes);
    if (!result.status.ok())
        return result;

    const auto parsed = parsePresetDocument(bytes, targetPluginId_);
    if (!parsed.ok()) {
        result.status = makeStatus(
            parsed.error == PresetCodecError::WrongTargetPlugin
                ? NativePresetStorageError::WrongTargetPlugin
                : NativePresetStorageError::ParseFailed,
            sourcePath,
            {},
            parsed.error);
        return result;
    }

    result.document = parsed.document;
    return result;
}

NativePresetStorageStatus NativePresetStorage::remove(
    std::string_view identity) noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!validIdentity(identity))
        return makeStatus(NativePresetStorageError::InvalidIdentity, root_);
    if (!scopeStatus_.ok())
        return scopeStatus_;

    PinnedRoot pinnedRoot;
    auto status = openPinnedRoot(baseRoot_, targetPluginId_, pinnedRoot);
    if (!status.ok())
        return status;
    return deleteRelativeEntry(pinnedRoot,
                               identity,
                               root_ / std::string{identity});
}

} // namespace webview_gui::examples::presets
