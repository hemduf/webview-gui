#pragma once

#if !defined(_WIN32)
#error windows_plugin_runtime.h is Windows-only
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <cstdint>

namespace webview_gui::detail {

static void windowsPluginModuleAnchor() {}

inline HMODULE windowsPluginModuleHandle() noexcept
{
    HMODULE module = nullptr;
    const auto address = reinterpret_cast<LPCWSTR>(&windowsPluginModuleAnchor);

    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           address,
                           &module) != 0)
        return module;

    return nullptr;
}

inline DWORD nextWindowsClassToken() noexcept
{
    static std::atomic<DWORD> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

// CHOC's WebView currently calls CoInitialize(nullptr) internally without a
// matching CoUninitialize(). The plug-in wrapper owns COM lifetime explicitly,
// so the intercepted CHOC call is intentionally a no-op.
inline HRESULT suppressedCHOCWebViewCoInitialize(void*) noexcept
{
    return S_OK;
}

class ScopedCOMApartment {
public:
    ScopedCOMApartment() noexcept
        : result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
          ownsReference(result == S_OK || result == S_FALSE)
    {
    }

    ScopedCOMApartment(const ScopedCOMApartment&) = delete;
    ScopedCOMApartment& operator=(const ScopedCOMApartment&) = delete;

    ~ScopedCOMApartment()
    {
        if (ownsReference)
            CoUninitialize();
    }

    [[nodiscard]] bool ok() const noexcept
    {
        return result == S_OK || result == S_FALSE;
    }

    [[nodiscard]] bool incompatibleApartment() const noexcept
    {
        return result == RPC_E_CHANGED_MODE;
    }

    [[nodiscard]] HRESULT status() const noexcept { return result; }

private:
    HRESULT result = E_FAIL;
    bool ownsReference = false;
};

} // namespace webview_gui::detail
