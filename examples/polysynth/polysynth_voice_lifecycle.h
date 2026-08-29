#pragma once

#include "polysynth_note_scheduler.h"

#include <clap/events.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace webview_gui::examples::polysynth {

enum class VoiceStage : std::uint8_t {
    Inactive,
    Held,
    Releasing,
};

struct VoiceLifecycleEvent {
    ScheduledNoteEvent note{};
    std::uint64_t generation = 0;
};

class VoiceLifecycle {
public:
    bool configure(std::size_t requestedVoices) noexcept {
        if (!scheduler_.configure(requestedVoices))
            return false;
        clearStages();
        return true;
    }

    void reset() noexcept {
        scheduler_.reset();
        clearStages();
    }

    [[nodiscard]] VoiceAllocator::VoiceIndex capacity() const noexcept {
        return scheduler_.capacity();
    }

    [[nodiscard]] VoiceAllocator::VoiceIndex activeCount() const noexcept {
        return scheduler_.activeCount();
    }

    [[nodiscard]] VoiceStage stage(VoiceAllocator::VoiceIndex index) const noexcept {
        return index < scheduler_.capacity() ? slots_[index].stage : VoiceStage::Inactive;
    }

    [[nodiscard]] std::uint64_t generation(VoiceAllocator::VoiceIndex index) const noexcept {
        if (index >= scheduler_.capacity() || slots_[index].stage == VoiceStage::Inactive)
            return 0;
        return slots_[index].generation;
    }

    [[nodiscard]] bool voiceIdentity(VoiceAllocator::VoiceIndex index,
                                     VoiceIdentity &identity) const noexcept {
        return scheduler_.voiceIdentity(index, identity);
    }

    template <typename VoiceSink, typename NoteEndSink>
    bool process(const clap_input_events_t *events,
                 std::uint32_t framesCount,
                 VoiceSink &voiceSink,
                 NoteEndSink &noteEndSink) noexcept {
        static_assert(std::is_nothrow_invocable_v<VoiceSink &, const VoiceLifecycleEvent &>,
                      "PolySynth voice lifecycle sink must be noexcept");
        static_assert(std::is_nothrow_invocable_v<NoteEndSink &, const clap_event_note_t &>,
                      "PolySynth NOTE_END sink must be noexcept");

        auto scheduledSink = [this, &voiceSink, &noteEndSink](
                                 const ScheduledNoteEvent &event) noexcept {
            applyScheduled(event, voiceSink, noteEndSink);
        };
        return scheduler_.process(events, framesCount, scheduledSink);
    }

    template <typename NoteEndSink>
    bool completeRelease(VoiceAllocator::VoiceIndex index,
                         std::uint64_t expectedGeneration,
                         std::uint32_t time,
                         NoteEndSink &noteEndSink) noexcept {
        static_assert(std::is_nothrow_invocable_v<NoteEndSink &, const clap_event_note_t &>,
                      "PolySynth NOTE_END sink must be noexcept");

        if (index >= scheduler_.capacity() || slots_[index].stage != VoiceStage::Releasing ||
            expectedGeneration == 0 || slots_[index].generation != expectedGeneration)
            return false;

        const auto identity = slots_[index].identity;
        if (!scheduler_.retireVoice(index, identity))
            return false;

        slots_[index] = {};
        emitNoteEnd(identity, time, noteEndSink);
        return true;
    }

private:
    struct Slot {
        VoiceIdentity identity{};
        std::uint64_t generation = 0;
        VoiceStage stage = VoiceStage::Inactive;
    };

    void clearStages() noexcept {
        for (auto &slot : slots_)
            slot = {};
    }

    [[nodiscard]] std::uint64_t nextGeneration() noexcept {
        ++generationSerial_;
        if (generationSerial_ == 0)
            ++generationSerial_;
        return generationSerial_;
    }

    template <typename VoiceSink>
    static void dispatchVoice(const ScheduledNoteEvent &event,
                              std::uint64_t generation,
                              VoiceSink &voiceSink) noexcept {
        voiceSink(VoiceLifecycleEvent{event, generation});
    }

    template <typename VoiceSink, typename NoteEndSink>
    void applyScheduled(const ScheduledNoteEvent &event,
                        VoiceSink &voiceSink,
                        NoteEndSink &noteEndSink) noexcept {
        if (event.voiceIndex >= scheduler_.capacity())
            return;

        auto &slot = slots_[event.voiceIndex];
        switch (event.kind) {
            case ScheduledNoteKind::NoteOn:
                if (event.replacedVoice)
                    emitNoteEnd(event.replacedIdentity, event.time, noteEndSink);
                slot.identity = event.identity;
                slot.generation = nextGeneration();
                slot.stage = VoiceStage::Held;
                dispatchVoice(event, slot.generation, voiceSink);
                return;

            case ScheduledNoteKind::NoteOff:
                if (slot.stage == VoiceStage::Inactive || slot.identity != event.identity)
                    return;
                slot.stage = VoiceStage::Releasing;
                dispatchVoice(event, slot.generation, voiceSink);
                return;

            case ScheduledNoteKind::NoteChoke:
                if (slot.stage == VoiceStage::Inactive || slot.identity != event.identity)
                    return;
                dispatchVoice(event, slot.generation, voiceSink);
                if (!scheduler_.retireVoice(event.voiceIndex, event.identity))
                    return;
                slot = {};
                emitNoteEnd(event.identity, event.time, noteEndSink);
                return;
        }
    }

    template <typename NoteEndSink>
    static void emitNoteEnd(const VoiceIdentity &identity,
                            std::uint32_t time,
                            NoteEndSink &noteEndSink) noexcept {
        clap_event_note_t event{};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_END;
        event.header.flags = 0;
        event.note_id = identity.noteId;
        event.port_index = identity.portIndex;
        event.channel = identity.channel;
        event.key = identity.key;
        event.velocity = 0.0;
        noteEndSink(event);
    }

    NoteEventScheduler scheduler_{};
    std::array<Slot, VoiceAllocator::kMaximumVoices> slots_{};
    std::uint64_t generationSerial_ = 0;
};

} // namespace webview_gui::examples::polysynth
