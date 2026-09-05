#pragma once

#include "preset_browser_model.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webview_gui::examples::presets {

inline constexpr std::size_t kPresetBrowserMaxRequestBytes = 4096u;
inline constexpr std::size_t kPresetBrowserMaxSnapshotBytes = 65535u;
inline constexpr std::size_t kPresetBrowserMaxStringBytes = 1024u;
inline constexpr std::size_t kPresetBrowserMaxEntries = 256u;
inline constexpr std::size_t kPresetBrowserMaxTagsPerEntry = 32u;

enum class PresetBrowserCommand : std::uint8_t {
    Snapshot = 1u,
    Load = 2u,
    Next = 3u,
    Previous = 4u,
    Init = 5u,
    SaveAs = 6u,
    Delete = 7u,
    Refresh = 8u,
};

enum class PresetBrowserProtocolError {
    None,
    InvalidMessage,
    UnsupportedVersion,
    UnsupportedCommand,
    InvalidShape,
    LimitExceeded,
};

struct PresetBrowserRequest {
    PresetBrowserCommand command = PresetBrowserCommand::Snapshot;
    PresetBrowserContentKind kind = PresetBrowserContentKind::None;
    std::string identity;
    std::string name;
    bool overwrite = false;
};

struct PresetBrowserRequestResult {
    PresetBrowserProtocolError error = PresetBrowserProtocolError::None;
    PresetBrowserRequest request{};

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetBrowserProtocolError::None;
    }
};

struct PresetBrowserSnapshotResult {
    PresetBrowserProtocolError error = PresetBrowserProtocolError::None;
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetBrowserProtocolError::None;
    }
};

struct PresetBrowserDecodedSnapshot {
    PresetBrowserProtocolError error = PresetBrowserProtocolError::None;
    PresetBrowserCurrentState current{};
    bool userMutationsAvailable = false;
    std::vector<PresetBrowserEntry> entries;

    [[nodiscard]] bool ok() const noexcept {
        return error == PresetBrowserProtocolError::None;
    }
};

namespace preset_browser_protocol_detail {

inline constexpr std::size_t kRequestHeaderBytes = 12u;
inline constexpr std::size_t kSnapshotHeaderBytes = 12u;
inline constexpr std::size_t kEntryHeaderBytes = 6u;

[[nodiscard]] inline std::uint16_t loadU16Le(const std::uint8_t *bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
}

inline void appendU16Le(std::vector<std::uint8_t> &bytes, std::size_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

[[nodiscard]] inline bool boundedString(std::string_view text) noexcept {
    return text.size() <= kPresetBrowserMaxStringBytes &&
           text.size() <= std::numeric_limits<std::uint16_t>::max();
}

inline void appendString(std::vector<std::uint8_t> &bytes, std::string_view text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
}

[[nodiscard]] inline bool validKindValue(std::uint8_t raw) noexcept {
    return raw <= static_cast<std::uint8_t>(PresetBrowserContentKind::User);
}

[[nodiscard]] inline bool validRequestShape(const PresetBrowserRequest &request) noexcept {
    const auto noPayload = request.kind == PresetBrowserContentKind::None &&
                           request.identity.empty() && request.name.empty() &&
                           !request.overwrite;
    switch (request.command) {
        case PresetBrowserCommand::Snapshot:
        case PresetBrowserCommand::Next:
        case PresetBrowserCommand::Previous:
        case PresetBrowserCommand::Init:
        case PresetBrowserCommand::Refresh:
            return noPayload;
        case PresetBrowserCommand::Load:
            return (request.kind == PresetBrowserContentKind::Factory ||
                    request.kind == PresetBrowserContentKind::User) &&
                   !request.identity.empty() && request.name.empty() &&
                   !request.overwrite;
        case PresetBrowserCommand::SaveAs:
            return request.kind == PresetBrowserContentKind::User &&
                   !request.identity.empty() && !request.name.empty();
        case PresetBrowserCommand::Delete:
            return request.kind == PresetBrowserContentKind::User &&
                   !request.identity.empty() && request.name.empty() &&
                   !request.overwrite;
    }
    return false;
}

[[nodiscard]] inline bool validCommandValue(std::uint8_t raw) noexcept {
    return raw >= static_cast<std::uint8_t>(PresetBrowserCommand::Snapshot) &&
           raw <= static_cast<std::uint8_t>(PresetBrowserCommand::Refresh);
}

[[nodiscard]] inline bool appendEntry(std::vector<std::uint8_t> &bytes,
                                      const PresetBrowserEntry &entry) {
    if ((entry.kind != PresetBrowserContentKind::Factory &&
         entry.kind != PresetBrowserContentKind::User) ||
        entry.identity.empty() || entry.name.empty() ||
        !boundedString(entry.identity) || !boundedString(entry.name) ||
        entry.tags.size() > kPresetBrowserMaxTagsPerEntry)
        return false;

    bytes.push_back(static_cast<std::uint8_t>(entry.kind));
    bytes.push_back(static_cast<std::uint8_t>(entry.tags.size()));
    appendU16Le(bytes, entry.identity.size());
    appendU16Le(bytes, entry.name.size());
    appendString(bytes, entry.identity);
    appendString(bytes, entry.name);
    for (const auto &tag : entry.tags) {
        if (!boundedString(tag))
            return false;
        appendU16Le(bytes, tag.size());
        appendString(bytes, tag);
    }
    return bytes.size() <= kPresetBrowserMaxSnapshotBytes;
}

[[nodiscard]] inline bool readString(const std::uint8_t *bytes,
                                     std::size_t size,
                                     std::size_t &offset,
                                     std::size_t length,
                                     std::string &output) {
    if (length > kPresetBrowserMaxStringBytes || offset > size ||
        length > size - offset)
        return false;
    output.assign(reinterpret_cast<const char *>(bytes + offset), length);
    offset += length;
    return true;
}

} // namespace preset_browser_protocol_detail

[[nodiscard]] inline PresetBrowserRequestResult decodePresetBrowserRequest(
    const void *buffer,
    std::size_t size) {
    using namespace preset_browser_protocol_detail;
    PresetBrowserRequestResult result;
    if (!buffer || size < kRequestHeaderBytes || size > kPresetBrowserMaxRequestBytes) {
        result.error = size > kPresetBrowserMaxRequestBytes
                           ? PresetBrowserProtocolError::LimitExceeded
                           : PresetBrowserProtocolError::InvalidMessage;
        return result;
    }

    const auto *bytes = static_cast<const std::uint8_t *>(buffer);
    if (bytes[0] != 'W' || bytes[1] != 'V' || bytes[2] != 'P') {
        result.error = PresetBrowserProtocolError::InvalidMessage;
        return result;
    }
    if (bytes[3] != '2') {
        result.error = PresetBrowserProtocolError::UnsupportedVersion;
        return result;
    }
    if (!validCommandValue(bytes[4])) {
        result.error = PresetBrowserProtocolError::UnsupportedCommand;
        return result;
    }
    if (!validKindValue(bytes[5]) || (bytes[6] & 0xfeu) != 0u || bytes[7] != 0u) {
        result.error = PresetBrowserProtocolError::InvalidShape;
        return result;
    }

    const auto identityLength = static_cast<std::size_t>(loadU16Le(bytes + 8u));
    const auto nameLength = static_cast<std::size_t>(loadU16Le(bytes + 10u));
    if (identityLength > kPresetBrowserMaxStringBytes ||
        nameLength > kPresetBrowserMaxStringBytes ||
        identityLength > size - kRequestHeaderBytes ||
        nameLength > size - kRequestHeaderBytes - identityLength ||
        kRequestHeaderBytes + identityLength + nameLength != size) {
        result.error = PresetBrowserProtocolError::InvalidMessage;
        return result;
    }

    result.request.command = static_cast<PresetBrowserCommand>(bytes[4]);
    result.request.kind = static_cast<PresetBrowserContentKind>(bytes[5]);
    result.request.overwrite = (bytes[6] & 0x01u) != 0u;
    result.request.identity.assign(
        reinterpret_cast<const char *>(bytes + kRequestHeaderBytes), identityLength);
    result.request.name.assign(
        reinterpret_cast<const char *>(bytes + kRequestHeaderBytes + identityLength),
        nameLength);

    if (!validRequestShape(result.request))
        result.error = PresetBrowserProtocolError::InvalidShape;
    return result;
}

// Used by deterministic host/protocol tests and by browser clients that need to
// synthesize the exact request wire format without depending on JSON.
[[nodiscard]] inline std::vector<std::uint8_t> encodePresetBrowserRequestForTest(
    PresetBrowserCommand command,
    PresetBrowserContentKind kind = PresetBrowserContentKind::None,
    std::string_view identity = {},
    std::string_view name = {},
    bool overwrite = false) {
    using namespace preset_browser_protocol_detail;
    std::vector<std::uint8_t> bytes;
    if (!boundedString(identity) || !boundedString(name))
        return bytes;
    bytes.reserve(kRequestHeaderBytes + identity.size() + name.size());
    bytes.insert(bytes.end(), {'W', 'V', 'P', '2'});
    bytes.push_back(static_cast<std::uint8_t>(command));
    bytes.push_back(static_cast<std::uint8_t>(kind));
    bytes.push_back(overwrite ? 0x01u : 0u);
    bytes.push_back(0u);
    appendU16Le(bytes, identity.size());
    appendU16Le(bytes, name.size());
    appendString(bytes, identity);
    appendString(bytes, name);
    return bytes;
}

[[nodiscard]] inline PresetBrowserSnapshotResult encodePresetBrowserSnapshot(
    const PresetBrowserModel &model,
    bool userMutationsAvailable) {
    using namespace preset_browser_protocol_detail;
    PresetBrowserSnapshotResult result;
    const auto &current = model.current();
    const auto &entries = model.entries();
    if (entries.size() > kPresetBrowserMaxEntries ||
        entries.size() > std::numeric_limits<std::uint16_t>::max() ||
        !boundedString(current.identity) || !boundedString(current.name)) {
        result.error = PresetBrowserProtocolError::LimitExceeded;
        return result;
    }

    try {
        auto &bytes = result.bytes;
        bytes.reserve(kSnapshotHeaderBytes + current.identity.size() +
                      current.name.size() + entries.size() * kEntryHeaderBytes);
        bytes.insert(bytes.end(), {'W', 'V', 'B', '2'});
        bytes.push_back(static_cast<std::uint8_t>(current.kind));
        std::uint8_t flags = 0u;
        if (current.dirty)
            flags |= 0x01u;
        if (userMutationsAvailable)
            flags |= 0x02u;
        bytes.push_back(flags);
        appendU16Le(bytes, entries.size());
        appendU16Le(bytes, current.identity.size());
        appendU16Le(bytes, current.name.size());
        appendString(bytes, current.identity);
        appendString(bytes, current.name);
        for (const auto &entry : entries) {
            if (!appendEntry(bytes, entry)) {
                result.bytes.clear();
                result.error = PresetBrowserProtocolError::LimitExceeded;
                return result;
            }
        }
        if (bytes.size() > kPresetBrowserMaxSnapshotBytes) {
            bytes.clear();
            result.error = PresetBrowserProtocolError::LimitExceeded;
        }
    } catch (...) {
        result.bytes.clear();
        result.error = PresetBrowserProtocolError::LimitExceeded;
    }
    return result;
}

// Native decoder used by deterministic tests. Keeping a complete inverse codec
// here makes framing changes fail in C++ before the same format is consumed by
// the JavaScript/TypeScript bridge.
[[nodiscard]] inline PresetBrowserDecodedSnapshot decodePresetBrowserSnapshotForTest(
    const void *buffer,
    std::size_t size) {
    using namespace preset_browser_protocol_detail;
    PresetBrowserDecodedSnapshot result;
    if (!buffer || size < kSnapshotHeaderBytes || size > kPresetBrowserMaxSnapshotBytes) {
        result.error = PresetBrowserProtocolError::InvalidMessage;
        return result;
    }
    const auto *bytes = static_cast<const std::uint8_t *>(buffer);
    if (bytes[0] != 'W' || bytes[1] != 'V' || bytes[2] != 'B') {
        result.error = PresetBrowserProtocolError::InvalidMessage;
        return result;
    }
    if (bytes[3] != '2') {
        result.error = PresetBrowserProtocolError::UnsupportedVersion;
        return result;
    }
    if (!validKindValue(bytes[4]) || (bytes[5] & 0xfcu) != 0u) {
        result.error = PresetBrowserProtocolError::InvalidShape;
        return result;
    }

    const auto entryCount = static_cast<std::size_t>(loadU16Le(bytes + 6u));
    const auto currentIdentityLength = static_cast<std::size_t>(loadU16Le(bytes + 8u));
    const auto currentNameLength = static_cast<std::size_t>(loadU16Le(bytes + 10u));
    if (entryCount > kPresetBrowserMaxEntries) {
        result.error = PresetBrowserProtocolError::LimitExceeded;
        return result;
    }

    result.current.kind = static_cast<PresetBrowserContentKind>(bytes[4]);
    result.current.dirty = (bytes[5] & 0x01u) != 0u;
    result.userMutationsAvailable = (bytes[5] & 0x02u) != 0u;
    std::size_t offset = kSnapshotHeaderBytes;
    if (!readString(bytes, size, offset, currentIdentityLength, result.current.identity) ||
        !readString(bytes, size, offset, currentNameLength, result.current.name)) {
        result.error = PresetBrowserProtocolError::InvalidMessage;
        return result;
    }

    try {
        result.entries.reserve(entryCount);
        for (std::size_t entryIndex = 0u; entryIndex < entryCount; ++entryIndex) {
            if (offset > size || kEntryHeaderBytes > size - offset) {
                result.error = PresetBrowserProtocolError::InvalidMessage;
                return result;
            }
            const auto kindRaw = bytes[offset];
            const auto tagCount = static_cast<std::size_t>(bytes[offset + 1u]);
            const auto identityLength = static_cast<std::size_t>(loadU16Le(bytes + offset + 2u));
            const auto nameLength = static_cast<std::size_t>(loadU16Le(bytes + offset + 4u));
            offset += kEntryHeaderBytes;
            if (!validKindValue(kindRaw) ||
                (kindRaw != static_cast<std::uint8_t>(PresetBrowserContentKind::Factory) &&
                 kindRaw != static_cast<std::uint8_t>(PresetBrowserContentKind::User)) ||
                tagCount > kPresetBrowserMaxTagsPerEntry) {
                result.error = PresetBrowserProtocolError::InvalidShape;
                return result;
            }

            PresetBrowserEntry entry;
            entry.kind = static_cast<PresetBrowserContentKind>(kindRaw);
            if (!readString(bytes, size, offset, identityLength, entry.identity) ||
                !readString(bytes, size, offset, nameLength, entry.name) ||
                entry.identity.empty() || entry.name.empty()) {
                result.error = PresetBrowserProtocolError::InvalidMessage;
                return result;
            }
            entry.tags.reserve(tagCount);
            for (std::size_t tagIndex = 0u; tagIndex < tagCount; ++tagIndex) {
                if (offset > size || 2u > size - offset) {
                    result.error = PresetBrowserProtocolError::InvalidMessage;
                    return result;
                }
                const auto tagLength = static_cast<std::size_t>(loadU16Le(bytes + offset));
                offset += 2u;
                std::string tag;
                if (!readString(bytes, size, offset, tagLength, tag)) {
                    result.error = PresetBrowserProtocolError::InvalidMessage;
                    return result;
                }
                entry.tags.push_back(std::move(tag));
            }
            result.entries.push_back(std::move(entry));
        }
    } catch (...) {
        result.entries.clear();
        result.error = PresetBrowserProtocolError::LimitExceeded;
        return result;
    }

    if (offset != size)
        result.error = PresetBrowserProtocolError::InvalidMessage;
    return result;
}

} // namespace webview_gui::examples::presets
