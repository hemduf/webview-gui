#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef WEBVIEW_GUI_MAX_MESSAGE_BYTES
#define WEBVIEW_GUI_MAX_MESSAGE_BYTES (16u * 1024u * 1024u)
#endif

#ifndef WEBVIEW_GUI_MAX_RESOURCE_BYTES
#define WEBVIEW_GUI_MAX_RESOURCE_BYTES (64u * 1024u * 1024u)
#endif

namespace webview_gui::detail {

inline constexpr std::size_t maxMessageBytes = WEBVIEW_GUI_MAX_MESSAGE_BYTES;
inline constexpr std::size_t maxResourceBytes = WEBVIEW_GUI_MAX_RESOURCE_BYTES;
inline constexpr std::string_view pluginLocalOrigin = "choc://choc.choc";

class ThreadAffinity {
public:
    void bindToCurrentThread()
    {
        owner = std::this_thread::get_id();
        bound = true;
    }

    [[nodiscard]] bool isBound() const noexcept { return bound; }

    [[nodiscard]] bool isCurrentThread() const noexcept
    {
        return !bound || owner == std::this_thread::get_id();
    }

private:
    std::thread::id owner{};
    bool bound = false;
};

template <typename T>
class PointerRegistry {
public:
    void set(const void* key, T* value)
    {
        if (key == nullptr)
            return;

        std::unique_lock lock{mutex};
        entries.insert_or_assign(key, value);
    }

    [[nodiscard]] T* find(const void* key) const
    {
        if (key == nullptr)
            return nullptr;

        std::shared_lock lock{mutex};
        const auto it = entries.find(key);
        return it == entries.end() ? nullptr : it->second;
    }

    bool eraseIfMatches(const void* key, T* expected)
    {
        if (key == nullptr)
            return false;

        std::unique_lock lock{mutex};
        const auto it = entries.find(key);
        if (it == entries.end() || it->second != expected)
            return false;

        entries.erase(it);
        return true;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::shared_lock lock{mutex};
        return entries.size();
    }

private:
    mutable std::shared_mutex mutex;
    std::unordered_map<const void*, T*> entries;
};

inline bool messageSizeAllowed(std::size_t bytes) noexcept
{
    return bytes <= maxMessageBytes;
}

inline bool resourceSizeAllowed(std::size_t bytes) noexcept
{
    return bytes <= maxResourceBytes;
}

inline bool base64MessageSizeAllowed(std::size_t encodedChars) noexcept
{
    constexpr auto maxEncoded = ((maxMessageBytes + 2u) / 3u) * 4u;
    return encodedChars <= maxEncoded;
}

inline int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool decodeURLPath(std::string_view encoded, std::string& decoded)
{
    decoded.clear();
    decoded.reserve(encoded.size());

    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const char c = encoded[i];
        if (c == '\0') return false;

        if (c != '%') {
            decoded.push_back(c);
            continue;
        }

        if (i + 2 >= encoded.size()) return false;
        const int hi = hexDigit(encoded[i + 1]);
        const int lo = hexDigit(encoded[i + 2]);
        if (hi < 0 || lo < 0) return false;

        const auto value = static_cast<unsigned char>((hi << 4) | lo);
        if (value == 0) return false;

        decoded.push_back(static_cast<char>(value));
        i += 2;
    }

    return true;
}

inline bool pathIsContained(const std::filesystem::path& root,
                            const std::filesystem::path& candidate)
{
    if (candidate == root) return true;

    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;

    for (const auto& part : relative) {
        if (part == "..") return false;
    }

    return true;
}

inline bool resolveContainedPath(const std::filesystem::path& root,
                                 const std::filesystem::path& requested,
                                 std::filesystem::path& resolved)
{
    const auto normalRoot = root.lexically_normal();
    auto relativeRequest = requested;

    if (relativeRequest.has_root_name())
        return false;
    if (relativeRequest.has_root_directory())
        relativeRequest = relativeRequest.relative_path();

    const auto candidate = (normalRoot / relativeRequest).lexically_normal();
    if (!pathIsContained(normalRoot, candidate))
        return false;

    resolved = candidate;
    return true;
}

inline bool resolveContainedExistingPath(const std::filesystem::path& root,
                                         std::string_view requestedURLPath,
                                         std::filesystem::path& resolved)
{
    std::string decoded;
    if (!decodeURLPath(requestedURLPath, decoded))
        return false;

    std::error_code ec;
    const auto canonicalRoot = std::filesystem::canonical(root, ec);
    if (ec) return false;

    std::filesystem::path lexicalCandidate;
    if (!resolveContainedPath(canonicalRoot, std::filesystem::path(decoded), lexicalCandidate))
        return false;

    const auto canonicalCandidate = std::filesystem::canonical(lexicalCandidate, ec);
    if (ec || !pathIsContained(canonicalRoot, canonicalCandidate))
        return false;

    resolved = canonicalCandidate;
    return true;
}

inline char asciiLower(char c) noexcept
{
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c + ('a' - 'A'));
    return c;
}

inline bool equalsASCIIInsensitive(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (asciiLower(a[i]) != asciiLower(b[i])) return false;
    return true;
}

inline bool startsWithASCIIInsensitive(std::string_view value, std::string_view prefix) noexcept
{
    return value.size() >= prefix.size()
        && equalsASCIIInsensitive(value.substr(0, prefix.size()), prefix);
}

inline bool isTrustedPluginURL(std::string_view url) noexcept
{
    if (equalsASCIIInsensitive(url, "about:blank"))
        return true;

    constexpr std::string_view origin = "choc://choc.choc";
    if (!startsWithASCIIInsensitive(url, origin))
        return false;

    if (url.size() == origin.size()) return true;
    const char next = url[origin.size()];
    return next == '/' || next == '?' || next == '#';
}

inline bool isSafePluginStartPath(std::string_view path) noexcept
{
    if (path.size() >= 2 && path[0] == '/' && path[1] == '/')
        return false;

    const auto colon = path.find(':');
    const auto delimiter = path.find_first_of("/?#");
    return colon == std::string_view::npos
        || (delimiter != std::string_view::npos && colon > delimiter);
}

inline std::size_t findASCIIInsensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) return 0;
    if (needle.size() > haystack.size()) return std::string_view::npos;

    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (equalsASCIIInsensitive(haystack.substr(i, needle.size()), needle))
            return i;
    }
    return std::string_view::npos;
}

inline constexpr std::string_view pluginContentSecurityPolicy =
    "default-src 'self' data: blob:; "
    "script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; "
    "style-src 'self' 'unsafe-inline'; "
    "img-src 'self' data: blob:; "
    "font-src 'self' data:; "
    "media-src 'self' data: blob:; "
    "connect-src 'self'; "
    "worker-src 'self' blob:; "
    "frame-src 'none'; "
    "object-src 'none'; "
    "base-uri 'none'; "
    "form-action 'none'";

inline bool applyPluginContentSecurityPolicy(std::vector<unsigned char>& bytes)
{
    if (!resourceSizeAllowed(bytes.size()))
        return false;

    const std::string html(bytes.begin(), bytes.end());
    const std::string meta =
        "<meta http-equiv=\"Content-Security-Policy\" content=\""
        + std::string(pluginContentSecurityPolicy)
        + "\">";

    if (!resourceSizeAllowed(html.size() + meta.size() + 13u))
        return false;

    std::string hardened = html;
    const auto head = findASCIIInsensitive(hardened, "<head");

    if (head != std::string::npos) {
        const auto end = hardened.find('>', head);
        if (end == std::string::npos) return false;
        hardened.insert(end + 1, meta);
    } else {
        const auto htmlTag = findASCIIInsensitive(hardened, "<html");
        if (htmlTag != std::string::npos) {
            const auto end = hardened.find('>', htmlTag);
            if (end == std::string::npos) return false;
            hardened.insert(end + 1, "<head>" + meta + "</head>");
        } else {
            hardened.insert(0, "<head>" + meta + "</head>");
        }
    }

    bytes.assign(hardened.begin(), hardened.end());
    return resourceSizeAllowed(bytes.size());
}

} // namespace webview_gui::detail
