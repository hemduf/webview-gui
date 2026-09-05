#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webview_gui::examples::presets {

// Main/UI-thread-only state for the shared Gain/PolySynth preset browser.
//
// This deliberately contains no storage, WebView, host or processor access. It
// models only durable browser identity/dirty/navigation state so transient
// sample-accurate modulation and voice telemetry never become preset state.
enum class PresetBrowserContentKind {
    None,
    Init,
    Factory,
    User,
};

struct PresetBrowserEntry {
    PresetBrowserContentKind kind = PresetBrowserContentKind::None;
    std::string identity;
    std::string name;
    std::vector<std::string> tags;
};

struct PresetBrowserCurrentState {
    PresetBrowserContentKind kind = PresetBrowserContentKind::None;
    std::string identity;
    std::string name;
    bool dirty = false;
};

class PresetBrowserModel {
public:
    [[nodiscard]] bool replaceEntries(std::vector<PresetBrowserEntry> factoryEntries,
                                      std::vector<PresetBrowserEntry> userEntries) {
        if (!validEntries(factoryEntries, PresetBrowserContentKind::Factory) ||
            !validEntries(userEntries, PresetBrowserContentKind::User) ||
            containsDuplicateIdentity(factoryEntries) ||
            containsDuplicateIdentity(userEntries))
            return false;

        std::vector<PresetBrowserEntry> candidate;
        candidate.reserve(factoryEntries.size() + userEntries.size());
        for (auto &entry : factoryEntries)
            candidate.push_back(std::move(entry));
        for (auto &entry : userEntries)
            candidate.push_back(std::move(entry));

        entries_ = std::move(candidate);
        factoryCount_ = factoryEntries.size();
        userCount_ = userEntries.size();

        // A refresh may remove/rename a user preset. Never keep a current
        // identity that the new snapshot can no longer resolve.
        if ((current_.kind == PresetBrowserContentKind::Factory ||
             current_.kind == PresetBrowserContentKind::User) &&
            !find(current_.kind, current_.identity)) {
            current_ = {};
        } else if (const auto *entry = find(current_.kind, current_.identity)) {
            current_.name = entry->name;
        }
        return true;
    }

    [[nodiscard]] const std::vector<PresetBrowserEntry> &entries() const noexcept {
        return entries_;
    }

    [[nodiscard]] std::size_t factoryCount() const noexcept { return factoryCount_; }
    [[nodiscard]] std::size_t userCount() const noexcept { return userCount_; }

    [[nodiscard]] const PresetBrowserCurrentState &current() const noexcept {
        return current_;
    }

    [[nodiscard]] const PresetBrowserEntry *find(PresetBrowserContentKind kind,
                                                 std::string_view identity) const noexcept {
        if (kind != PresetBrowserContentKind::Factory &&
            kind != PresetBrowserContentKind::User)
            return nullptr;
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [kind, identity](const PresetBrowserEntry &entry) {
                                         return entry.kind == kind && entry.identity == identity;
                                     });
        return it == entries_.end() ? nullptr : &*it;
    }

    [[nodiscard]] bool markLoaded(PresetBrowserContentKind kind,
                                  std::string_view identity) {
        const auto *entry = find(kind, identity);
        if (!entry)
            return false;
        current_.kind = entry->kind;
        current_.identity = entry->identity;
        current_.name = entry->name;
        current_.dirty = false;
        return true;
    }

    void markInitLoaded(std::string_view displayName = "Init") {
        current_.kind = PresetBrowserContentKind::Init;
        current_.identity.clear();
        current_.name.assign(displayName.data(), displayName.size());
        current_.dirty = false;
    }

    // Loading opaque project/host state must not retain a preset identity just
    // because the resulting parameter values happen to resemble a preset.
    void clearIdentityAfterStateRestore() noexcept { current_ = {}; }

    // Call only for persistent/base parameter or persistent setting changes.
    // PARAM_MOD, note expression and voice activity must use
    // markTransientChange() (which intentionally does nothing).
    void markPersistentEdit() noexcept { current_.dirty = true; }
    void markTransientChange() const noexcept {}

    [[nodiscard]] const PresetBrowserEntry *next() const noexcept {
        if (entries_.empty())
            return nullptr;
        const auto index = currentEntryIndex();
        if (index == npos)
            return &entries_.front();
        return &entries_[(index + 1u) % entries_.size()];
    }

    [[nodiscard]] const PresetBrowserEntry *previous() const noexcept {
        if (entries_.empty())
            return nullptr;
        const auto index = currentEntryIndex();
        if (index == npos || index == 0u)
            return &entries_.back();
        return &entries_[index - 1u];
    }

    [[nodiscard]] std::vector<const PresetBrowserEntry *> filtered(
        std::string_view query,
        std::string_view requiredTag) const {
        std::vector<const PresetBrowserEntry *> result;
        result.reserve(entries_.size());
        for (const auto &entry : entries_) {
            const bool queryMatches = query.empty() ||
                                      containsCaseInsensitive(entry.name, query) ||
                                      containsCaseInsensitive(entry.identity, query) ||
                                      anyTagContains(entry, query);
            const bool tagMatches = requiredTag.empty() || hasTag(entry, requiredTag);
            if (queryMatches && tagMatches)
                result.push_back(&entry);
        }
        return result;
    }

private:
    inline static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    [[nodiscard]] static bool validEntries(const std::vector<PresetBrowserEntry> &entries,
                                           PresetBrowserContentKind expectedKind) noexcept {
        return std::all_of(entries.begin(), entries.end(),
                           [expectedKind](const PresetBrowserEntry &entry) {
                               return entry.kind == expectedKind &&
                                      !entry.identity.empty() && !entry.name.empty();
                           });
    }

    [[nodiscard]] static bool containsDuplicateIdentity(
        const std::vector<PresetBrowserEntry> &entries) noexcept {
        for (std::size_t i = 0u; i < entries.size(); ++i) {
            for (std::size_t j = 0u; j < i; ++j) {
                if (entries[i].identity == entries[j].identity)
                    return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t currentEntryIndex() const noexcept {
        if (current_.kind != PresetBrowserContentKind::Factory &&
            current_.kind != PresetBrowserContentKind::User)
            return npos;
        for (std::size_t i = 0u; i < entries_.size(); ++i) {
            if (entries_[i].kind == current_.kind &&
                entries_[i].identity == current_.identity)
                return i;
        }
        return npos;
    }

    [[nodiscard]] static char foldAscii(char value) noexcept {
        const auto byte = static_cast<unsigned char>(value);
        return static_cast<char>(std::tolower(byte));
    }

    [[nodiscard]] static bool containsCaseInsensitive(std::string_view haystack,
                                                      std::string_view needle) noexcept {
        if (needle.empty())
            return true;
        if (needle.size() > haystack.size())
            return false;
        for (std::size_t start = 0u; start + needle.size() <= haystack.size(); ++start) {
            bool match = true;
            for (std::size_t i = 0u; i < needle.size(); ++i) {
                if (foldAscii(haystack[start + i]) != foldAscii(needle[i])) {
                    match = false;
                    break;
                }
            }
            if (match)
                return true;
        }
        return false;
    }

    [[nodiscard]] static bool hasTag(const PresetBrowserEntry &entry,
                                     std::string_view requiredTag) noexcept {
        return std::any_of(entry.tags.begin(), entry.tags.end(),
                           [requiredTag](const std::string &tag) {
                               return tag.size() == requiredTag.size() &&
                                      containsCaseInsensitive(tag, requiredTag);
                           });
    }

    [[nodiscard]] static bool anyTagContains(const PresetBrowserEntry &entry,
                                             std::string_view query) noexcept {
        return std::any_of(entry.tags.begin(), entry.tags.end(),
                           [query](const std::string &tag) {
                               return containsCaseInsensitive(tag, query);
                           });
    }

    std::vector<PresetBrowserEntry> entries_;
    std::size_t factoryCount_ = 0u;
    std::size_t userCount_ = 0u;
    PresetBrowserCurrentState current_{};
};

} // namespace webview_gui::examples::presets
