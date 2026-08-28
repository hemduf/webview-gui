#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <Security/SecRandom.h>
#elif defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#endif

namespace webview_gui::detail {

inline bool fillSecureRandom(void* destination, std::size_t size) noexcept
{
    if (size == 0) return true;
    if (destination == nullptr) return false;

#if defined(_WIN32)
    if (size > static_cast<std::size_t>(ULONG_MAX)) return false;
    return BCryptGenRandom(nullptr,
                          static_cast<PUCHAR>(destination),
                          static_cast<ULONG>(size),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    return SecRandomCopyBytes(kSecRandomDefault, size, destination) == errSecSuccess;
#elif defined(__linux__)
    auto* bytes = static_cast<unsigned char*>(destination);
    std::size_t offset = 0;

    while (offset < size) {
        const auto result = getrandom(bytes + offset, size - offset, 0);
        if (result > 0) {
            offset += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
#else
    (void)destination;
    (void)size;
    return false;
#endif
}

inline std::string makeSecureBridgeToken()
{
    unsigned char bytes[32] = {};
    if (!fillSecureRandom(bytes, sizeof(bytes)))
        return {};

    static constexpr char hex[] = "0123456789abcdef";
    std::string token(64, '0');
    for (std::size_t i = 0; i < sizeof(bytes); ++i) {
        token[i * 2] = hex[bytes[i] >> 4];
        token[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    return token;
}

} // namespace webview_gui::detail
