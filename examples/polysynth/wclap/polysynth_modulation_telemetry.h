#pragma once

#include <clap/clap.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace webview_gui::examples::polysynth::wclap::detail {

enum class ModulationTelemetryKind : std::uint32_t {
    Modulation = 1u,
    NoteOn = 2u,
    NoteEnd = 3u,
};

struct ModulationTelemetryRecord {
    ModulationTelemetryKind kind = ModulationTelemetryKind::Modulation;
    std::uint32_t sampleOffset = 0u;
    clap_id paramId = CLAP_INVALID_ID;
    std::int32_t noteId = -1;
    std::int32_t portIndex = -1;
    std::int32_t channel = -1;
    std::int32_t key = -1;
    float amount = 0.0f;
};

static_assert(std::is_trivially_copyable_v<ModulationTelemetryRecord>,
              "RT modulation telemetry records must remain trivially copyable");
static_assert(sizeof(ModulationTelemetryRecord) == 32u,
              "The WVT2 wire encoder relies on a compact 32-byte logical record");

class ModulationTelemetryQueue {
public:
    static constexpr std::uint32_t kCapacity = 128u;
    static constexpr std::uint32_t kMaximumPending = kCapacity - 1u;

    ModulationTelemetryQueue() noexcept = default;
    ModulationTelemetryQueue(const ModulationTelemetryQueue &) = delete;
    ModulationTelemetryQueue &operator=(const ModulationTelemetryQueue &) = delete;

    // Reset is called only while processing is stopped (activate/deactivate/reset
    // lifecycle), so it never races the single RT producer.
    void reset() noexcept {
        readIndex_.store(0u, std::memory_order_relaxed);
        writeIndex_.store(0u, std::memory_order_relaxed);
        droppedCount_.store(0u, std::memory_order_relaxed);
    }

    // Single RT producer. Overflow is deliberately drop-newest: records already
    // published to the UI retain order and a cumulative counter tells the UI to
    // invalidate any coalesced current-state view instead of displaying stale data.
    [[nodiscard]] bool push(const ModulationTelemetryRecord &record) noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto next = increment(write);
        if (next == readIndex_.load(std::memory_order_acquire)) {
            droppedCount_.fetch_add(1u, std::memory_order_relaxed);
            return false;
        }
        records_[write] = record;
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    // UI/main-thread consumer. Copy does not consume until the WebView send has
    // succeeded, so transient host backpressure does not silently lose records.
    [[nodiscard]] std::uint32_t copyPending(
        std::array<ModulationTelemetryRecord, kMaximumPending> &destination) const noexcept {
        auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        std::uint32_t count = 0u;
        while (read != write && count < destination.size()) {
            destination[count++] = records_[read];
            read = increment(read);
        }
        return count;
    }

    void consume(std::uint32_t count) noexcept {
        auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        while (count != 0u && read != write) {
            read = increment(read);
            --count;
        }
        readIndex_.store(read, std::memory_order_release);
    }

    [[nodiscard]] std::uint32_t droppedCount() const noexcept {
        return droppedCount_.load(std::memory_order_acquire);
    }

private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "RT modulation telemetry indices must remain lock-free");

    static constexpr std::uint32_t increment(std::uint32_t value) noexcept {
        return (value + 1u) % kCapacity;
    }

    std::array<ModulationTelemetryRecord, kCapacity> records_{};
    std::atomic<std::uint32_t> readIndex_{0u};
    std::atomic<std::uint32_t> writeIndex_{0u};
    std::atomic<std::uint32_t> droppedCount_{0u};
};

} // namespace webview_gui::examples::polysynth::wclap::detail
