#pragma once

#include <cstddef>
#include <string_view>

namespace webview_gui::detail {

inline constexpr std::string_view appleLinuxPluginResourceAllowOrigin = "choc://choc.choc";
inline constexpr std::string_view windowsPluginResourceAllowOrigin = "https://choc.localhost";
inline constexpr std::size_t maxResourceMediaTypeBytes = 1024u;

// MIME values are eventually copied into native response headers. Keep that
// boundary intentionally narrower than arbitrary user strings so a custom
// ResourceGetter cannot inject CR/LF-delimited headers into WebView2 (or feed
// control characters to another backend's HTTP response API).
inline constexpr bool resourceMediaTypeAllowed(std::string_view mediaType) noexcept
{
    if (mediaType.empty() || mediaType.size() > maxResourceMediaTypeBytes)
        return false;

    for (const unsigned char c : mediaType) {
        if (c < 0x20u || c >= 0x7fu)
            return false;
    }

    return true;
}

static_assert(appleLinuxPluginResourceAllowOrigin != "*");
static_assert(windowsPluginResourceAllowOrigin != "*");
static_assert(resourceMediaTypeAllowed("text/html; charset=utf-8"));
static_assert(resourceMediaTypeAllowed("application/wasm"));
static_assert(!resourceMediaTypeAllowed("text/html\r\nX-Injected: 1"));
static_assert(!resourceMediaTypeAllowed("text/html\nX-Injected: 1"));

} // namespace webview_gui::detail
