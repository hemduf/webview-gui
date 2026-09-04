#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <mutex>
#include <random>
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
inline constexpr std::string_view windowsPluginLocalOrigin = "https://choc.localhost";
inline constexpr std::string_view bridgeFragmentKey = "__wg";

inline bool tryConvertNativeHostDimension(double value, int& native) noexcept
{
    if (!std::isfinite(value) || value < 0.0
        || value > static_cast<double>(std::numeric_limits<int>::max()))
        return false;

    native = static_cast<int>(value);
    return true;
}

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

inline bool isBridgeToken(std::string_view token) noexcept
{
    if (token.size() != 64) return false;
    for (const char c : token)
        if (hexDigit(c) < 0) return false;
    return true;
}

inline std::string makeBridgeToken()
{
    // Mainstream libc++/libstdc++/MSVC implementations back random_device with
    // an OS CSPRNG. Draw a full byte independently for a 256-bit capability.
    static constexpr char hex[] = "0123456789abcdef";
    std::random_device random;
    std::string token;
    token.resize(64);

    for (std::size_t i = 0; i < 32; ++i) {
        const auto byte = static_cast<unsigned char>(random() & 0xffu);
        token[i * 2] = hex[byte >> 4];
        token[i * 2 + 1] = hex[byte & 0x0f];
    }

    return token;
}

inline bool constantTimeTokenEquals(std::string_view expected, std::string_view actual) noexcept
{
    if (expected.size() != actual.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
        difference |= static_cast<unsigned char>(expected[i] ^ actual[i]);
    return difference == 0;
}

inline std::string appendBridgeTokenToURL(std::string url, std::string_view token)
{
    if (!isBridgeToken(token)) return {};
    url += url.find('#') == std::string::npos ? "#" : "&";
    url += bridgeFragmentKey;
    url += "=";
    url += token;
    return url;
}

inline std::string bridgeSendFunctionName(std::string_view token)
{
    if (!isBridgeToken(token)) return {};
    return "_WebviewGui_send_" + std::string(token);
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

    const auto trustedOrigin = [&](std::string_view origin) {
        if (!startsWithASCIIInsensitive(url, origin)) return false;
        if (url.size() == origin.size()) return true;
        const char next = url[origin.size()];
        return next == '/' || next == '?' || next == '#';
    };

    return trustedOrigin(pluginLocalOrigin) || trustedOrigin(windowsPluginLocalOrigin);
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

inline std::string bridgeBootstrapInitScript()
{
    return std::string(R"((()=>{
const storageKey='__webview_gui_capability';
let token='';
const match=location.hash.match(/(?:^#|&)__wg=([0-9a-f]{64})(?:&|$)/);
try {
  if(match){token=match[1];sessionStorage.setItem(storageKey,token);}
  else token=sessionStorage.getItem(storageKey)||'';
} catch(_) {}
if(!/^[0-9a-f]{64}$/.test(token)) return;
const maxBytes=)") + std::to_string(maxMessageBytes) + R"(;
if(!Uint8Array.prototype.toBase64){Uint8Array.prototype.toBase64=function(){let s='';for(let i=0;i<this.length;++i)s+=String.fromCharCode(this[i]);return btoa(s);};}
if(!Uint8Array.fromBase64){Uint8Array.fromBase64=b64=>{const s=atob(b64);const a=new Uint8Array(s.length);for(let i=0;i<s.length;++i)a[i]=s.charCodeAt(i);return a;};}
const bridgeBytes=d=>d instanceof ArrayBuffer?new Uint8Array(d):d instanceof Uint8Array?d:null;
const forwardToNative=d=>{const b=bridgeBytes(d);if(!b||b.byteLength>maxBytes)return false;window._WebviewGui_receive64(token,b.toBase64());return true;};
const nativePostMessage=window.postMessage.bind(window);
window.postMessage=function(data,targetOrigin,transfer){if(forwardToNative(data))return;if(arguments.length>2)return nativePostMessage(data,targetOrigin,transfer);return nativePostMessage(data,targetOrigin);};
window.addEventListener('message',e=>{if(e.source!==window)return;if(!forwardToNative(e.data))return;e.stopImmediatePropagation();},{capture:true});
window['_WebviewGui_send_'+token]=b64=>{const d=Uint8Array.fromBase64(b64);if(d.byteLength<=maxBytes)window.dispatchEvent(new MessageEvent('message',{data:d.buffer,source:null}));};
if(typeof window._WebviewGui_ready==='function')window._WebviewGui_ready(token);
})();)";
}

inline bool isHTMLWhitespace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

inline bool isASCIIAlpha(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool isHTMLTagNameDelimiter(char c) noexcept
{
    return isHTMLWhitespace(c) || c == '/' || c == '>';
}

inline bool matchesHTMLTagNameAt(std::string_view html,
                                 std::size_t nameStart,
                                 std::string_view name) noexcept
{
    const auto nameEnd = nameStart + name.size();
    return nameEnd < html.size()
        && equalsASCIIInsensitive(html.substr(nameStart, name.size()), name)
        && isHTMLTagNameDelimiter(html[nameEnd]);
}

inline std::size_t findHTMLTagEnd(std::string_view html, std::size_t tagStart) noexcept
{
    char quote = 0;
    for (std::size_t i = tagStart + 1; i < html.size(); ++i) {
        const char c = html[i];
        if (quote != 0) {
            if (c == quote)
                quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            continue;
        }
        if (c == '>')
            return i;
    }
    return std::string_view::npos;
}

inline std::size_t firstNonHTMLWhitespace(std::string_view html,
                                          std::size_t begin,
                                          std::size_t end) noexcept
{
    for (std::size_t i = begin; i < end; ++i)
        if (!isHTMLWhitespace(html[i]))
            return i;
    return std::string_view::npos;
}

inline bool findHTMLCommentEnd(std::string_view html,
                               std::size_t contentStart,
                               std::size_t& afterComment) noexcept
{
    const auto normal = html.find("-->", contentStart);
    const auto bang = html.find("--!>", contentStart);
    const auto end = normal == std::string_view::npos ? bang
        : bang == std::string_view::npos ? normal
        : std::min(normal, bang);
    if (end == std::string_view::npos)
        return false;

    afterComment = end + (end == bang ? 4u : 3u);
    return true;
}

inline bool findHTMLHardeningInsertionPoint(std::string_view html,
                                            std::size_t& insertion,
                                            bool& createHead) noexcept
{
    const bool hasUTF8BOM = html.size() >= 3
        && static_cast<unsigned char>(html[0]) == 0xEFu
        && static_cast<unsigned char>(html[1]) == 0xBBu
        && static_cast<unsigned char>(html[2]) == 0xBFu;
    std::size_t cursor = hasUTF8BOM ? 3u : 0u;

    // Only emulate the browser states which can exist before the document head
    // has been created. Once any other token is seen, HTML creates an implicit
    // head and reprocesses that token. Inject immediately before that token, so
    // later text such as <head> inside script/template/foreign/body content can
    // never redirect the CSP policy into an inert element.
    while (cursor < html.size()) {
        const auto relative = html.substr(cursor).find('<');
        const auto tagStart = relative == std::string_view::npos
            ? html.size() : cursor + relative;

        if (const auto text = firstNonHTMLWhitespace(html, cursor, tagStart);
            text != std::string_view::npos) {
            insertion = text;
            createHead = true;
            return true;
        }

        if (tagStart == html.size())
            break;

        const auto remaining = html.substr(tagStart);

        if (remaining.size() >= 4 && remaining.substr(0, 4) == "<!--") {
            std::size_t afterComment = 0;
            if (!findHTMLCommentEnd(html, tagStart + 4, afterComment))
                return false;
            cursor = afterComment;
            continue;
        }

        if (startsWithASCIIInsensitive(remaining, "<!doctype")
            && remaining.size() > 9
            && isHTMLTagNameDelimiter(remaining[9])) {
            const auto tagEnd = findHTMLTagEnd(html, tagStart);
            if (tagEnd == std::string_view::npos)
                return false;
            cursor = tagEnd + 1;
            continue;
        }

        // HTML tokenizes processing instructions as bogus comments. They are
        // harmless before the head, but malformed/unclosed forms fail closed.
        if (remaining.size() >= 2 && remaining.substr(0, 2) == "<?") {
            const auto end = html.find('>', tagStart + 2);
            if (end == std::string_view::npos)
                return false;
            cursor = end + 1;
            continue;
        }

        // Unknown declarations have complicated bogus-comment rules. Refuse to
        // guess their browser context rather than risking a misplaced policy.
        if (remaining.size() >= 2 && remaining.substr(0, 2) == "<!")
            return false;

        if (remaining.size() >= 2 && remaining.substr(0, 2) == "</") {
            const auto nameStart = tagStart + 2;
            if (matchesHTMLTagNameAt(html, nameStart, "head")
                || matchesHTMLTagNameAt(html, nameStart, "body")
                || matchesHTMLTagNameAt(html, nameStart, "html")
                || matchesHTMLTagNameAt(html, nameStart, "br")) {
                insertion = tagStart;
                createHead = true;
                return true;
            }

            // Other end tags are ignored by both the before-html and before-head
            // insertion modes. Skip the complete token and keep looking.
            const auto tagEnd = findHTMLTagEnd(html, tagStart);
            if (tagEnd == std::string_view::npos)
                return false;
            cursor = tagEnd + 1;
            continue;
        }

        if (remaining.size() >= 2 && remaining[0] == '<'
            && isASCIIAlpha(remaining[1])) {
            const auto nameStart = tagStart + 1;
            if (matchesHTMLTagNameAt(html, nameStart, "html")) {
                const auto tagEnd = findHTMLTagEnd(html, tagStart);
                if (tagEnd == std::string_view::npos)
                    return false;
                cursor = tagEnd + 1;
                continue;
            }

            if (matchesHTMLTagNameAt(html, nameStart, "head")) {
                const auto tagEnd = findHTMLTagEnd(html, tagStart);
                if (tagEnd == std::string_view::npos)
                    return false;
                insertion = tagEnd + 1;
                createHead = false;
                return true;
            }

            insertion = tagStart;
            createHead = true;
            return true;
        }

        // A stray '<' or malformed tag opener is emitted as character data by
        // the tokenizer. Non-whitespace character data implies the head.
        insertion = tagStart;
        createHead = true;
        return true;
    }

    insertion = html.size();
    createHead = true;
    return true;
}

inline bool injectIntoHTMLHead(std::string& html, std::string_view content)
{
    std::size_t insertion = 0;
    bool createHead = false;
    if (!findHTMLHardeningInsertionPoint(html, insertion, createHead))
        return false;

    if (createHead)
        html.insert(insertion, "<head>" + std::string(content) + "</head>");
    else
        html.insert(insertion, content);
    return true;
}

inline bool applyPluginContentSecurityPolicy(std::vector<unsigned char>& bytes)
{
    if (!resourceSizeAllowed(bytes.size()))
        return false;

    std::string html(bytes.begin(), bytes.end());
    const std::string meta =
        "<meta http-equiv=\"Content-Security-Policy\" content=\""
        + std::string(pluginContentSecurityPolicy)
        + "\">";

    if (!resourceSizeAllowed(html.size() + meta.size() + 13u)
        || !injectIntoHTMLHead(html, meta))
        return false;

    bytes.assign(html.begin(), html.end());
    return resourceSizeAllowed(bytes.size());
}

inline bool applyPluginHTMLHardening(std::vector<unsigned char>& bytes)
{
    return applyPluginContentSecurityPolicy(bytes);
}

} // namespace webview_gui::detail
