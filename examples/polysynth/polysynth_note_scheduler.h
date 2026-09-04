#pragma once

#include "polysynth_voice_allocator.h"

#include <clap/events.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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
    bool replacedVoice = false;
    VoiceIdentity replacedIdentity{};
};

class NoteEventScheduler {
public:
    bool configure(std::size_t requestedVoices) noexcept {
        if (!allocator_.configure(requestedVoices))
            return false;
        resetMidiState();
        return true;
    }

    void reset() noexcept {
        allocator_.reset();
        resetMidiState();
    }

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

    bool retireVoice(VoiceAllocator::VoiceIndex index,
                     const VoiceIdentity &identity) noexcept {
        if (index < sustainedReleases_.size())
            sustainedReleases_[index] = false;
        return allocator_.releaseAt(index, identity);
    }

    template <typename Sink>
    bool process(const clap_input_events_t *events,
                 std::uint32_t framesCount,
                 Sink &sink) noexcept {
        auto boundarySink = [](std::uint32_t) noexcept {};
        return processWithBoundaries(events, framesCount, boundarySink, sink);
    }

    // The boundary sink runs exactly once before the first event at each sample
    // timestamp and, critically, before allocator matching/allocation at that
    // timestamp. A voice engine can therefore render the preceding segment and
    // retire an envelope which ends at this boundary before same-sample NOTE_ON
    // allocation occurs.
    template <typename BoundarySink, typename Sink>
    bool processWithBoundaries(const clap_input_events_t *events,
                               std::uint32_t framesCount,
                               BoundarySink &boundarySink,
                               Sink &sink) noexcept {
        auto ignoredCoreEvent = [](const clap_event_header_t &) noexcept -> bool {
            return true;
        };
        return processWithBoundariesAndEvents(
            events, framesCount, boundarySink, ignoredCoreEvent, sink);
    }

    // Preserve host ordering while translating the MIDI 1.0 channel messages
    // emitted by clap-wrapper's standalone host into the PolySynth's native CLAP
    // voice/event model. Note messages use the deterministic allocator. Stateful
    // channel expression (pitch bend, pressure and selected CCs) is converted to
    // note expressions, including replay immediately before a later MIDI NOTE_ON.
    // Sustain and the standard all-notes/all-sound-off controllers are handled at
    // lifecycle level. Program Change and unmapped CC/system messages remain raw
    // core events and are accepted as no-ops by the current engine.
    template <typename BoundarySink, typename CoreEventSink, typename Sink>
    bool processWithBoundariesAndEvents(const clap_input_events_t *events,
                                        std::uint32_t framesCount,
                                        BoundarySink &boundarySink,
                                        CoreEventSink &coreEventSink,
                                        Sink &sink) noexcept {
        static_assert(std::is_nothrow_invocable_v<BoundarySink &, std::uint32_t>,
                      "PolySynth scheduler boundary sink must be noexcept");
        static_assert(
            std::is_nothrow_invocable_r_v<bool,
                                          CoreEventSink &,
                                          const clap_event_header_t &>,
            "PolySynth core-event sink must be noexcept and return bool");
        static_assert(std::is_nothrow_invocable_v<Sink &, const ScheduledNoteEvent &>,
                      "PolySynth note scheduler sink must be noexcept");

        if (!events)
            return true;
        if (!events->size || !events->get)
            return false;

        const auto eventCount = events->size(events);
        std::uint32_t previousTime = 0;
        bool havePreviousTime = false;
        std::uint32_t boundaryTime = 0;
        bool haveBoundaryTime = false;

        for (std::uint32_t eventIndex = 0; eventIndex < eventCount; ++eventIndex) {
            const auto *header = events->get(events, eventIndex);
            if (!header || header->size < sizeof(clap_event_header_t))
                return false;
            if (header->time >= framesCount ||
                (havePreviousTime && header->time < previousTime))
                return false;

            previousTime = header->time;
            havePreviousTime = true;

            if (!haveBoundaryTime || header->time != boundaryTime) {
                boundarySink(header->time);
                boundaryTime = header->time;
                haveBoundaryTime = true;
            }

            if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
                continue;

            if (header->type == CLAP_EVENT_MIDI) {
                if (header->size < sizeof(clap_event_midi_t))
                    return false;
                const auto &midi = *reinterpret_cast<const clap_event_midi_t *>(header);
                if (!processMidiEvent(midi, coreEventSink, sink))
                    return false;
                continue;
            }

            if (header->type != CLAP_EVENT_NOTE_ON &&
                header->type != CLAP_EVENT_NOTE_OFF &&
                header->type != CLAP_EVENT_NOTE_CHOKE) {
                if (!coreEventSink(*header))
                    return false;
                continue;
            }

            if (header->size < sizeof(clap_event_note_t))
                return false;

            const auto &event = *reinterpret_cast<const clap_event_note_t *>(header);
            if (header->type == CLAP_EVENT_NOTE_ON) {
                if (!dispatchNoteOn(event, coreEventSink, sink))
                    return false;
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
    struct MidiChannelState {
        std::uint16_t pitchBend = 8192u;
        std::uint8_t pitchBendRangeCoarse = 2u;
        std::uint8_t pitchBendRangeFine = 0u;
        std::uint8_t rpnMsb = 127u;
        std::uint8_t rpnLsb = 127u;
        double channelPressure = 0.0;
        double channelVolume = 1.0;
        double expression = 1.0;
        double pan = 0.5;
        double brightness = 0.0;
        bool sustain = false;
    };

    void resetMidiState() noexcept {
        midiChannels_.fill(MidiChannelState{});
        sustainedReleases_.fill(false);
    }

    static bool validMidiDataByte(std::uint8_t value) noexcept {
        return value <= 0x7fu;
    }

    static std::uint8_t midiFamily(const clap_event_midi_t &midi) noexcept {
        return static_cast<std::uint8_t>(midi.data[0] & 0xf0u);
    }

    static std::uint8_t midiChannel(const clap_event_midi_t &midi) noexcept {
        return static_cast<std::uint8_t>(midi.data[0] & 0x0fu);
    }

    static bool midiPortCanAddressClap(const clap_event_midi_t &midi) noexcept {
        return midi.port_index <= static_cast<std::uint16_t>(
                                      std::numeric_limits<std::int16_t>::max());
    }

    static double normalizedMidi7(std::uint8_t value) noexcept {
        return static_cast<double>(value) / 127.0;
    }

    static double pitchBendRange(const MidiChannelState &state) noexcept {
        const auto configured = static_cast<double>(state.pitchBendRangeCoarse) +
                                static_cast<double>(state.pitchBendRangeFine) / 100.0;
        return std::min(configured, 120.0);
    }

    static double pitchBendSemitones(const MidiChannelState &state) noexcept {
        const auto centered = static_cast<int>(state.pitchBend) - 8192;
        const double normalized = centered >= 0
                                      ? static_cast<double>(centered) / 8191.0
                                      : static_cast<double>(centered) / 8192.0;
        return normalized * pitchBendRange(state);
    }

    template <typename CoreEventSink>
    static bool emitNoteExpression(const clap_event_midi_t &midi,
                                   std::int32_t expressionId,
                                   std::int16_t key,
                                   double value,
                                   CoreEventSink &coreEventSink) noexcept {
        if (!midiPortCanAddressClap(midi) || !std::isfinite(value))
            return false;

        clap_event_note_expression_t expression{};
        expression.header = midi.header;
        expression.header.size = sizeof(expression);
        expression.header.type = CLAP_EVENT_NOTE_EXPRESSION;
        expression.expression_id = expressionId;
        expression.note_id = -1;
        expression.port_index = static_cast<std::int16_t>(midi.port_index);
        expression.channel = static_cast<std::int16_t>(midiChannel(midi));
        expression.key = key;
        expression.value = value;
        return coreEventSink(expression.header);
    }

    template <typename CoreEventSink>
    bool emitMidiChannelStateForNoteOn(const clap_event_midi_t &midi,
                                       CoreEventSink &coreEventSink) noexcept {
        const auto channel = midiChannel(midi);
        const auto &state = midiChannels_[channel];
        const auto key = static_cast<std::int16_t>(midi.data[1]);
        const auto performance = std::clamp(state.channelVolume * state.expression, 0.0, 1.0);
        return emitNoteExpression(midi,
                                  CLAP_NOTE_EXPRESSION_TUNING,
                                  key,
                                  pitchBendSemitones(state),
                                  coreEventSink) &&
               emitNoteExpression(midi,
                                  CLAP_NOTE_EXPRESSION_PRESSURE,
                                  key,
                                  state.channelPressure,
                                  coreEventSink) &&
               emitNoteExpression(midi,
                                  CLAP_NOTE_EXPRESSION_EXPRESSION,
                                  key,
                                  performance,
                                  coreEventSink) &&
               emitNoteExpression(midi,
                                  CLAP_NOTE_EXPRESSION_PAN,
                                  key,
                                  state.pan,
                                  coreEventSink) &&
               emitNoteExpression(midi,
                                  CLAP_NOTE_EXPRESSION_BRIGHTNESS,
                                  key,
                                  state.brightness,
                                  coreEventSink);
    }

    static bool translateMidiNote(const clap_event_midi_t &midi,
                                  clap_event_note_t &note) noexcept {
        const auto family = midiFamily(midi);
        if (family != 0x80u && family != 0x90u)
            return false;
        if (!validMidiDataByte(midi.data[1]) || !validMidiDataByte(midi.data[2]) ||
            !midiPortCanAddressClap(midi))
            return false;

        note = {};
        note.header = midi.header;
        note.header.size = sizeof(note);
        note.header.type = family == 0x90u && midi.data[2] != 0u
                               ? CLAP_EVENT_NOTE_ON
                               : CLAP_EVENT_NOTE_OFF;
        note.note_id = -1;
        note.port_index = static_cast<std::int16_t>(midi.port_index);
        note.channel = static_cast<std::int16_t>(midiChannel(midi));
        note.key = static_cast<std::int16_t>(midi.data[1]);
        note.velocity = normalizedMidi7(midi.data[2]);
        return true;
    }

    template <typename CoreEventSink, typename Sink>
    bool processMidiEvent(const clap_event_midi_t &midi,
                          CoreEventSink &coreEventSink,
                          Sink &sink) noexcept {
        const auto family = midiFamily(midi);
        if (family >= 0x80u && family <= 0xe0u && !midiPortCanAddressClap(midi))
            return coreEventSink(midi.header);

        if (family == 0x80u || family == 0x90u) {
            clap_event_note_t note{};
            if (!translateMidiNote(midi, note))
                return coreEventSink(midi.header);

            if (note.header.type == CLAP_EVENT_NOTE_ON) {
                if (!emitMidiChannelStateForNoteOn(midi, coreEventSink))
                    return false;
                return dispatchNoteOn(note, coreEventSink, sink);
            }

            const auto channel = midiChannel(midi);
            if (midiChannels_[channel].sustain) {
                deferMatching(note);
                return true;
            }
            dispatchMatching(note, ScheduledNoteKind::NoteOff, sink);
            return true;
        }

        if (family == 0xa0u) {
            if (!validMidiDataByte(midi.data[1]) || !validMidiDataByte(midi.data[2]))
                return coreEventSink(midi.header);
            return emitNoteExpression(midi,
                                      CLAP_NOTE_EXPRESSION_PRESSURE,
                                      static_cast<std::int16_t>(midi.data[1]),
                                      normalizedMidi7(midi.data[2]),
                                      coreEventSink);
        }

        if (family == 0xb0u) {
            if (!validMidiDataByte(midi.data[1]) || !validMidiDataByte(midi.data[2]))
                return coreEventSink(midi.header);
            return processMidiController(midi, coreEventSink, sink);
        }

        if (family == 0xd0u) {
            if (!validMidiDataByte(midi.data[1]))
                return coreEventSink(midi.header);
            auto &state = midiChannels_[midiChannel(midi)];
            state.channelPressure = normalizedMidi7(midi.data[1]);
            return emitNoteExpression(midi,
                                      CLAP_NOTE_EXPRESSION_PRESSURE,
                                      -1,
                                      state.channelPressure,
                                      coreEventSink);
        }

        if (family == 0xe0u) {
            if (!validMidiDataByte(midi.data[1]) || !validMidiDataByte(midi.data[2]))
                return coreEventSink(midi.header);
            auto &state = midiChannels_[midiChannel(midi)];
            state.pitchBend = static_cast<std::uint16_t>(midi.data[1]) |
                              static_cast<std::uint16_t>(midi.data[2]) << 7u;
            return emitNoteExpression(midi,
                                      CLAP_NOTE_EXPRESSION_TUNING,
                                      -1,
                                      pitchBendSemitones(state),
                                      coreEventSink);
        }

        // Program Change, unmapped channel messages and realtime/system messages
        // are deliberately kept as raw core events. The current PolySynth engine
        // accepts unknown core events as a no-op, which is RT-safe and leaves a
        // future preset/program policy outside the audio callback.
        return coreEventSink(midi.header);
    }

    template <typename CoreEventSink, typename Sink>
    bool processMidiController(const clap_event_midi_t &midi,
                               CoreEventSink &coreEventSink,
                               Sink &sink) noexcept {
        const auto channel = midiChannel(midi);
        auto &state = midiChannels_[channel];
        const auto controller = midi.data[1];
        const auto value = midi.data[2];
        const auto normalized = normalizedMidi7(value);

        switch (controller) {
            case 6u: // Data Entry MSB for RPN 0,0: pitch bend sensitivity semitones.
                if (state.rpnMsb != 0u || state.rpnLsb != 0u)
                    return coreEventSink(midi.header);
                state.pitchBendRangeCoarse = value;
                return emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_TUNING,
                                          -1,
                                          pitchBendSemitones(state),
                                          coreEventSink);

            case 7u: // Channel volume, composed with CC11 into EXPRESSION.
                state.channelVolume = normalized;
                return emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_EXPRESSION,
                                          -1,
                                          std::clamp(state.channelVolume * state.expression,
                                                     0.0,
                                                     1.0),
                                          coreEventSink);

            case 10u: // Pan.
                state.pan = normalized;
                return emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_PAN,
                                          -1,
                                          state.pan,
                                          coreEventSink);

            case 11u: // Expression.
                state.expression = normalized;
                return emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_EXPRESSION,
                                          -1,
                                          std::clamp(state.channelVolume * state.expression,
                                                     0.0,
                                                     1.0),
                                          coreEventSink);

            case 38u: // Data Entry LSB for RPN 0,0: pitch bend sensitivity cents.
                if (state.rpnMsb != 0u || state.rpnLsb != 0u)
                    return coreEventSink(midi.header);
                state.pitchBendRangeFine = static_cast<std::uint8_t>(std::min<unsigned>(value, 99u));
                return emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_TUNING,
                                          -1,
                                          pitchBendSemitones(state),
                                          coreEventSink);

            case 64u: { // Sustain pedal.
                const bool sustain = value >= 64u;
                const bool releasePending = state.sustain && !sustain;
                state.sustain = sustain;
                if (releasePending)
                    releaseSustained(static_cast<std::int16_t>(midi.port_index),
                                     static_cast<std::int16_t>(channel),
                                     midi.header.time,
                                     sink);
                return true;
            }

            case 74u: // Common MPE/General Purpose brightness mapping.
                state.brightness = normalized;
                return emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_BRIGHTNESS,
                                          -1,
                                          state.brightness,
                                          coreEventSink);

            case 100u: // RPN LSB.
                state.rpnLsb = value;
                return true;

            case 101u: // RPN MSB.
                state.rpnMsb = value;
                return true;

            case 120u: { // All Sound Off: immediate choke, independent of sustain.
                const auto wildcard = midiWildcardNote(midi, CLAP_EVENT_NOTE_CHOKE);
                dispatchMatching(wildcard, ScheduledNoteKind::NoteChoke, sink);
                return true;
            }

            case 121u: { // Reset All Controllers.
                const bool hadSustain = state.sustain;
                state = MidiChannelState{};
                if (hadSustain)
                    releaseSustained(static_cast<std::int16_t>(midi.port_index),
                                     static_cast<std::int16_t>(channel),
                                     midi.header.time,
                                     sink);
                return emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_TUNING,
                                          -1,
                                          0.0,
                                          coreEventSink) &&
                       emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_PRESSURE,
                                          -1,
                                          0.0,
                                          coreEventSink) &&
                       emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_EXPRESSION,
                                          -1,
                                          1.0,
                                          coreEventSink) &&
                       emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_PAN,
                                          -1,
                                          0.5,
                                          coreEventSink) &&
                       emitNoteExpression(midi,
                                          CLAP_NOTE_EXPRESSION_BRIGHTNESS,
                                          -1,
                                          0.0,
                                          coreEventSink);
            }

            case 123u: { // All Notes Off: sustain-aware note-off for this channel.
                const auto wildcard = midiWildcardNote(midi, CLAP_EVENT_NOTE_OFF);
                if (state.sustain)
                    deferMatching(wildcard);
                else
                    dispatchMatching(wildcard, ScheduledNoteKind::NoteOff, sink);
                return true;
            }

            default:
                return coreEventSink(midi.header);
        }
    }

    static clap_event_note_t midiWildcardNote(const clap_event_midi_t &midi,
                                              std::uint16_t type) noexcept {
        clap_event_note_t note{};
        note.header = midi.header;
        note.header.size = sizeof(note);
        note.header.type = type;
        note.note_id = -1;
        note.port_index = static_cast<std::int16_t>(midi.port_index);
        note.channel = static_cast<std::int16_t>(midiChannel(midi));
        note.key = -1;
        note.velocity = 0.0;
        return note;
    }

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

    template <typename CoreEventSink>
    static auto notifyNoteOnDispatched(CoreEventSink &coreEventSink,
                                       const ScheduledNoteEvent &event,
                                       int) noexcept
        -> decltype(static_cast<bool>(coreEventSink.noteOnDispatched(event))) {
        static_assert(noexcept(coreEventSink.noteOnDispatched(event)),
                      "PolySynth note-on dispatch hook must be noexcept");
        return coreEventSink.noteOnDispatched(event);
    }

    template <typename CoreEventSink>
    static bool notifyNoteOnDispatched(CoreEventSink &,
                                       const ScheduledNoteEvent &,
                                       long) noexcept {
        return true;
    }

    template <typename CoreEventSink, typename Sink>
    bool dispatchNoteOn(const clap_event_note_t &event,
                        CoreEventSink &coreEventSink,
                        Sink &sink) noexcept {
        if (!validNoteOn(event))
            return true;

        const auto identity = identityFrom(event);
        const auto allocation = allocator_.allocateDetailed(identity);
        if (allocation.voiceIndex == VoiceAllocator::kInvalidVoice)
            return true;
        if (allocation.voiceIndex < sustainedReleases_.size())
            sustainedReleases_[allocation.voiceIndex] = false;

        const ScheduledNoteEvent scheduled{
            event.header.time,
            ScheduledNoteKind::NoteOn,
            allocation.voiceIndex,
            identity,
            event.velocity,
            allocation.replacedVoice,
            allocation.replacedIdentity,
        };
        sink(scheduled);
        return notifyNoteOnDispatched(coreEventSink, scheduled, 0);
    }

    void deferMatching(const clap_event_note_t &event) noexcept {
        for (VoiceAllocator::VoiceIndex voiceIndex = 0;
             voiceIndex < allocator_.capacity(); ++voiceIndex) {
            VoiceIdentity identity{};
            if (!allocator_.voiceIdentity(voiceIndex, identity) || !matches(event, identity))
                continue;
            if (voiceIndex < sustainedReleases_.size())
                sustainedReleases_[voiceIndex] = true;
        }
    }

    template <typename Sink>
    void releaseSustained(std::int16_t portIndex,
                          std::int16_t channel,
                          std::uint32_t time,
                          Sink &sink) noexcept {
        for (VoiceAllocator::VoiceIndex voiceIndex = 0;
             voiceIndex < allocator_.capacity(); ++voiceIndex) {
            if (voiceIndex >= sustainedReleases_.size() || !sustainedReleases_[voiceIndex])
                continue;
            VoiceIdentity identity{};
            if (!allocator_.voiceIdentity(voiceIndex, identity)) {
                sustainedReleases_[voiceIndex] = false;
                continue;
            }
            if ((portIndex >= 0 && identity.portIndex != portIndex) ||
                (channel >= 0 && identity.channel != channel))
                continue;

            sustainedReleases_[voiceIndex] = false;
            sink(ScheduledNoteEvent{
                time,
                ScheduledNoteKind::NoteOff,
                voiceIndex,
                identity,
                0.0,
                false,
                {},
            });
        }
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
            if (voiceIndex < sustainedReleases_.size())
                sustainedReleases_[voiceIndex] = false;

            const ScheduledNoteEvent scheduled{
                event.header.time,
                kind,
                voiceIndex,
                identity,
                kind == ScheduledNoteKind::NoteOff ? event.velocity : 0.0,
                false,
                {},
            };
            sink(scheduled);
        }
    }

    VoiceAllocator allocator_{};
    std::array<MidiChannelState, 16> midiChannels_{};
    std::array<bool, VoiceAllocator::kMaximumVoices> sustainedReleases_{};
};

} // namespace webview_gui::examples::polysynth
