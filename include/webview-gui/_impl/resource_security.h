#pragma once

#include <string_view>

namespace webview_gui::detail {

inline constexpr std::string_view appleLinuxPluginResourceAllowOrigin = "choc://choc.choc";
inline constexpr std::string_view windowsPluginResourceAllowOrigin = "https://choc.localhost";

static_assert(appleLinuxPluginResourceAllowOrigin != "*");
static_assert(windowsPluginResourceAllowOrigin != "*");

} // namespace webview_gui::detail
