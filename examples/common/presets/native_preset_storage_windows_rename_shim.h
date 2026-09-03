#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

namespace webview_gui_native_preset_windows_detail {

using NtSetInformationFileFn = NTSTATUS(NTAPI *)(
    HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
using RtlNtStatusToDosErrorFn = ULONG(NTAPI *)(NTSTATUS);

[[nodiscard]] inline NtSetInformationFileFn ntSetInformationFile() noexcept {
    static const auto function = reinterpret_cast<NtSetInformationFileFn>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
    return function;
}

[[nodiscard]] inline DWORD ntStatusToWin32(NTSTATUS status) noexcept {
    static const auto converter = reinterpret_cast<RtlNtStatusToDosErrorFn>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError"));
    if (!converter)
        return ERROR_GEN_FAILURE;
    const auto error = converter(status);
    return error <= std::numeric_limits<DWORD>::max()
               ? static_cast<DWORD>(error)
               : ERROR_GEN_FAILURE;
}

[[nodiscard]] inline BOOL setFileInformationByHandleRelativeToRoot(
    HANDLE file,
    FILE_INFO_BY_HANDLE_CLASS informationClass,
    LPVOID information,
    DWORD informationSize,
    HANDLE rootDirectory) noexcept {
    if (informationClass != FileRenameInfo)
        return ::SetFileInformationByHandle(file,
                                            informationClass,
                                            information,
                                            informationSize);

    const auto ntSetInformation = ntSetInformationFile();
    if (!ntSetInformation || information == nullptr) {
        ::SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    constexpr auto sourceHeaderBytes = offsetof(FILE_RENAME_INFO, FileName);
    if (informationSize < sourceHeaderBytes) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    const auto *source = static_cast<const FILE_RENAME_INFO *>(information);
    if ((source->FileNameLength % sizeof(wchar_t)) != 0u ||
        static_cast<std::size_t>(source->FileNameLength) >
            static_cast<std::size_t>(informationSize) - sourceHeaderBytes) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    struct NtRenameInformation {
        BOOLEAN replaceIfExists;
        HANDLE rootDirectory;
        ULONG fileNameLength;
        WCHAR fileName[1];
    };

    const auto fileNameBytes = static_cast<std::size_t>(source->FileNameLength);
    const auto allocationSize = offsetof(NtRenameInformation, fileName) + fileNameBytes;
    if (allocationSize > std::numeric_limits<ULONG>::max()) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    std::vector<std::byte> storage;
    try {
        storage.resize(allocationSize);
    } catch (...) {
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    auto *rename = reinterpret_cast<NtRenameInformation *>(storage.data());
    std::memset(rename, 0, allocationSize);
    rename->replaceIfExists = source->ReplaceIfExists ? TRUE : FALSE;
    rename->rootDirectory = rootDirectory;
    rename->fileNameLength = source->FileNameLength;
    if (fileNameBytes != 0u)
        std::memcpy(rename->fileName, source->FileName, fileNameBytes);

    IO_STATUS_BLOCK ioStatus{};
    const auto status = ntSetInformation(
        file,
        &ioStatus,
        rename,
        static_cast<ULONG>(allocationSize),
        static_cast<FILE_INFORMATION_CLASS>(10)); // FileRenameInformation
    if (status >= 0)
        return TRUE;

    ::SetLastError(ntStatusToWin32(status));
    return FALSE;
}

} // namespace webview_gui_native_preset_windows_detail

// The implementation's two SetFileInformationByHandle calls are both in
// functions that already hold the verified PinnedRoot as `root`. Inject that
// handle only for FileRenameInfo; disposition updates delegate to Win32 unchanged.
#define SetFileInformationByHandle(file, informationClass, information, informationSize) \
    webview_gui_native_preset_windows_detail::setFileInformationByHandleRelativeToRoot( \
        (file), (informationClass), (information), (informationSize), root.get())

#endif
