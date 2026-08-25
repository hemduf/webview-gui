#include "webview-gui/_impl/platform/windows_plugin_runtime.h"
#include "webview-gui/_impl/platform/choc_plugin_webview.h"

#if !defined(_WIN32)
#error Windows-only test module
#endif

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <iterator>
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

extern "C" __declspec(dllexport) bool webview_gui_test_create_destroy_windows_webviews(
    std::size_t count)
{
    webview_gui::detail::ScopedCOMApartment localApartment;
    if (!localApartment.ok())
        return false;

    std::vector<std::unique_ptr<choc::ui::WebView>> views;
    views.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        auto view = std::make_unique<choc::ui::WebView>();
        if (!view->loadedOK() || !view->getViewHandle())
            return false;
        views.push_back(std::move(view));
    }

    // `views` is destroyed before localApartment because locals are unwound in
    // reverse declaration order, keeping every CHOC destruction on the same STA
    // that constructed it.
    return true;
}

extern "C" __declspec(dllexport) std::uintptr_t webview_gui_test_first_windows_hwnd()
{
    const auto& views = retainedViews();
    if (views.empty() || !views.front() || !views.front()->getViewHandle())
        return 0;

    return reinterpret_cast<std::uintptr_t>(views.front()->getViewHandle());
}

extern "C" __declspec(dllexport) bool webview_gui_test_exercise_windows_host_lifecycle(
    std::uintptr_t hostHandle,
    std::size_t passes)
{
    const auto host = reinterpret_cast<HWND>(hostHandle);
    auto& views = retainedViews();
    if (!IsWindow(host) || views.empty() || passes == 0)
        return false;

    for (std::size_t pass = 0; pass < passes; ++pass) {
        for (std::size_t i = 0; i < views.size(); ++i) {
            auto& view = views[i];
            if (!view || !view->loadedOK() || !view->getViewHandle())
                return false;

            const auto child = static_cast<HWND>(view->getViewHandle());
            if (!IsWindow(child))
                return false;

            if (GetParent(child) != host
                && !webview_gui::detail::attachChildWindowToHost(child, host))
                return false;
            if (GetParent(child) != host)
                return false;

            const int width = 480 + static_cast<int>((i + pass) % 17) * 8;
            const int height = 280 + static_cast<int>((i + pass) % 13) * 6;
            if (!webview_gui::detail::resizeChildWindow(child, width, height))
                return false;

            RECT rect{};
            if (!GetClientRect(child, &rect)
                || rect.right - rect.left != width
                || rect.bottom - rect.top != height)
                return false;

            if (!webview_gui::detail::setChildWindowVisible(child, false)
                || IsWindowVisible(child))
                return false;
            if (!webview_gui::detail::setChildWindowVisible(child, true)
                || !IsWindowVisible(child))
                return false;
        }
    }

    return true;
}

extern "C" __declspec(dllexport) void webview_gui_test_release_windows_webviews()
{
    retainedViews().clear();
    apartment().reset();
}
