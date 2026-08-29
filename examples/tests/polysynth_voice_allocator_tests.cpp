#include "polysynth_voice_allocator.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>

namespace {

using webview_gui::examples::polysynth::VoiceIdentity;
using webview_gui::examples::polysynth::VoiceAllocator;

VoiceIdentity identity(int noteId, int key) noexcept {
    VoiceIdentity value{};
    value.noteId = noteId;
    value.portIndex = 0;
    value.channel = 1;
    value.key = static_cast<int16_t>(key);
    return value;
}

} // namespace

int main() {
    static_assert(std::is_nothrow_destructible_v<VoiceAllocator>,
                  "voice allocator teardown must remain noexcept");

    VoiceAllocator allocator;
    if (!allocator.configure(2) || allocator.capacity() != 2 || allocator.activeCount() != 0) {
        std::cerr << "voice allocator rejected a valid fixed polyphony\n";
        return 1;
    }

    const auto first = identity(100, 60);
    const auto second = identity(101, 60);
    const auto third = identity(102, 64);

    const auto firstIndex = allocator.allocate(first);
    const auto secondIndex = allocator.allocate(second);
    if (firstIndex != 0 || secondIndex != 1 || allocator.activeCount() != 2) {
        std::cerr << "voice allocator did not use deterministic free-slot order\n";
        return 2;
    }

    if (allocator.findExact(first) != firstIndex || allocator.findExact(second) != secondIndex) {
        std::cerr << "overlapping same-key notes were not isolated by CLAP note identity\n";
        return 3;
    }

    const auto stolenIndex = allocator.allocate(third);
    if (stolenIndex != firstIndex || allocator.activeCount() != 2 ||
        allocator.findExact(first) != VoiceAllocator::kInvalidVoice ||
        allocator.findExact(second) != secondIndex ||
        allocator.findExact(third) != stolenIndex) {
        std::cerr << "full allocator did not deterministically steal the oldest active voice\n";
        return 4;
    }

    if (!allocator.releaseExact(second) || allocator.activeCount() != 1 ||
        allocator.findExact(second) != VoiceAllocator::kInvalidVoice) {
        std::cerr << "exact note release did not retire only the matching voice\n";
        return 5;
    }

    const auto replacement = identity(103, 67);
    if (allocator.allocate(replacement) != secondIndex || allocator.activeCount() != 2) {
        std::cerr << "allocator did not reuse the lowest available slot deterministically\n";
        return 6;
    }

    if (allocator.configure(0) || allocator.configure(VoiceAllocator::kMaximumVoices + 1) ||
        allocator.capacity() != 2 || allocator.activeCount() != 2) {
        std::cerr << "invalid polyphony configuration mutated live allocator state\n";
        return 7;
    }

    allocator.reset();
    if (allocator.activeCount() != 0 ||
        allocator.findExact(third) != VoiceAllocator::kInvalidVoice ||
        allocator.findExact(replacement) != VoiceAllocator::kInvalidVoice) {
        std::cerr << "allocator reset leaked prior note identity/state\n";
        return 8;
    }

    return 0;
}
