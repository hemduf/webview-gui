#include "webview-gui/_impl/platform/windows_plugin_runtime.h"
#include "webview-gui/_impl/platform/choc_plugin_webview.h"

#if !defined(_WIN32)
#error Windows-only test module
#endif

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

std::vector<std::unique_ptr<choc::ui::WebView>>& retainedViews()
{
    static std::vector<std::unique_ptr<choc::ui::WebView>> views;
    return views;
}

std::unique_ptr<webview_gui::detail::ScopedCOMApartment>& apartment()
{
    static std::unique_ptr<webview_gui::detail::ScopedCOMApartment> value;
    return value;
}

void copyWide(const std::wstring& source, wchar_t* output, std::size_t capacity)
{
    if (!output || capacity == 0) return;
    const auto count = (std::min)(capacity - 1, source.size());
    std::wmemcpy(output, source.data(), count);
    output[count] = L'\0';
}

} // namespace

extern "C" __declspec(dllexport) bool webview_gui_test_retain_windows_webviews(
    std::size_t count,
    std::uintptr_t* moduleHandleOut,
    std::uintptr_t* firstWindowInstanceOut,
    wchar_t* firstClassName,
    std::size_t firstClassCapacity,
    bool* allOwnedByModule,
    bool* allClassNamesUnique)
{
    auto& views = retainedViews();
    views.clear();
    apartment().reset();

    apartment() = std::make_unique<webview_gui::detail::ScopedCOMApartment>();
    if (!apartment()->ok()) {
        apartment().reset();
        return false;
    }

    const auto module = webview_gui::detail::windowsPluginModuleHandle();
    if (!module) {
        apartment().reset();
        return false;
    }

    std::set<std::wstring> classNames;
    bool owned = true;
    std::wstring firstClass;
    std::uintptr_t firstWindowInstance = 0;

    views.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto view = std::make_unique<choc::ui::WebView>();
        if (!view->loadedOK() || !view->getViewHandle()) {
            views.clear();
            apartment().reset();
            return false;
        }

        auto hwnd = static_cast<HWND>(view->getViewHandle());
        const auto windowInstance = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        owned = owned && windowInstance == module;

        wchar_t className[256] = {};
        if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) <= 0) {
            views.clear();
            apartment().reset();
            return false;
        }

        classNames.emplace(className);
        if (i == 0) {
            firstClass = className;
            firstWindowInstance = reinterpret_cast<std::uintptr_t>(windowInstance);
        }

        views.push_back(std::move(view));
    }

    if (moduleHandleOut)
        *moduleHandleOut = reinterpret_cast<std::uintptr_t>(module);
    if (firstWindowInstanceOut)
        *firstWindowInstanceOut = firstWindowInstance;
    copyWide(firstClass, firstClassName, firstClassCapacity);
    if (allOwnedByModule)
        *allOwnedByModule = owned;
    if (allClassNamesUnique)
        *allClassNamesUnique = classNames.size() == count;

    return count == 0 || (!firstClass.empty() && owned && classNames.size() == count);
}

extern "C" __declspec(dllexport) void webview_gui_test_release_windows_webviews()
{
    retainedViews().clear();
    apartment().reset();
}
