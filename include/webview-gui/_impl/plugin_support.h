#pragma once

#include <filesystem>
#include <shared_mutex>
#include <unordered_map>
#include <mutex>

namespace webview_gui::detail {

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

inline bool resolveContainedPath(const std::filesystem::path& root,
                                 const std::filesystem::path& requested,
                                 std::filesystem::path& resolved)
{
    const auto normalRoot = root.lexically_normal();
    auto relativeRequest = requested;

    // Resource paths are URI paths, so a leading '/' means "from the resource
    // root", not an OS filesystem root. A Windows drive/UNC root, however,
    // must never be accepted and rebased into the resource directory.
    if (relativeRequest.has_root_name())
        return false;
    if (relativeRequest.has_root_directory())
        relativeRequest = relativeRequest.relative_path();

    const auto candidate = (normalRoot / relativeRequest).lexically_normal();
    const auto relative = candidate.lexically_relative(normalRoot);

    if (relative.empty() && candidate != normalRoot)
        return false;

    for (const auto& part : relative) {
        if (part == "..")
            return false;
    }

    resolved = candidate;
    return true;
}

} // namespace webview_gui::detail
