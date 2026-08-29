#pragma once

#include "polysynth_voice_allocator.h"

#include <clap/events.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace webview_gui::examples::polysynth {

enum class ScheduledNoteKind : std::uint8_t {
    NoteOn,
    NoteOff,
    NoteChoke,
};

struct ScheduledNoteEvent {
    std::uint32_t time = 0;
    ScheduledNoteKind kind = ScheduledNoteKind::NoteOn;
    VoiceAllocator::VoiceIndex voiceIndex = VoiceAllocator::kInvalidVoice;
    VoiceIdentity identity{};
    double velocity = 0.0;
};

class NoteEventScheduler {
public:
    bool configure(std::size_t requestedVoices) noexcept {
        return allocator_.configure(requestedVoices);
    }

    void reset() noexcept { allocator_.reset(); }

    [[nodiscard]] VoiceAllocator::VoiceIndex capacity() const noexcept {
        return allocator_.capacity();
    }

    [[nodiscard]] VoiceAllocator::VoiceIndex activeCount() const noexcept {
        return allocator_.activeCount();
    }

    [[nodiscard]] bool voiceIdentity(VoiceAllocator::VoiceIndex index,
                                     VoiceIdentity &identity) const noexcept {
        return allocator_.voiceIdentity(index, identity);
    }

    template <typename Sink>
    bool process(const clap_input_events_t *events,
                 std::uint32_t framesCount,
                 Sink &sink) noexcept {
        static_assert(std::is_nothrow_invocable_v<Sink &, const ScheduledNoteEvent &>,
                      "PolySynth note scheduler sink must be noexcept");

        if (!events)
            return true;
        if (!events->size || !events->get)
            return false;

        const auto eventCount = events->size(events);
        std::uint32_t previousTime = 0;
        bool havePreviousTime = false;

        for (std::uint32_t eventIndex = 0; eventIndex < eventCount; ++eventIndex) {
            const auto *header = events->get(events, eventIndex);
            if (!header || header->size < sizeof(clap_event_header_t))
                return false;
            if (header->time > framesCount ||
                (havePreviousTime && header->time < previousTime))
                return false;

            previousTime = header->time;
            havePreviousTime = true;

            if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
                continue;
            if (header->type != CLAP_EVENT_NOTE_ON &&
                header->type != CLAP_EVENT_NOTE_OFF &&
                header->type != CLAP_EVENT_NOTE_CHOKE)
                continue;
            if (header->size < sizeof(clap_event_note_t))
                return false;

            const auto &event = *reinterpret_cast<const clap_event_note_t *>(header);
            if (header->type == CLAP_EVENT_NOTE_ON) {
                dispatchNoteOn(event, sink);
                continue;
            }

            if (!validWildcardAddress(event))
                continue;

            const auto kind = header->type == CLAP_EVENT_NOTE_OFF
                                  ? ScheduledNoteKind::NoteOff
                                  : ScheduledNoteKind::NoteChoke;
            dispatchMatching(event, kind, sink);
        }

        return true;
    }

private:
    static bool validNoteOn(const clap_event_note_t &event) noexcept {
        return event.note_id >= -1 &&
               event.port_index >= 0 &&
               event.channel >= 0 && event.channel <= 15 &&
               event.key >= 0 && event.key <= 127 &&
               std::isfinite(event.velocity) && event.velocity >= 0.0 && event.velocity <= 1.0;
    }

    static bool validWildcardAddress(const clap_event_note_t &event) noexcept {
        return event.note_id >= -1 &&
               event.port_index >= -1 &&
               event.channel >= -1 && event.channel <= 15 &&
               event.key >= -1 && event.key <= 127;
    }

    static VoiceIdentity identityFrom(const clap_event_note_t &event) noexcept {
        VoiceIdentity identity{};
        identity.noteId = event.note_id;
        identity.portIndex = event.port_index;
        identity.channel = event.channel;
        identity.key = event.key;
        return identity;
    }

    static bool fieldMatches(std::int32_t pattern, std::int32_t value) noexcept {
        return pattern == -1 || pattern == value;
    }

    static bool matches(const clap_event_note_t &pattern,
                        const VoiceIdentity &identity) noexcept {
        return fieldMatches(pattern.note_id, identity.noteId) &&
               fieldMatches(pattern.port_index, identity.portIndex) &&
               fieldMatches(pattern.channel, identity.channel) &&
               fieldMatches(pattern.key, identity.key);
    }

    template <typename Sink>
    void dispatchNoteOn(const clap_event_note_t &event, Sink &sink) noexcept {
        if (!validNoteOn(event))
            return;

        const auto identity = identityFrom(event);
        const auto voiceIndex = allocator_.allocate(identity);
        if (voiceIndex == VoiceAllocator::kInvalidVoice)
            return;

        const ScheduledNoteEvent scheduled{
            event.header.time,
            ScheduledNoteKind::NoteOn,
            voiceIndex,
            identity,
            event.velocity,
        };
        sink(scheduled);
    }

    template <typename Sink>
    void dispatchMatching(const clap_event_note_t &event,
                          ScheduledNoteKind kind,
                          Sink &sink) noexcept {
        for (VoiceAllocator::VoiceIndex voiceIndex = 0;
             voiceIndex < allocator_.capacity(); ++voiceIndex) {
            VoiceIdentity identity{};
            if (!allocator_.voiceIdentity(voiceIndex, identity) || !matches(event, identity))
                continue;

            const ScheduledNoteEvent scheduled{
                event.header.time,
                kind,
                voiceIndex,
                identity,
                kind == ScheduledNoteKind::NoteOff ? event.velocity : 0.0,
            };
            sink(scheduled);
        }
    }

    VoiceAllocator allocator_{};
};

} // namespace webview_gui::examples::polysynth
