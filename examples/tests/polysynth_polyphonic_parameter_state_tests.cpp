#include "polysynth_polyphonic_parameter_state.h"

#include <clap/events.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using webview_gui::examples::polysynth::PolyphonicParameterState;
using webview_gui::examples::polysynth::VoiceIdentity;

constexpr std::size_t kPitchSlot = 0;
constexpr std::size_t kFilterSlot = 1;

clap_event_param_mod_t makeMod(std::int32_t noteId,
                               std::int16_t portIndex,
                               std::int16_t channel,
                               std::int16_t key,
                               double amount) noexcept {
    clap_event_param_mod_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_MOD;
    event.note_id = noteId;
    event.port_index = portIndex;
    event.channel = channel;
    event.key = key;
    event.amount = amount;
    return event;
}

clap_event_param_value_t makeValue(std::int32_t noteId,
                                   std::int16_t portIndex,
                                   std::int16_t channel,
                                   std::int16_t key,
                                   double value) noexcept {
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.note_id = noteId;
    event.port_index = portIndex;
    event.channel = channel;
    event.key = key;
    event.value = value;
    return event;
}

bool nearlyEqual(double left, double right, double tolerance = 1.0e-12) noexcept {
    return std::fabs(left - right) <= tolerance;
}

bool readBase(const PolyphonicParameterState &state,
              std::uint32_t voiceIndex,
              std::size_t slot,
              double expected) noexcept {
    double value = 0.0;
    return state.baseValue(voiceIndex, slot, value) && nearlyEqual(value, expected);
}

bool readMod(const PolyphonicParameterState &state,
             std::uint32_t voiceIndex,
             std::size_t slot,
             double expected) noexcept {
    double value = 0.0;
    return state.modulation(voiceIndex, slot, value) && nearlyEqual(value, expected);
}

} // namespace

int main() {
    PolyphonicParameterState state;
    if (state.setGlobalBase(kPitchSlot, 0.25) ||
        state.setGlobalModulation(kPitchSlot, 0.1)) {
        std::cerr << "unconfigured parameter state accepted updates\n";
        return 1;
    }

    if (!state.configure(2)) {
        std::cerr << "failed to configure parameter state\n";
        return 1;
    }

    const VoiceIdentity first{10, 0, 2, 60};
    const VoiceIdentity second{11, 0, 2, 60};
    if (!state.startVoice(0, first) || !state.startVoice(1, second) ||
        state.startVoice(2, first)) {
        std::cerr << "voice activation contract failed\n";
        return 1;
    }

    if (!state.setGlobalBase(kPitchSlot, 0.25) ||
        !state.setGlobalModulation(kPitchSlot, 0.1) ||
        !readBase(state, 0, kPitchSlot, 0.25) ||
        !readBase(state, 1, kPitchSlot, 0.25) ||
        !readMod(state, 0, kPitchSlot, 0.1) ||
        !readMod(state, 1, kPitchSlot, 0.1)) {
        std::cerr << "global parameter defaults were not visible to both voices\n";
        return 1;
    }

    auto firstOnly = makeMod(10, 0, 2, 60, 0.35);
    if (!state.applyModulation(kPitchSlot, firstOnly) ||
        !readMod(state, 0, kPitchSlot, 0.35) ||
        !readMod(state, 1, kPitchSlot, 0.1)) {
        std::cerr << "note-id modulation leaked into an overlapping voice\n";
        return 1;
    }

    auto channelWide = makeMod(-1, 0, 2, -1, 0.2);
    if (!state.applyModulation(kPitchSlot, channelWide) ||
        !readMod(state, 0, kPitchSlot, 0.2) ||
        !readMod(state, 1, kPitchSlot, 0.2)) {
        std::cerr << "channel wildcard modulation did not target both matching voices\n";
        return 1;
    }

    auto secondOnly = makeMod(11, -1, -1, -1, 0.45);
    if (!state.applyModulation(kPitchSlot, secondOnly) ||
        !readMod(state, 0, kPitchSlot, 0.2) ||
        !readMod(state, 1, kPitchSlot, 0.45)) {
        std::cerr << "later note-id modulation did not become the targeted voice value\n";
        return 1;
    }

    auto globalMod = makeMod(-1, -1, -1, -1, 0.05);
    if (!state.applyModulation(kPitchSlot, globalMod) ||
        !readMod(state, 0, kPitchSlot, 0.2) ||
        !readMod(state, 1, kPitchSlot, 0.45)) {
        std::cerr << "global modulation overwrote active polyphonic modulation\n";
        return 1;
    }

    auto keyValue = makeValue(-1, 0, 2, 60, 0.75);
    if (!state.applyValue(kFilterSlot, keyValue) ||
        !readBase(state, 0, kFilterSlot, 0.75) ||
        !readBase(state, 1, kFilterSlot, 0.75) ||
        !state.setGlobalBase(kFilterSlot, 0.5) ||
        !readBase(state, 0, kFilterSlot, 0.75) ||
        !readBase(state, 1, kFilterSlot, 0.75)) {
        std::cerr << "polyphonic parameter value did not override the global base\n";
        return 1;
    }

    if (!state.stopVoice(1) || state.stopVoice(1)) {
        std::cerr << "voice reset contract failed\n";
        return 1;
    }

    const VoiceIdentity replacement{12, 0, 2, 60};
    if (!state.startVoice(1, replacement) ||
        !readMod(state, 1, kPitchSlot, 0.05) ||
        !readBase(state, 1, kFilterSlot, 0.5)) {
        std::cerr << "voice reuse leaked prior polyphonic parameter state\n";
        return 1;
    }

    auto keyOnly = makeMod(-1, -1, -1, 60, -0.3);
    if (!state.applyModulation(kFilterSlot, keyOnly) ||
        !readMod(state, 0, kFilterSlot, -0.3) ||
        !readMod(state, 1, kFilterSlot, -0.3)) {
        std::cerr << "key wildcard modulation did not match active identities\n";
        return 1;
    }

    auto invalidChannel = makeMod(-1, 0, 16, -1, 0.4);
    auto invalidKey = makeMod(-1, 0, 2, 128, 0.4);
    auto invalidNote = makeMod(-2, 0, 2, 60, 0.4);
    auto invalidAmount = makeMod(-1, -1, -1, -1,
                                 std::numeric_limits<double>::quiet_NaN());
    if (state.applyModulation(kPitchSlot, invalidChannel) ||
        state.applyModulation(kPitchSlot, invalidKey) ||
        state.applyModulation(kPitchSlot, invalidNote) ||
        state.applyModulation(kPitchSlot, invalidAmount) ||
        state.setGlobalBase(PolyphonicParameterState::kMaximumParameters, 0.0) ||
        state.setGlobalModulation(PolyphonicParameterState::kMaximumParameters, 0.0)) {
        std::cerr << "invalid parameter address/value was accepted\n";
        return 1;
    }

    if (!readMod(state, 0, kPitchSlot, 0.2) ||
        !readMod(state, 1, kPitchSlot, 0.05)) {
        std::cerr << "rejected events modified parameter state\n";
        return 1;
    }

    state.reset();
    double unused = 0.0;
    if (state.baseValue(0, kPitchSlot, unused) || state.modulation(0, kPitchSlot, unused)) {
        std::cerr << "reset left an active parameter voice\n";
        return 1;
    }

    return 0;
}
