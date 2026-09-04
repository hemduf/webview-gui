#pragma once

#include <utility>

namespace webview_gui::examples::presets::detail {

template <typename Fn, typename Fallback>
[[nodiscard]] auto exceptionBoundary(Fn &&fn, Fallback &&fallback) noexcept
    -> decltype(std::forward<Fn>(fn)()) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
        return std::forward<Fn>(fn)();
    } catch (...) {
        return std::forward<Fallback>(fallback)();
    }
#else
    (void)fallback;
    return std::forward<Fn>(fn)();
#endif
}

} // namespace webview_gui::examples::presets::detail
