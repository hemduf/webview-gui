#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace webview_gui::examples::polysynth {

struct VoiceIdentity {
    std::int32_t noteId = -1;
    std::int16_t portIndex = -1;
    std::int16_t channel = -1;
    std::int16_t key = -1;
};

constexpr bool operator==(const VoiceIdentity &left, const VoiceIdentity &right) noexcept {
    return left.noteId == right.noteId &&
           left.portIndex == right.portIndex &&
           left.channel == right.channel &&
           left.key == right.key;
}

constexpr bool operator!=(const VoiceIdentity &left, const VoiceIdentity &right) noexcept {
    return !(left == right);
}

class VoiceAllocator {
public:
    using VoiceIndex = std::uint32_t;

    static constexpr VoiceIndex kMaximumVoices = 64;
    static constexpr VoiceIndex kInvalidVoice = std::numeric_limits<VoiceIndex>::max();

    struct AllocationResult {
        VoiceIndex voiceIndex = kInvalidVoice;
        bool replacedVoice = false;
        VoiceIdentity replacedIdentity{};
    };

    bool configure(std::size_t requestedVoices) noexcept {
        if (requestedVoices == 0 || requestedVoices > kMaximumVoices)
            return false;

        capacity_ = static_cast<VoiceIndex>(requestedVoices);
        reset();
        return true;
    }

    void reset() noexcept {
        for (auto &slot : slots_)
            slot = {};
        activeCount_ = 0;
        allocationSerial_ = 0;
    }

    [[nodiscard]] VoiceIndex capacity() const noexcept { return capacity_; }
    [[nodiscard]] VoiceIndex activeCount() const noexcept { return activeCount_; }

    [[nodiscard]] bool voiceIdentity(VoiceIndex index, VoiceIdentity &identity) const noexcept {
        if (index >= capacity_ || !slots_[index].active)
            return false;
        identity = slots_[index].identity;
        return true;
    }

    [[nodiscard]] VoiceIndex findExact(const VoiceIdentity &identity) const noexcept {
        for (VoiceIndex index = 0; index < capacity_; ++index) {
            const auto &slot = slots_[index];
            if (slot.active && slot.identity == identity)
                return index;
        }
        return kInvalidVoice;
    }

    [[nodiscard]] AllocationResult allocateDetailed(const VoiceIdentity &identity) noexcept {
        if (capacity_ == 0)
            return {};

        for (VoiceIndex index = 0; index < capacity_; ++index) {
            if (!slots_[index].active) {
                activate(index, identity);
                ++activeCount_;
                return {index, false, {}};
            }
        }

        VoiceIndex oldest = 0;
        for (VoiceIndex index = 1; index < capacity_; ++index) {
            if (slots_[index].allocationSerial < slots_[oldest].allocationSerial)
                oldest = index;
        }

        const auto replacedIdentity = slots_[oldest].identity;
        activate(oldest, identity);
        return {oldest, true, replacedIdentity};
    }

    [[nodiscard]] VoiceIndex allocate(const VoiceIdentity &identity) noexcept {
        return allocateDetailed(identity).voiceIndex;
    }

    bool releaseAt(VoiceIndex index, const VoiceIdentity &expectedIdentity) noexcept {
        if (index >= capacity_ || !slots_[index].active ||
            slots_[index].identity != expectedIdentity)
            return false;

        slots_[index] = {};
        --activeCount_;
        return true;
    }

    bool releaseExact(const VoiceIdentity &identity) noexcept {
        const auto index = findExact(identity);
        return index != kInvalidVoice && releaseAt(index, identity);
    }

private:
    struct Slot {
        VoiceIdentity identity{};
        std::uint64_t allocationSerial = 0;
        bool active = false;
    };

    void activate(VoiceIndex index, const VoiceIdentity &identity) noexcept {
        auto &slot = slots_[index];
        slot.identity = identity;
        slot.allocationSerial = nextAllocationSerial();
        slot.active = true;
    }

    std::uint64_t nextAllocationSerial() noexcept {
        if (allocationSerial_ == std::numeric_limits<std::uint64_t>::max())
            renormalizeAllocationSerials();
        return ++allocationSerial_;
    }

    void renormalizeAllocationSerials() noexcept {
        // This path is only reachable after 2^64 allocations. Keep the behavior
        // defined and bounded anyway: rank active voices from oldest to newest
        // without allocating, preserving deterministic stealing order.
        std::uint64_t nextRank = 1;
        std::array<bool, kMaximumVoices> ranked{};

        for (VoiceIndex rank = 0; rank < activeCount_; ++rank) {
            VoiceIndex oldest = kInvalidVoice;
            for (VoiceIndex index = 0; index < capacity_; ++index) {
                const auto &slot = slots_[index];
                if (!slot.active || ranked[index])
                    continue;
                if (oldest == kInvalidVoice ||
                    slot.allocationSerial < slots_[oldest].allocationSerial)
                    oldest = index;
            }
            if (oldest == kInvalidVoice)
                break;
            slots_[oldest].allocationSerial = nextRank++;
            ranked[oldest] = true;
        }

        allocationSerial_ = nextRank - 1;
    }

    std::array<Slot, kMaximumVoices> slots_{};
    VoiceIndex capacity_ = 0;
    VoiceIndex activeCount_ = 0;
    std::uint64_t allocationSerial_ = 0;
};

} // namespace webview_gui::examples::polysynth
