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

        typename Entry::Visitor visitor;
        T* value = nullptr;
        if (!entry->begin(visitor, value))
            return false;

        struct ActiveVisit {
            Entry& entry;
            typename Entry::Visitor& visitor;
            ~ActiveVisit() { entry.end(visitor); }
        } activeVisit{*entry, visitor};

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
        struct Visitor {
            std::thread::id thread;
            Visitor* previous = nullptr;
            Visitor* next = nullptr;
        };

        explicit Entry(T* initialValue) noexcept : value(initialValue) {}

        bool begin(Visitor& visitor, T*& result)
        {
            std::lock_guard lock{mutex};
            if (!accepting || value == nullptr)
                return false;

            visitor.thread = std::this_thread::get_id();
            visitor.next = visitors;
            if (visitors != nullptr)
                visitors->previous = &visitor;
            visitors = &visitor;

            ++activeVisitors;
            result = value;
            return true;
        }

        void end(Visitor& visitor) noexcept
        {
            std::lock_guard lock{mutex};

            if (visitor.previous != nullptr)
                visitor.previous->next = visitor.next;
            else
                visitors = visitor.next;
            if (visitor.next != nullptr)
                visitor.next->previous = visitor.previous;

            --activeVisitors;
            if (!accepting)
                drained.notify_all();
        }

        void stopAndWait()
        {
            std::unique_lock lock{mutex};
            accepting = false;

            const auto callingThread = std::this_thread::get_id();
            std::size_t visitorsOnCallingThread = 0;
            for (auto* visitor = visitors; visitor != nullptr; visitor = visitor->next) {
                if (visitor->thread == callingThread)
                    ++visitorsOnCallingThread;
            }

            drained.wait(lock, [&] { return activeVisitors <= visitorsOnCallingThread; });
        }

        T* const value;
        std::mutex mutex;
        std::condition_variable drained;
        Visitor* visitors = nullptr;
        std::size_t activeVisitors = 0;
        bool accepting = true;
    };

    mutable std::mutex entriesMutex;
    std::unordered_map<const void*, std::shared_ptr<Entry>> entries;
};

} // namespace webview_gui::detail
