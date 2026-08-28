#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace webview_gui {

// Fixed-capacity single-producer/single-consumer queue intended for publishing
// small POD-style state snapshots from an audio/DSP thread to the GUI thread.
//
// tryPush() and tryPop() are bounded O(1) operations: they allocate nothing,
// take no locks, perform no syscalls, and never wait. If the queue is full the
// producer receives false and may drop/coalesce that UI update instead of
// blocking the real-time callback.
//
// Exactly one producer thread and one consumer thread may access a queue. The
// usable capacity is Capacity - 1 because one slot distinguishes full/empty.
template <typename T, std::size_t Capacity>
class RealtimeToUiQueue {
    static_assert(Capacity >= 2, "RealtimeToUiQueue needs at least two slots");
    static_assert(std::is_trivially_copyable<T>::value,
                  "RealtimeToUiQueue payloads must be trivially copyable");
    static_assert(std::is_default_constructible<T>::value,
                  "RealtimeToUiQueue payloads must be default constructible");

    using Index = std::size_t;
    using AtomicIndex = std::atomic<Index>;

public:
    static constexpr bool isLockFree = AtomicIndex::is_always_lock_free;
    static constexpr std::size_t usableCapacity = Capacity - 1;

    static_assert(isLockFree,
                  "RealtimeToUiQueue requires lock-free atomic indices on the supported target");

    RealtimeToUiQueue() = default;
    RealtimeToUiQueue(const RealtimeToUiQueue&) = delete;
    RealtimeToUiQueue& operator=(const RealtimeToUiQueue&) = delete;

    [[nodiscard]] bool tryPush(const T& value) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = increment(write);

        if (next == readIndex.load(std::memory_order_acquire))
            return false;

        slots[write] = value;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool tryPop(T& value) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);

        if (read == writeIndex.load(std::memory_order_acquire))
            return false;

        value = slots[read];
        readIndex.store(increment(read), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return readIndex.load(std::memory_order_acquire)
            == writeIndex.load(std::memory_order_acquire);
    }

private:
    static constexpr Index increment(Index index) noexcept
    {
        ++index;
        return index == Capacity ? 0 : index;
    }

    std::array<T, Capacity> slots{};

    // Producer and consumer mostly write different indices. Keep them on
    // separate cache lines to avoid needless false sharing in the audio path.
    alignas(64) AtomicIndex writeIndex{0};
    alignas(64) AtomicIndex readIndex{0};
};

} // namespace webview_gui
