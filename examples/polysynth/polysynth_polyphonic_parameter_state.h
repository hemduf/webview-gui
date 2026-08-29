#pragma once

#include "polysynth_voice_allocator.h"

#include <clap/events.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace webview_gui::examples::polysynth {

// Fixed-capacity CLAP parameter state used by the PolySynth audio-thread event
// path. Host-visible base values and modulation are kept separate. A targeted
// value/modulation event installs an override only on matching active voices;
// later global changes therefore remain visible only to voices which do not have
// a more-specific active override.
//
// The class owns no heap state and performs only bounded scans over the activated
// voice capacity. It is intended to be mutated from the same audio-thread event
// stream that renders the synth; callers must not concurrently mutate/read it
// from another thread without an external handoff.
class PolyphonicParameterState {
public:
    static constexpr std::size_t kMaximumParameters = 16;

    bool configure(std::size_t requestedVoices) noexcept {
        if (requestedVoices == 0 || requestedVoices > VoiceAllocator::kMaximumVoices)
            return false;

        capacity_ = static_cast<std::uint32_t>(requestedVoices);
        globalBase_.fill(0.0);
        globalModulation_.fill(0.0);
        for (auto &voice : voices_)
            voice = {};
        configured_ = true;
        return true;
    }

    // reset() clears all ephemeral voice addressing and modulation while keeping
    // host-visible base parameter values. This mirrors CLAP's distinction between
    // persistent parameter values and process-time modulation state.
    void reset() noexcept {
        if (!configured_)
            return;
        globalModulation_.fill(0.0);
        for (auto &voice : voices_)
            voice = {};
    }

    bool startVoice(std::uint32_t voiceIndex, const VoiceIdentity &identity) noexcept {
        if (!configured_ || voiceIndex >= capacity_ || !validIdentity(identity))
            return false;

        auto &voice = voices_[voiceIndex];
        voice = {};
        voice.identity = identity;
        voice.active = true;
        return true;
    }

    bool stopVoice(std::uint32_t voiceIndex) noexcept {
        if (!configured_ || voiceIndex >= capacity_ || !voices_[voiceIndex].active)
            return false;
        voices_[voiceIndex] = {};
        return true;
    }

    bool setGlobalBase(std::size_t parameterSlot, double value) noexcept {
        if (!validSlot(parameterSlot) || !std::isfinite(value))
            return false;
        globalBase_[parameterSlot] = value;
        return true;
    }

    bool setGlobalModulation(std::size_t parameterSlot, double amount) noexcept {
        if (!validSlot(parameterSlot) || !std::isfinite(amount))
            return false;
        globalModulation_[parameterSlot] = amount;
        return true;
    }

    bool applyValue(std::size_t parameterSlot,
                    const clap_event_param_value_t &event) noexcept {
        if (!validSlot(parameterSlot) || !validValueEvent(event))
            return false;

        if (isGlobalAddress(event.note_id,
                            event.port_index,
                            event.channel,
                            event.key)) {
            globalBase_[parameterSlot] = event.value;
            return true;
        }

        for (std::uint32_t index = 0; index < capacity_; ++index) {
            auto &voice = voices_[index];
            if (!voice.active ||
                !matchesAddress(voice.identity,
                                event.note_id,
                                event.port_index,
                                event.channel,
                                event.key))
                continue;
            voice.baseOverride[parameterSlot] = event.value;
            voice.hasBaseOverride[parameterSlot] = true;
        }
        return true;
    }

    bool applyModulation(std::size_t parameterSlot,
                         const clap_event_param_mod_t &event) noexcept {
        if (!validSlot(parameterSlot) || !validModulationEvent(event))
            return false;

        if (isGlobalAddress(event.note_id,
                            event.port_index,
                            event.channel,
                            event.key)) {
            globalModulation_[parameterSlot] = event.amount;
            return true;
        }

        for (std::uint32_t index = 0; index < capacity_; ++index) {
            auto &voice = voices_[index];
            if (!voice.active ||
                !matchesAddress(voice.identity,
                                event.note_id,
                                event.port_index,
                                event.channel,
                                event.key))
                continue;
            // Per the CLAP params contract, a polyphonic modulation amount for a
            // voice already includes the monophonic contribution. It replaces the
            // global modulation for that addressed voice rather than adding to it.
            voice.modulationOverride[parameterSlot] = event.amount;
            voice.hasModulationOverride[parameterSlot] = true;
        }
        return true;
    }

    [[nodiscard]] bool baseValue(std::uint32_t voiceIndex,
                                 std::size_t parameterSlot,
                                 double &value) const noexcept {
        if (!validVoiceRead(voiceIndex, parameterSlot))
            return false;
        const auto &voice = voices_[voiceIndex];
        value = voice.hasBaseOverride[parameterSlot]
                    ? voice.baseOverride[parameterSlot]
                    : globalBase_[parameterSlot];
        return true;
    }

    [[nodiscard]] bool modulation(std::uint32_t voiceIndex,
                                  std::size_t parameterSlot,
                                  double &amount) const noexcept {
        if (!validVoiceRead(voiceIndex, parameterSlot))
            return false;
        const auto &voice = voices_[voiceIndex];
        amount = voice.hasModulationOverride[parameterSlot]
                     ? voice.modulationOverride[parameterSlot]
                     : globalModulation_[parameterSlot];
        return true;
    }

    [[nodiscard]] bool voiceIdentity(std::uint32_t voiceIndex,
                                     VoiceIdentity &identity) const noexcept {
        if (!configured_ || voiceIndex >= capacity_ || !voices_[voiceIndex].active)
            return false;
        identity = voices_[voiceIndex].identity;
        return true;
    }

    [[nodiscard]] std::uint32_t capacity() const noexcept {
        return configured_ ? capacity_ : 0;
    }

    static bool matchesAddress(const VoiceIdentity &identity,
                               std::int32_t noteId,
                               std::int16_t portIndex,
                               std::int16_t channel,
                               std::int16_t key) noexcept {
        return fieldMatches(noteId, identity.noteId) &&
               fieldMatches(portIndex, identity.portIndex) &&
               fieldMatches(channel, identity.channel) &&
               fieldMatches(key, identity.key);
    }

private:
    struct VoiceState {
        VoiceIdentity identity{};
        std::array<double, kMaximumParameters> baseOverride{};
        std::array<double, kMaximumParameters> modulationOverride{};
        std::array<bool, kMaximumParameters> hasBaseOverride{};
        std::array<bool, kMaximumParameters> hasModulationOverride{};
        bool active = false;
    };

    template <typename Field>
    static bool fieldMatches(Field pattern, Field value) noexcept {
        return pattern == static_cast<Field>(-1) || pattern == value;
    }

    static bool isGlobalAddress(std::int32_t noteId,
                                std::int16_t portIndex,
                                std::int16_t channel,
                                std::int16_t key) noexcept {
        return noteId == -1 && portIndex == -1 && channel == -1 && key == -1;
    }

    static bool validAddress(std::int32_t noteId,
                             std::int16_t portIndex,
                             std::int16_t channel,
                             std::int16_t key) noexcept {
        return noteId >= -1 && portIndex >= -1 &&
               channel >= -1 && channel <= 15 &&
               key >= -1 && key <= 127;
    }

    static bool validIdentity(const VoiceIdentity &identity) noexcept {
        return identity.noteId >= -1 && identity.portIndex >= 0 &&
               identity.channel >= 0 && identity.channel <= 15 &&
               identity.key >= 0 && identity.key <= 127;
    }

    static bool validValueEvent(const clap_event_param_value_t &event) noexcept {
        return event.header.size >= sizeof(clap_event_param_value_t) &&
               event.header.space_id == CLAP_CORE_EVENT_SPACE_ID &&
               event.header.type == CLAP_EVENT_PARAM_VALUE &&
               std::isfinite(event.value) &&
               validAddress(event.note_id,
                            event.port_index,
                            event.channel,
                            event.key);
    }

    static bool validModulationEvent(const clap_event_param_mod_t &event) noexcept {
        return event.header.size >= sizeof(clap_event_param_mod_t) &&
               event.header.space_id == CLAP_CORE_EVENT_SPACE_ID &&
               event.header.type == CLAP_EVENT_PARAM_MOD &&
               std::isfinite(event.amount) &&
               validAddress(event.note_id,
                            event.port_index,
                            event.channel,
                            event.key);
    }

    [[nodiscard]] bool validSlot(std::size_t parameterSlot) const noexcept {
        return configured_ && parameterSlot < kMaximumParameters;
    }

    [[nodiscard]] bool validVoiceRead(std::uint32_t voiceIndex,
                                      std::size_t parameterSlot) const noexcept {
        return configured_ && parameterSlot < kMaximumParameters &&
               voiceIndex < capacity_ && voices_[voiceIndex].active;
    }

    std::array<VoiceState, VoiceAllocator::kMaximumVoices> voices_{};
    std::array<double, kMaximumParameters> globalBase_{};
    std::array<double, kMaximumParameters> globalModulation_{};
    std::uint32_t capacity_ = 0;
    bool configured_ = false;
};

} // namespace webview_gui::examples::polysynth
