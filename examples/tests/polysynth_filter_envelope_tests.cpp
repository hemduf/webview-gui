#include "polysynth_parameter_voice_engine.h"
#include "polysynth_voice_engine.h"

#include <clap/clap.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using webview_gui::examples::polysynth::kFirstParameterId;
using webview_gui::examples::polysynth::ParameterSlot;
using webview_gui::examples::polysynth::ParameterVoiceEngine;
using webview_gui::examples::polysynth::VoiceEngine;

constexpr clap_id kFilterEnvelopeAmountId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::FilterEnvelopeAmount);

struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    void noteOn(std::uint32_t time,
                std::int32_t noteId,
                std::int16_t key,
                double velocity = 1.0) noexcept {
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = velocity;
        present = true;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *list) noexcept {
        if (!list || !list->ctx)
            return 0;
        return static_cast<const InputEvents *>(list->ctx)->present ? 1u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *list,
                                                    std::uint32_t index) noexcept {
        if (!list || !list->ctx || index != 0)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(list->ctx);
        return self.present ? &self.event.header : nullptr;
    }

    clap_event_note_t event{};
    bool present = false;
    clap_input_events_t input{};
};

struct IgnoreNoteEnd {
    void operator()(const clap_event_note_t &) noexcept {}
};

template <std::size_t Frames>
bool renderNote(VoiceEngine &engine,
                std::int32_t noteId,
                std::array<float, Frames> &left,
                std::array<float, Frames> &right) noexcept {
    InputEvents events;
    events.noteOn(0, noteId, 69);
    IgnoreNoteEnd noteEnd;
    return engine.process(&events.input,
                          static_cast<std::uint32_t>(Frames),
                          left.data(),
                          right.data(),
                          noteEnd);
}

template <std::size_t Frames>
double energy(const std::array<float, Frames> &samples,
              std::size_t begin = 0) noexcept {
    double total = 0.0;
    for (std::size_t i = begin; i < Frames; ++i)
        total += std::fabs(static_cast<double>(samples[i]));
    return total;
}

template <std::size_t Frames>
bool sameBlock(const std::array<float, Frames> &a,
               const std::array<float, Frames> &b,
               float tolerance = 1.0e-6f) noexcept {
    for (std::size_t i = 0; i < Frames; ++i) {
        if (std::fabs(a[i] - b[i]) > tolerance)
            return false;
    }
    return true;
}

template <std::size_t Frames>
bool finiteBlock(const std::array<float, Frames> &left,
                 const std::array<float, Frames> &right) noexcept {
    for (std::size_t i = 0; i < Frames; ++i) {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
            return false;
    }
    return true;
}

clap_event_param_value_t globalFilterEnvelopeValue(double value) noexcept {
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = kFilterEnvelopeAmountId;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    return event;
}

} // namespace

int main() {
    constexpr double sampleRate = 48000.0;

    VoiceEngine validation;
    if (validation.setFilterEnvelopeAmount(0.0f)) {
        std::cerr << "filter envelope amount accepted before engine configuration\n";
        return 1;
    }
    if (!validation.configure(1, sampleRate, 64))
        return 2;
    if (validation.setFilterEnvelopeAmount(-1.01f) ||
        validation.setFilterEnvelopeAmount(1.01f) ||
        validation.setFilterEnvelopeAmount(std::numeric_limits<float>::quiet_NaN())) {
        std::cerr << "filter envelope amount accepted a value outside [-1, 1]\n";
        return 3;
    }
    if (!validation.setFilterEnvelopeAmount(-1.0f) ||
        !validation.setFilterEnvelopeAmount(0.0f) ||
        !validation.setFilterEnvelopeAmount(1.0f)) {
        std::cerr << "filter envelope amount rejected a legal endpoint\n";
        return 4;
    }

    // Zero envelope amount must preserve the existing constant-filter path.
    VoiceEngine legacyFilter;
    VoiceEngine explicitZero;
    if (!legacyFilter.configure(1, sampleRate, 64) ||
        !explicitZero.configure(1, sampleRate, 64) ||
        !legacyFilter.setFilter(1200.0f, 0.2f) ||
        !explicitZero.setFilterEnvelopeAmount(0.0f) ||
        !explicitZero.setFilter(1200.0f, 0.2f))
        return 5;

    std::array<float, 256> legacyLeft{};
    std::array<float, 256> legacyRight{};
    std::array<float, 256> zeroLeft{};
    std::array<float, 256> zeroRight{};
    if (!renderNote(legacyFilter, 100, legacyLeft, legacyRight) ||
        !renderNote(explicitZero, 100, zeroLeft, zeroRight) ||
        !sameBlock(legacyLeft, zeroLeft) || !sameBlock(legacyRight, zeroRight)) {
        std::cerr << "zero filter envelope amount changed the constant-filter reference output\n";
        return 6;
    }

    // The filter envelope reuses the normalized amplitude-envelope shape, but
    // not note velocity. A positive full-scale amount maps to a four-octave
    // upward cutoff excursion. With an 80 Hz base cutoff and a 128-sample
    // attack, the settled portion must therefore open enough to pass much more
    // of the 440 Hz oscillator than the zero-amount reference.
    VoiceEngine closedReference;
    VoiceEngine openingEnvelope;
    if (!closedReference.configure(1, sampleRate, 64) ||
        !openingEnvelope.configure(1, sampleRate, 64) ||
        !closedReference.setAmpEnvelope(128, 0, 1.0f, 64) ||
        !openingEnvelope.setAmpEnvelope(128, 0, 1.0f, 64) ||
        !closedReference.setFilter(80.0f, 0.15f) ||
        !openingEnvelope.setFilter(80.0f, 0.15f) ||
        !closedReference.setFilterEnvelopeAmount(0.0f) ||
        !openingEnvelope.setFilterEnvelopeAmount(1.0f))
        return 7;

    std::array<float, 256> closedLeft{};
    std::array<float, 256> closedRight{};
    std::array<float, 256> openingLeft{};
    std::array<float, 256> openingRight{};
    if (!renderNote(closedReference, 200, closedLeft, closedRight) ||
        !renderNote(openingEnvelope, 200, openingLeft, openingRight))
        return 8;
    if (!(energy(openingLeft, 128) > energy(closedLeft, 128) * 4.0)) {
        std::cerr << "positive filter envelope amount did not open the low-pass cutoff\n";
        return 9;
    }

    // A negative amount moves cutoff in the opposite direction. At full
    // envelope level, -1 maps to a four-octave downward excursion, clamped to
    // the legal filter range.
    VoiceEngine openReference;
    VoiceEngine closingEnvelope;
    if (!openReference.configure(1, sampleRate, 64) ||
        !closingEnvelope.configure(1, sampleRate, 64) ||
        !openReference.setFilter(1200.0f, 0.15f) ||
        !closingEnvelope.setFilter(1200.0f, 0.15f) ||
        !openReference.setFilterEnvelopeAmount(0.0f) ||
        !closingEnvelope.setFilterEnvelopeAmount(-1.0f))
        return 10;

    std::array<float, 512> openLeft{};
    std::array<float, 512> openRight{};
    std::array<float, 512> closingLeft{};
    std::array<float, 512> closingRight{};
    if (!renderNote(openReference, 300, openLeft, openRight) ||
        !renderNote(closingEnvelope, 300, closingLeft, closingRight))
        return 11;
    if (!(energy(closingLeft, 256) < energy(openLeft, 256) * 0.25)) {
        std::cerr << "negative filter envelope amount did not close the low-pass cutoff\n";
        return 12;
    }

    // Filter-envelope amount is a future-voice default. Updating it must not
    // splice coefficient modulation into a voice which already owns a NOTE_ON
    // generation, while the next generation must observe the new default.
    VoiceEngine stableDefaults;
    VoiceEngine changedDefaults;
    if (!stableDefaults.configure(1, sampleRate, 64) ||
        !changedDefaults.configure(1, sampleRate, 64) ||
        !stableDefaults.setFilter(80.0f, 0.2f) ||
        !changedDefaults.setFilter(80.0f, 0.2f) ||
        !stableDefaults.setFilterEnvelopeAmount(0.0f) ||
        !changedDefaults.setFilterEnvelopeAmount(0.0f))
        return 13;

    std::array<float, 64> stableFirstLeft{};
    std::array<float, 64> stableFirstRight{};
    std::array<float, 64> changedFirstLeft{};
    std::array<float, 64> changedFirstRight{};
    if (!renderNote(stableDefaults, 400, stableFirstLeft, stableFirstRight) ||
        !renderNote(changedDefaults, 400, changedFirstLeft, changedFirstRight) ||
        !sameBlock(stableFirstLeft, changedFirstLeft))
        return 14;

    if (!changedDefaults.setFilterEnvelopeAmount(1.0f))
        return 15;

    IgnoreNoteEnd noteEnd;
    std::array<float, 64> stableSecondLeft{};
    std::array<float, 64> stableSecondRight{};
    std::array<float, 64> changedSecondLeft{};
    std::array<float, 64> changedSecondRight{};
    if (!stableDefaults.process(nullptr, 64, stableSecondLeft.data(), stableSecondRight.data(), noteEnd) ||
        !changedDefaults.process(nullptr, 64, changedSecondLeft.data(), changedSecondRight.data(), noteEnd) ||
        !sameBlock(stableSecondLeft, changedSecondLeft) ||
        !sameBlock(stableSecondRight, changedSecondRight)) {
        std::cerr << "active voice filter envelope changed when future defaults were updated\n";
        return 16;
    }

    stableDefaults.reset();
    changedDefaults.reset();
    std::array<float, 128> stableFutureLeft{};
    std::array<float, 128> stableFutureRight{};
    std::array<float, 128> changedFutureLeft{};
    std::array<float, 128> changedFutureRight{};
    if (!renderNote(stableDefaults, 401, stableFutureLeft, stableFutureRight) ||
        !renderNote(changedDefaults, 401, changedFutureLeft, changedFutureRight) ||
        sameBlock(stableFutureLeft, changedFutureLeft, 1.0e-4f)) {
        std::cerr << "future NOTE_ON did not snapshot the updated filter envelope amount\n";
        return 17;
    }

    // Dynamic filter coefficients and envelope progress must be independent of
    // host block partitioning. Rendering one 256-frame block must match rendering
    // the same note as 64 + 192 frames.
    VoiceEngine oneBlock;
    VoiceEngine splitBlocks;
    if (!oneBlock.configure(1, sampleRate, 64) ||
        !splitBlocks.configure(1, sampleRate, 64) ||
        !oneBlock.setAmpEnvelope(128, 64, 0.5f, 64) ||
        !splitBlocks.setAmpEnvelope(128, 64, 0.5f, 64) ||
        !oneBlock.setFilter(200.0f, 0.7f) ||
        !splitBlocks.setFilter(200.0f, 0.7f) ||
        !oneBlock.setFilterEnvelopeAmount(0.75f) ||
        !splitBlocks.setFilterEnvelopeAmount(0.75f))
        return 18;

    std::array<float, 256> oneLeft{};
    std::array<float, 256> oneRight{};
    if (!renderNote(oneBlock, 500, oneLeft, oneRight))
        return 19;

    InputEvents splitNote;
    splitNote.noteOn(0, 500, 69);
    std::array<float, 64> splitFirstLeft{};
    std::array<float, 64> splitFirstRight{};
    std::array<float, 192> splitSecondLeft{};
    std::array<float, 192> splitSecondRight{};
    if (!splitBlocks.process(&splitNote.input, 64,
                             splitFirstLeft.data(), splitFirstRight.data(), noteEnd) ||
        !splitBlocks.process(nullptr, 192,
                             splitSecondLeft.data(), splitSecondRight.data(), noteEnd))
        return 20;
    for (std::size_t i = 0; i < 64; ++i) {
        if (std::fabs(oneLeft[i] - splitFirstLeft[i]) > 1.0e-6f ||
            std::fabs(oneRight[i] - splitFirstRight[i]) > 1.0e-6f) {
            std::cerr << "filter envelope output depends on first host block boundary\n";
            return 21;
        }
    }
    for (std::size_t i = 0; i < 192; ++i) {
        if (std::fabs(oneLeft[i + 64] - splitSecondLeft[i]) > 1.0e-6f ||
            std::fabs(oneRight[i + 64] - splitSecondRight[i]) > 1.0e-6f) {
            std::cerr << "filter envelope output depends on later host block boundary\n";
            return 22;
        }
    }

    // Worst-case legal resonance plus a full upward envelope excursion must stay
    // finite even when the target cutoff clamps at the 0.45 * sample-rate limit.
    VoiceEngine extreme;
    if (!extreme.configure(1, sampleRate, 64) ||
        !extreme.setFilter(12000.0f, 0.99f) ||
        !extreme.setFilterEnvelopeAmount(1.0f))
        return 23;
    std::array<float, 2048> extremeLeft{};
    std::array<float, 2048> extremeRight{};
    if (!renderNote(extreme, 600, extremeLeft, extremeRight) ||
        !finiteBlock(extremeLeft, extremeRight)) {
        std::cerr << "legal resonant filter-envelope excursion produced NaN/Inf\n";
        return 24;
    }

    // #32 CLAP adapter contract: Filter Env Amount first enters the retained
    // parameter model before a later increment wires voice-local DSP composition.
    ParameterVoiceEngine adapter;
    if (!adapter.configure(1, sampleRate, 64))
        return 25;

    double retainedAmount = 99.0;
    if (!adapter.parameterBaseValue(kFilterEnvelopeAmountId, retainedAmount) ||
        std::fabs(retainedAmount) > 1.0e-12) {
        std::cerr << "Filter Env Amount is missing from the retained parameter model\n";
        return 26;
    }

    const auto setAmount = globalFilterEnvelopeValue(0.625);
    if (!adapter.applyParameterFlushEvent(setAmount.header) ||
        !adapter.parameterBaseValue(kFilterEnvelopeAmountId, retainedAmount) ||
        std::fabs(retainedAmount - 0.625) > 1.0e-12) {
        std::cerr << "Filter Env Amount PARAM_VALUE was not retained as the global base\n";
        return 27;
    }

    const auto invalidAmount = globalFilterEnvelopeValue(1.5);
    if (!adapter.applyParameterFlushEvent(invalidAmount.header) ||
        !adapter.parameterBaseValue(kFilterEnvelopeAmountId, retainedAmount) ||
        std::fabs(retainedAmount - 0.625) > 1.0e-12) {
        std::cerr << "out-of-range Filter Env Amount mutated the retained base\n";
        return 28;
    }

    adapter.reset();
    if (!adapter.parameterBaseValue(kFilterEnvelopeAmountId, retainedAmount) ||
        std::fabs(retainedAmount - 0.625) > 1.0e-12) {
        std::cerr << "reset discarded the retained Filter Env Amount base\n";
        return 29;
    }

    // Specialist-review regression: the non-RT configuration setter inherited
    // from VoiceEngine must keep the adapter's retained model synchronized.
    if (!adapter.setFilterEnvelopeAmount(-0.375f) ||
        !adapter.parameterBaseValue(kFilterEnvelopeAmountId, retainedAmount) ||
        std::fabs(retainedAmount + 0.375) > 1.0e-12) {
        std::cerr << "configuration Filter Env Amount setter diverged from retained state\n";
        return 30;
    }

    return 0;
}