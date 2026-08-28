#pragma once

#include "plugin_support.h"

#include <string_view>

namespace webview_gui::detail {

inline bool trustedURLForExactOrigin(std::string_view url, std::string_view origin) noexcept
{
    if (equalsASCIIInsensitive(url, "about:blank"))
        return true;

    if (!startsWithASCIIInsensitive(url, origin))
        return false;

    if (url.size() == origin.size())
        return true;

    const char next = url[origin.size()];
    return next == '/' || next == '?' || next == '#';
}

inline bool isTrustedAppleLinuxPluginURL(std::string_view url) noexcept
{
    return trustedURLForExactOrigin(url, pluginLocalOrigin);
}

inline bool isTrustedWindowsPluginURL(std::string_view url) noexcept
{
    return trustedURLForExactOrigin(url, windowsPluginLocalOrigin);
}

} // namespace webview_gui::detail
