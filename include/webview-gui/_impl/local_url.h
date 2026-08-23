#pragma once

#include <string>
#include <string_view>

namespace webview_gui::detail {

inline std::string joinLocalPluginURL(std::string_view base, std::string_view path)
{
    if (base.empty()) return {};

    std::string result(base);

    const bool baseHasSlash = !result.empty() && result.back() == '/';
    const bool pathHasSlash = !path.empty() && path.front() == '/';

    if (baseHasSlash && pathHasSlash) {
        result.append(path.substr(1));
    } else if (!baseHasSlash && !pathHasSlash && !path.empty()) {
        result.push_back('/');
        result.append(path);
    } else {
        result.append(path);
    }

    return result;
}

} // namespace webview_gui::detail
