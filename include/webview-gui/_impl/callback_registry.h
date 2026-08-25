#pragma once

#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace webview_gui::detail {

template <typename T>
class CallbackRegistry {
public:
    void set(const void* key, T* value)
    {
        if (key == nullptr || value == nullptr)
            return;

        std::unique_lock lock{mutex};
        entries.insert_or_assign(key, value);
    }

    template <typename Callback>
    bool visit(const void* key, Callback&& callback) const
    {
        if (key == nullptr)
            return false;

        std::shared_lock lock{mutex};
        const auto it = entries.find(key);
        if (it == entries.end() || it->second == nullptr)
            return false;

        callback(*it->second);
        return true;
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

} // namespace webview_gui::detail
