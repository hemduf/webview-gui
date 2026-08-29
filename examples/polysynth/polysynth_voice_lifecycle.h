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

    [[nodiscard]] bool voiceIdentity(VoiceAllocator::VoiceIndex index,
                                     VoiceIdentity &identity) const noexcept {
        return scheduler_.voiceIdentity(index, identity);
    }

    template <typename VoiceSink, typename NoteEndSink>
    bool process(const clap_input_events_t *events,
                 std::uint32_t framesCount,
                 VoiceSink &voiceSink,
                 NoteEndSink &noteEndSink) noexcept {
        static_assert(std::is_nothrow_invocable_v<VoiceSink &, const ScheduledNoteEvent &>,
                      "PolySynth voice sink must be noexcept");
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
                         std::uint32_t time,
                         NoteEndSink &noteEndSink) noexcept {
        static_assert(std::is_nothrow_invocable_v<NoteEndSink &, const clap_event_note_t &>,
                      "PolySynth NOTE_END sink must be noexcept");

        if (index >= scheduler_.capacity() || slots_[index].stage != VoiceStage::Releasing)
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
        VoiceStage stage = VoiceStage::Inactive;
    };

    void clearStages() noexcept {
        for (auto &slot : slots_)
            slot = {};
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
                slot.stage = VoiceStage::Held;
                voiceSink(event);
                return;

            case ScheduledNoteKind::NoteOff:
                if (slot.stage == VoiceStage::Inactive || slot.identity != event.identity)
                    return;
                slot.stage = VoiceStage::Releasing;
                voiceSink(event);
                return;

            case ScheduledNoteKind::NoteChoke:
                if (slot.stage == VoiceStage::Inactive || slot.identity != event.identity)
                    return;
                voiceSink(event);
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
};

} // namespace webview_gui::examples::polysynth
