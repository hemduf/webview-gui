#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace webview_gui::detail {

template <typename T>
class CallbackRegistry {
public:
    void set(const void* key, T* value)
    {
        if (key == nullptr || value == nullptr)
            return;

        auto replacement = std::make_shared<Entry>(value);
        std::shared_ptr<Entry> previous;

        {
            std::lock_guard lock{entriesMutex};
            const auto it = entries.find(key);
            if (it != entries.end())
                previous = it->second;
            entries.insert_or_assign(key, std::move(replacement));
        }

        if (previous)
            previous->stopAndWait();
    }

    template <typename Callback>
    bool visit(const void* key, Callback&& callback) const
    {
        if (key == nullptr)
            return false;

        std::shared_ptr<Entry> entry;
        {
            std::lock_guard lock{entriesMutex};
            const auto it = entries.find(key);
            if (it == entries.end())
                return false;
            entry = it->second;
        }

        T* value = nullptr;
        if (!entry->begin(value))
            return false;

        struct ActiveVisit {
            Entry& entry;
            ~ActiveVisit() { entry.end(); }
        } activeVisit{*entry};

        callback(*value);
        return true;
    }

    bool eraseIfMatches(const void* key, T* expected)
    {
        if (key == nullptr)
            return false;

        std::shared_ptr<Entry> removed;
        {
            std::lock_guard lock{entriesMutex};
            const auto it = entries.find(key);
            if (it == entries.end() || it->second->value != expected)
                return false;

            removed = it->second;
            entries.erase(it);
        }

        removed->stopAndWait();
        return true;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard lock{entriesMutex};
        return entries.size();
    }

private:
    struct Entry {
        explicit Entry(T* initialValue) noexcept : value(initialValue) {}

        bool begin(T*& result)
        {
            std::lock_guard lock{mutex};
            if (!accepting || value == nullptr)
                return false;

            ++activeVisitors;
            ++activeVisitorsByThread[std::this_thread::get_id()];
            result = value;
            return true;
        }

        void end() noexcept
        {
            std::lock_guard lock{mutex};

            const auto thread = std::this_thread::get_id();
            const auto threadIt = activeVisitorsByThread.find(thread);
            if (threadIt != activeVisitorsByThread.end()) {
                if (--threadIt->second == 0)
                    activeVisitorsByThread.erase(threadIt);
            }

            --activeVisitors;
            if (!accepting)
                drained.notify_all();
        }

        void stopAndWait()
        {
            std::unique_lock lock{mutex};
            accepting = false;

            const auto threadIt = activeVisitorsByThread.find(std::this_thread::get_id());
            const auto visitorsOnCallingThread = threadIt == activeVisitorsByThread.end()
                ? std::size_t{0}
                : threadIt->second;

            drained.wait(lock, [&] { return activeVisitors <= visitorsOnCallingThread; });
        }

        T* const value;
        std::mutex mutex;
        std::condition_variable drained;
        std::size_t activeVisitors = 0;
        std::unordered_map<std::thread::id, std::size_t> activeVisitorsByThread;
        bool accepting = true;
    };

    mutable std::mutex entriesMutex;
    std::unordered_map<const void*, std::shared_ptr<Entry>> entries;
};

} // namespace webview_gui::detail
