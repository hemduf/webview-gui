#include "polysynth_plugin.h"
#include "polysynth_parameter_snapshot.h"
#include "polysynth_parameter_voice_engine.h"
#include "polysynth_preset_state.h"

#include <clap/ext/note-name.h>
#include <clap/ext/remote-controls.h>
#include <clap/ext/state-context.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>
#include <clap/ext/voice-info.h>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

namespace webview_gui::examples::polysynth {
namespace {

using PolySynthBase = clap::helpers::Plugin<
    clap::helpers::MisbehaviourHandler::Terminate,
    clap::helpers::CheckingLevel::Minimal>;

constexpr clap_id kHostMasterGainParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::MasterGain);
constexpr clap_id kHostWaveformParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::Waveform);
constexpr clap_id kHostCoarseTuneParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::CoarseTuning);
constexpr clap_id kHostFineTuneParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::FineTuning);
constexpr clap_id kHostFilterCutoffParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::FilterCutoff);
constexpr clap_id kHostFilterResonanceParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::FilterResonance);
constexpr clap_id kHostAmpAttackParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::AmpAttack);
constexpr clap_id kHostAmpDecayParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::AmpDecay);
constexpr clap_id kHostAmpSustainParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::AmpSustain);
constexpr clap_id kHostAmpReleaseParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::AmpRelease);
constexpr clap_id kHostFilterEnvelopeAmountParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::FilterEnvelopeAmount);
constexpr clap_id kHostPanParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::Pan);
constexpr clap_id kHostAmpLevelParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::AmpLevel);

// Host parameter indices are an append-only ABI surface. Keep the already-qualified
// 0..8 order unchanged and append the four pre-existing stable Amp Envelope IDs.
constexpr std::array<clap_id, 13> kPublishedHostParameterIds{{
    kHostFineTuneParameterId,
    kHostMasterGainParameterId,
    kHostWaveformParameterId,
    kHostCoarseTuneParameterId,
    kHostPanParameterId,
    kHostFilterCutoffParameterId,
    kHostFilterResonanceParameterId,
    kHostFilterEnvelopeAmountParameterId,
    kHostAmpLevelParameterId,
    kHostAmpAttackParameterId,
    kHostAmpDecayParameterId,
    kHostAmpSustainParameterId,
    kHostAmpReleaseParameterId,
}};

constexpr clap_id kTuningRemoteControlsPageId = 0x3200u;
constexpr clap_id kPerformanceRemoteControlsPageId = 0x3201u;
constexpr clap_id kFilterRemoteControlsPageId = 0x3202u;
constexpr clap_id kAmpEnvelopeRemoteControlsPageId = 0x3203u;
constexpr std::uint32_t kPolySynthBootstrapReleaseSamples = 64u;
constexpr std::uint32_t kMaximumFiniteTailSamples =
    static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) - 1u;
constexpr double kMaximumPublishedReleaseSeconds = 10.0;
constexpr double kMinimumFilterCutoffHz = 20.0;
constexpr double kMaximumFilterCutoffFraction = 0.45;

static_assert(std::atomic<float>::is_always_lock_free,
              "PolySynth requires lock-free host-visible float parameter snapshots");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "PolySynth state handoff and tail snapshots must be lock-free");
static_assert(std::atomic<std::int32_t>::is_always_lock_free,
              "PolySynth coarse-tune host snapshot must be lock-free");
static_assert(sizeof(float) == sizeof(std::uint32_t),
              "PolySynth state requires a 32-bit float");
static_assert(std::numeric_limits<float>::is_iec559,
              "PolySynth state requires IEEE-754 single precision");
static_assert(sizeof(double) == sizeof(std::uint64_t),
              "PolySynth state requires a 64-bit double");
static_assert(std::numeric_limits<double>::is_iec559,
              "PolySynth state requires IEEE-754 double precision");
static_assert(kPolySynthDefaultVoiceCount > 0 &&
                  kPolySynthDefaultVoiceCount <= VoiceAllocator::kMaximumVoices,
              "PolySynth voice-info must report a valid configured voice count");
static_assert(kTuningRemoteControlsPageId != CLAP_INVALID_ID &&
                  kPerformanceRemoteControlsPageId != CLAP_INVALID_ID &&
                  kFilterRemoteControlsPageId != CLAP_INVALID_ID &&
                  kAmpEnvelopeRemoteControlsPageId != CLAP_INVALID_ID &&
                  kTuningRemoteControlsPageId != kPerformanceRemoteControlsPageId &&
                  kTuningRemoteControlsPageId != kFilterRemoteControlsPageId &&
                  kTuningRemoteControlsPageId != kAmpEnvelopeRemoteControlsPageId &&
                  kPerformanceRemoteControlsPageId != kFilterRemoteControlsPageId &&
                  kPerformanceRemoteControlsPageId != kAmpEnvelopeRemoteControlsPageId &&
                  kFilterRemoteControlsPageId != kAmpEnvelopeRemoteControlsPageId,
              "PolySynth remote-controls page IDs must be valid and unique");
static_assert(kPolySynthBootstrapReleaseSamples < kMaximumFiniteTailSamples,
              "PolySynth bootstrap release must remain a finite CLAP tail");

const char *const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    kPolySynthPluginId,
    "webview-gui PolySynth",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "",
    "",
    "0.1.0",
    "Polyphonic CLAP reference instrument",
    kFeatures,
};

constexpr std::array<std::uint8_t, 8> kStateMagic{{'W', 'V', 'P', 'S', 'Y', 'N', 'T', 'H'}};
constexpr std::uint32_t kStateVersion = 10u;
constexpr std::size_t kLegacyStateSize = 24u;
constexpr std::size_t kStateV3Size = 28u;
constexpr std::size_t kStateV4Size = 32u;
constexpr std::size_t kStateV5Size = 36u;
constexpr std::size_t kStateV6Size = 40u;
constexpr std::size_t kStateV7Size = 44u;
constexpr std::size_t kStateV8Size = 48u;
constexpr std::size_t kStateV9Size = 52u;
constexpr std::size_t kStateSize = 68u;

bool copyName(char *destination, std::size_t capacity, const char *text) noexcept {
    if (!destination || capacity == 0 || !text)
        return false;
    const int written = std::snprintf(destination, capacity, "%s", text);
    return written >= 0 && static_cast<std::size_t>(written) < capacity;
}

bool isGlobalParameterAddress(std::int32_t noteId,
                              std::int16_t portIndex,
                              std::int16_t channel,
                              std::int16_t key) noexcept {
    return noteId == -1 && portIndex == -1 && channel == -1 && key == -1;
}

bool isPublishedHostParameter(clap_id id) noexcept {
    return std::find(kPublishedHostParameterIds.begin(),
                     kPublishedHostParameterIds.end(),
                     id) != kPublishedHostParameterIds.end();
}

// CLAP treats tail values >= INT32_MAX as infinite. The published Release range is
// finite, so reject activations whose legal 10-second endpoint could not be
// represented as a finite tail and use deterministic half-up conversion thereafter.
bool releaseSecondsToTailSamples(double seconds,
                                 double sampleRate,
                                 std::uint32_t &samples) noexcept {
    if (!std::isfinite(seconds) || seconds < 0.0 ||
        !std::isfinite(sampleRate) || sampleRate <= 0.0)
        return false;
    if (sampleRate > static_cast<double>(kMaximumFiniteTailSamples) /
                         kMaximumPublishedReleaseSeconds)
        return false;

    const double scaled = seconds * sampleRate;
    if (!std::isfinite(scaled) || scaled < 0.0 ||
        scaled > static_cast<double>(kMaximumFiniteTailSamples))
        return false;

    auto rounded = static_cast<std::uint32_t>(scaled);
    if (scaled - static_cast<double>(rounded) >= 0.5) {
        if (rounded >= kMaximumFiniteTailSamples)
            return false;
        ++rounded;
    }
    if (rounded == 0u)
        rounded = 1u;
    samples = rounded;
    return true;
}

void storeU32Le(std::uint8_t *destination, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        destination[i] = static_cast<std::uint8_t>((value >> (i * 8u)) & 0xffu);
}

void storeU64Le(std::uint8_t *destination, std::uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        destination[i] = static_cast<std::uint8_t>((value >> (i * 8u)) & 0xffu);
}

std::uint32_t loadU32Le(const std::uint8_t *source) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(source[i]) << (i * 8u);
    return value;
}

std::uint64_t loadU64Le(const std::uint8_t *source) noexcept {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(source[i]) << (i * 8u);
    return value;
}

bool writeAll(const clap_ostream_t *stream,
              const std::uint8_t *data,
              std::size_t size) noexcept {
    if (!stream || !stream->write || (!data && size != 0))
        return false;

    std::size_t offset = 0;
    while (offset < size) {
        const auto written = stream->write(stream, data + offset, size - offset);
        if (written <= 0 || static_cast<std::uint64_t>(written) > size - offset)
            return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool readAll(const clap_istream_t *stream,
             std::uint8_t *data,
             std::size_t size) noexcept {
    if (!stream || !stream->read || (!data && size != 0))
        return false;

    std::size_t offset = 0;
    while (offset < size) {
        const auto read = stream->read(stream, data + offset, size - offset);
        if (read <= 0 || static_cast<std::uint64_t>(read) > size - offset)
            return false;
        offset += static_cast<std::size_t>(read);
    }
    return true;
}

std::array<std::uint8_t, kStateSize> encodeState(double fineTuneCents,
                                                  float masterGainDb,
                                                  std::uint32_t waveform,
                                                  std::int32_t coarseTuneSemitones,
                                                  float pan,
                                                  float filterCutoffHz,
                                                  float filterResonance,
                                                  float filterEnvelopeAmount,
                                                  float ampLevel,
                                                  float ampAttackSeconds,
                                                  float ampDecaySeconds,
                                                  float ampSustain,
                                                  float ampReleaseSeconds) noexcept {
    std::array<std::uint8_t, kStateSize> bytes{};
    std::copy(kStateMagic.begin(), kStateMagic.end(), bytes.begin());
    storeU32Le(bytes.data() + 8, kStateVersion);

    std::uint64_t fineTuneBits = 0;
    std::memcpy(&fineTuneBits, &fineTuneCents, sizeof(fineTuneBits));
    storeU64Le(bytes.data() + 12, fineTuneBits);

    std::uint32_t masterGainBits = 0;
    std::memcpy(&masterGainBits, &masterGainDb, sizeof(masterGainBits));
    storeU32Le(bytes.data() + 20, masterGainBits);
    storeU32Le(bytes.data() + 24, waveform);

    std::uint32_t coarseTuneBits = 0;
    std::memcpy(&coarseTuneBits, &coarseTuneSemitones, sizeof(coarseTuneBits));
    storeU32Le(bytes.data() + 28, coarseTuneBits);

    std::uint32_t panBits = 0;
    std::memcpy(&panBits, &pan, sizeof(panBits));
    storeU32Le(bytes.data() + 32, panBits);

    std::uint32_t cutoffBits = 0;
    std::memcpy(&cutoffBits, &filterCutoffHz, sizeof(cutoffBits));
    storeU32Le(bytes.data() + 36, cutoffBits);

    std::uint32_t resonanceBits = 0;
    std::memcpy(&resonanceBits, &filterResonance, sizeof(resonanceBits));
    storeU32Le(bytes.data() + 40, resonanceBits);

    std::uint32_t filterEnvelopeBits = 0;
    std::memcpy(&filterEnvelopeBits, &filterEnvelopeAmount, sizeof(filterEnvelopeBits));
    storeU32Le(bytes.data() + 44, filterEnvelopeBits);

    std::uint32_t ampLevelBits = 0;
    std::memcpy(&ampLevelBits, &ampLevel, sizeof(ampLevelBits));
    storeU32Le(bytes.data() + 48, ampLevelBits);

    std::uint32_t ampAttackBits = 0;
    std::memcpy(&ampAttackBits, &ampAttackSeconds, sizeof(ampAttackBits));
    storeU32Le(bytes.data() + 52, ampAttackBits);

    std::uint32_t ampDecayBits = 0;
    std::memcpy(&ampDecayBits, &ampDecaySeconds, sizeof(ampDecayBits));
    storeU32Le(bytes.data() + 56, ampDecayBits);

    std::uint32_t ampSustainBits = 0;
    std::memcpy(&ampSustainBits, &ampSustain, sizeof(ampSustainBits));
    storeU32Le(bytes.data() + 60, ampSustainBits);

    std::uint32_t ampReleaseBits = 0;
    std::memcpy(&ampReleaseBits, &ampReleaseSeconds, sizeof(ampReleaseBits));
    storeU32Le(bytes.data() + 64, ampReleaseBits);
    return bytes;
}

bool decodeState(const std::array<std::uint8_t, kStateSize> &bytes,
                 std::size_t encodedSize,
                 double &fineTuneCents,
                 float &masterGainDb,
                 std::uint32_t &waveform,
                 std::int32_t &coarseTuneSemitones,
                 float &pan,
                 float &filterCutoffHz,
                 float &filterResonance,
                 float &filterEnvelopeAmount,
                 float &ampLevel,
                 float &ampAttackSeconds,
                 float &ampDecaySeconds,
                 float &ampSustain,
                 float &ampReleaseSeconds) noexcept {
    if (!std::equal(kStateMagic.begin(), kStateMagic.end(), bytes.begin()))
        return false;

    const auto version = loadU32Le(bytes.data() + 8);
    if (version < 1u || version > kStateVersion)
        return false;
    const auto expectedSize = version < 3u ? kLegacyStateSize
                              : version == 3u ? kStateV3Size
                              : version == 4u ? kStateV4Size
                              : version == 5u ? kStateV5Size
                              : version == 6u ? kStateV6Size
                              : version == 7u ? kStateV7Size
                              : version == 8u ? kStateV8Size
                              : version == 9u ? kStateV9Size
                                              : kStateSize;
    if (encodedSize != expectedSize)
        return false;

    const auto fineTuneBits = loadU64Le(bytes.data() + 12);
    std::memcpy(&fineTuneCents, &fineTuneBits, sizeof(fineTuneCents));
    const auto *fineTuneSpec = parameterSpecForId(kHostFineTuneParameterId);
    if (!fineTuneSpec || !std::isfinite(fineTuneCents) ||
        fineTuneCents < fineTuneSpec->minValue || fineTuneCents > fineTuneSpec->maxValue)
        return false;

    waveform = 0u;
    coarseTuneSemitones = 0;
    pan = 0.0f;
    filterCutoffHz = 6000.0f;
    filterResonance = 0.0f;
    filterEnvelopeAmount = 0.0f;
    ampLevel = 1.0f;
    ampAttackSeconds = 0.01f;
    ampDecaySeconds = 0.1f;
    ampSustain = 0.8f;
    ampReleaseSeconds = 0.25f;
    if (version == 1u) {
        if (bytes[20] != 0u || bytes[21] != 0u || bytes[22] != 0u || bytes[23] != 0u)
            return false;
        masterGainDb = 0.0f;
        return true;
    }

    const auto masterGainBits = loadU32Le(bytes.data() + 20);
    std::memcpy(&masterGainDb, &masterGainBits, sizeof(masterGainBits));
    const auto *masterGainSpec = parameterSpecForId(kHostMasterGainParameterId);
    if (!masterGainSpec || !std::isfinite(masterGainDb) ||
        static_cast<double>(masterGainDb) < masterGainSpec->minValue ||
        static_cast<double>(masterGainDb) > masterGainSpec->maxValue)
        return false;

    if (version == 2u)
        return true;

    waveform = loadU32Le(bytes.data() + 24);
    const auto *waveformSpec = parameterSpecForId(kHostWaveformParameterId);
    if (!waveformSpec || static_cast<double>(waveform) < waveformSpec->minValue ||
        static_cast<double>(waveform) > waveformSpec->maxValue)
        return false;
    if (version == 3u)
        return true;

    const auto coarseTuneBits = loadU32Le(bytes.data() + 28);
    std::memcpy(&coarseTuneSemitones, &coarseTuneBits, sizeof(coarseTuneSemitones));
    const auto *coarseTuneSpec = parameterSpecForId(kHostCoarseTuneParameterId);
    if (!coarseTuneSpec ||
        static_cast<double>(coarseTuneSemitones) < coarseTuneSpec->minValue ||
        static_cast<double>(coarseTuneSemitones) > coarseTuneSpec->maxValue)
        return false;
    if (version == 4u)
        return true;

    const auto panBits = loadU32Le(bytes.data() + 32);
    std::memcpy(&pan, &panBits, sizeof(pan));
    const auto *panSpec = parameterSpecForId(kHostPanParameterId);
    if (!panSpec || !std::isfinite(pan) ||
        static_cast<double>(pan) < panSpec->minValue ||
        static_cast<double>(pan) > panSpec->maxValue)
        return false;
    if (version == 5u)
        return true;

    const auto cutoffBits = loadU32Le(bytes.data() + 36);
    std::memcpy(&filterCutoffHz, &cutoffBits, sizeof(filterCutoffHz));
    const auto *cutoffSpec = parameterSpecForId(kHostFilterCutoffParameterId);
    if (!cutoffSpec || !std::isfinite(filterCutoffHz) ||
        static_cast<double>(filterCutoffHz) < cutoffSpec->minValue ||
        static_cast<double>(filterCutoffHz) > cutoffSpec->maxValue)
        return false;
    if (version == 6u)
        return true;

    const auto resonanceBits = loadU32Le(bytes.data() + 40);
    std::memcpy(&filterResonance, &resonanceBits, sizeof(filterResonance));
    const auto *resonanceSpec = parameterSpecForId(kHostFilterResonanceParameterId);
    if (!resonanceSpec || !std::isfinite(filterResonance) ||
        static_cast<double>(filterResonance) < resonanceSpec->minValue ||
        static_cast<double>(filterResonance) > resonanceSpec->maxValue)
        return false;
    if (version == 7u)
        return true;

    const auto filterEnvelopeBits = loadU32Le(bytes.data() + 44);
    std::memcpy(&filterEnvelopeAmount, &filterEnvelopeBits, sizeof(filterEnvelopeAmount));
    const auto *filterEnvelopeSpec = parameterSpecForId(kHostFilterEnvelopeAmountParameterId);
    if (!filterEnvelopeSpec || !std::isfinite(filterEnvelopeAmount) ||
        static_cast<double>(filterEnvelopeAmount) < filterEnvelopeSpec->minValue ||
        static_cast<double>(filterEnvelopeAmount) > filterEnvelopeSpec->maxValue)
        return false;
    if (version == 8u)
        return true;

    const auto ampLevelBits = loadU32Le(bytes.data() + 48);
    std::memcpy(&ampLevel, &ampLevelBits, sizeof(ampLevel));
    const auto *ampLevelSpec = parameterSpecForId(kHostAmpLevelParameterId);
    if (!ampLevelSpec || !std::isfinite(ampLevel) ||
        static_cast<double>(ampLevel) < ampLevelSpec->minValue ||
        static_cast<double>(ampLevel) > ampLevelSpec->maxValue)
        return false;
    if (version == 9u)
        return true;

    const auto ampAttackBits = loadU32Le(bytes.data() + 52);
    const auto ampDecayBits = loadU32Le(bytes.data() + 56);
    const auto ampSustainBits = loadU32Le(bytes.data() + 60);
    const auto ampReleaseBits = loadU32Le(bytes.data() + 64);
    std::memcpy(&ampAttackSeconds, &ampAttackBits, sizeof(ampAttackSeconds));
    std::memcpy(&ampDecaySeconds, &ampDecayBits, sizeof(ampDecaySeconds));
    std::memcpy(&ampSustain, &ampSustainBits, sizeof(ampSustain));
    std::memcpy(&ampReleaseSeconds, &ampReleaseBits, sizeof(ampReleaseSeconds));

    const auto *ampAttackSpec = parameterSpecForId(kHostAmpAttackParameterId);
    const auto *ampDecaySpec = parameterSpecForId(kHostAmpDecayParameterId);
    const auto *ampSustainSpec = parameterSpecForId(kHostAmpSustainParameterId);
    const auto *ampReleaseSpec = parameterSpecForId(kHostAmpReleaseParameterId);
    return ampAttackSpec && ampDecaySpec && ampSustainSpec && ampReleaseSpec &&
           std::isfinite(ampAttackSeconds) && std::isfinite(ampDecaySeconds) &&
           std::isfinite(ampSustain) && std::isfinite(ampReleaseSeconds) &&
           static_cast<double>(ampAttackSeconds) >= ampAttackSpec->minValue &&
           static_cast<double>(ampAttackSeconds) <= ampAttackSpec->maxValue &&
           static_cast<double>(ampDecaySeconds) >= ampDecaySpec->minValue &&
           static_cast<double>(ampDecaySeconds) <= ampDecaySpec->maxValue &&
           static_cast<double>(ampSustain) >= ampSustainSpec->minValue &&
           static_cast<double>(ampSustain) <= ampSustainSpec->maxValue &&
           static_cast<double>(ampReleaseSeconds) >= ampReleaseSpec->minValue &&
           static_cast<double>(ampReleaseSeconds) <= ampReleaseSpec->maxValue;
}

struct NoteEndOutputSink {
    const clap_output_events_t *events = nullptr;
    bool ok = true;

    void operator()(const clap_event_note_t &event) noexcept {
        if (!events || !events->try_push || !events->try_push(events, &event.header))
            ok = false;
    }
};

struct PendingParameterStateInput {
    PendingParameterStateInput() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool prepare(const clap_input_events_t *newHostInput,
                 float fineTuneCents,
                 float masterGainDb,
                 std::uint32_t waveform,
                 std::int32_t coarseTuneSemitones,
                 float pan,
                 float filterCutoffHz,
                 float filterResonance,
                 float filterEnvelopeAmount,
                 float ampLevel,
                 float ampAttackSeconds,
                 float ampDecaySeconds,
                 float ampSustain,
                 float ampReleaseSeconds) noexcept {
        hostInput = newHostInput;
        hostCount = 0;
        valid = true;

        prepareEvent(events[0], kHostFineTuneParameterId, fineTuneCents);
        prepareEvent(events[1], kHostMasterGainParameterId, masterGainDb);
        prepareEvent(events[2], kHostWaveformParameterId, waveform);
        prepareEvent(events[3], kHostCoarseTuneParameterId, coarseTuneSemitones);
        prepareEvent(events[4], kHostPanParameterId, pan);
        prepareEvent(events[5], kHostFilterCutoffParameterId, filterCutoffHz);
        prepareEvent(events[6], kHostFilterResonanceParameterId, filterResonance);
        prepareEvent(events[7], kHostFilterEnvelopeAmountParameterId, filterEnvelopeAmount);
        prepareEvent(events[8], kHostAmpLevelParameterId, ampLevel);
        prepareEvent(events[9], kHostAmpAttackParameterId, ampAttackSeconds);
        prepareEvent(events[10], kHostAmpDecayParameterId, ampDecaySeconds);
        prepareEvent(events[11], kHostAmpSustainParameterId, ampSustain);
        prepareEvent(events[12], kHostAmpReleaseParameterId, ampReleaseSeconds);

        if (hostInput && hostInput->size && hostInput->get)
            hostCount = hostInput->size(hostInput);
        else if (hostInput)
            valid = false;

        if (hostCount > std::numeric_limits<std::uint32_t>::max() - events.size())
            valid = false;
        return valid;
    }

    static void prepareEvent(clap_event_param_value_t &event,
                             clap_id paramId,
                             double value) noexcept {
        event = {};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = paramId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *inputEvents) noexcept {
        if (!inputEvents || !inputEvents->ctx)
            return 0;
        const auto &self = *static_cast<const PendingParameterStateInput *>(inputEvents->ctx);
        return self.valid ? self.hostCount + static_cast<std::uint32_t>(self.events.size()) : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *inputEvents,
                                                    std::uint32_t index) noexcept {
        if (!inputEvents || !inputEvents->ctx)
            return nullptr;
        const auto &self = *static_cast<const PendingParameterStateInput *>(inputEvents->ctx);
        if (!self.valid)
            return nullptr;
        if (index < self.events.size())
            return &self.events[index].header;
        const auto hostIndex = index - static_cast<std::uint32_t>(self.events.size());
        if (!self.hostInput || hostIndex >= self.hostCount)
            return nullptr;
        return self.hostInput->get(self.hostInput, hostIndex);
    }

    const clap_input_events_t *hostInput = nullptr;
    std::array<clap_event_param_value_t, 13> events{};
    clap_input_events_t input{};
    std::uint32_t hostCount = 0;
    bool valid = false;
};

class PolySynthPlugin final : public PolySynthBase {
public:
    explicit PolySynthPlugin(const clap_host_t *host)
        : PolySynthBase(&kDescriptor, host), host_(host) {}

    ~PolySynthPlugin() override = default;

protected:
    bool init() noexcept override {
        if (host_ && host_->get_extension) {
            hostParams_ = static_cast<const clap_host_params_t *>(
                host_->get_extension(host_, CLAP_EXT_PARAMS));
            hostTail_ = static_cast<const clap_host_tail_t *>(
                host_->get_extension(host_, CLAP_EXT_TAIL));
        }
        return true;
    }

    bool activate(double sampleRate,
                  std::uint32_t minFrameCount,
                  std::uint32_t maxFrameCount) noexcept override {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || minFrameCount == 0 ||
            maxFrameCount < minFrameCount)
            return false;

        ParameterSnapshot retained{};
        std::uint32_t retainedRevision = 0u;
        if (!readEffectiveParameterSnapshot(retained, &retainedRevision))
            return false;

        std::uint32_t initialTailSamples = 0;
        if (!releaseSecondsToTailSamples(retained.ampReleaseSeconds,
                                         sampleRate,
                                         initialTailSamples))
            return false;

        const double maximumFilterCutoffHz = sampleRate * kMaximumFilterCutoffFraction;
        if (!std::isfinite(maximumFilterCutoffHz) ||
            maximumFilterCutoffHz < kMinimumFilterCutoffHz)
            return false;
        auto maximumFilterCutoffForEngine = static_cast<float>(maximumFilterCutoffHz);
        if (static_cast<double>(maximumFilterCutoffForEngine) > maximumFilterCutoffHz)
            maximumFilterCutoffForEngine =
                std::nextafter(maximumFilterCutoffForEngine, 0.0f);
        if (maximumFilterCutoffForEngine < static_cast<float>(kMinimumFilterCutoffHz))
            return false;
        const auto initialFilterCutoffHz =
            std::clamp(retained.filterCutoffHz,
                       static_cast<float>(kMinimumFilterCutoffHz),
                       maximumFilterCutoffForEngine);

        activeSampleRate_ = sampleRate;
        if (!engine_.configure(kPolySynthDefaultVoiceCount,
                               sampleRate,
                               kPolySynthBootstrapReleaseSamples) ||
            !engine_.setFilter(initialFilterCutoffHz, retained.filterResonance) ||
            !engine_.setFineTuningCents(retained.fineTuneCents) ||
            !applyRetainedParameterBaseToEngine(
                kHostMasterGainParameterId, retained.masterGainDb) ||
            !applyRetainedParameterBaseToEngine(
                kHostWaveformParameterId, retained.waveform) ||
            !applyRetainedParameterBaseToEngine(
                kHostCoarseTuneParameterId, retained.coarseTuneSemitones) ||
            !applyRetainedParameterBaseToEngine(
                kHostPanParameterId, retained.pan) ||
            !applyRetainedParameterBaseToEngine(
                kHostFilterCutoffParameterId, retained.filterCutoffHz) ||
            !applyRetainedParameterBaseToEngine(
                kHostFilterResonanceParameterId, retained.filterResonance) ||
            !applyRetainedParameterBaseToEngine(
                kHostFilterEnvelopeAmountParameterId, retained.filterEnvelopeAmount) ||
            !applyRetainedParameterBaseToEngine(
                kHostAmpLevelParameterId, retained.ampLevel) ||
            !applyRetainedParameterBaseToEngine(
                kHostAmpAttackParameterId, retained.ampAttackSeconds) ||
            !applyRetainedParameterBaseToEngine(
                kHostAmpDecayParameterId, retained.ampDecaySeconds) ||
            !applyRetainedParameterBaseToEngine(
                kHostAmpSustainParameterId, retained.ampSustain) ||
            !applyRetainedParameterBaseToEngine(
                kHostAmpReleaseParameterId, retained.ampReleaseSeconds))
            return false;

        publishHostParameterSnapshot(retained);
        currentTailSamples_.store(initialTailSamples, std::memory_order_release);
        pendingLoadedTailSamples_.store(initialTailSamples, std::memory_order_release);
        tailLoadedStateRevisionPublished_.store(retainedRevision,
                                                std::memory_order_release);
        appliedLoadedStateRevision_ = retainedRevision;
        appliedLoadedStateRevisionPublished_.store(retainedRevision,
                                                   std::memory_order_release);
        active_ = true;
        return true;
    }

    void deactivate() noexcept override {
        active_ = false;
        (void)materializePendingParameterSnapshotOnMainThread();
        engine_.reset();
    }

    bool startProcessing() noexcept override { return true; }
    void stopProcessing() noexcept override {}
    void reset() noexcept override { engine_.reset(); }

    clap_process_status process(const clap_process_t *processData) noexcept override {
        if (!processData || processData->frames_count == 0 ||
            processData->audio_inputs_count != 0 ||
            processData->audio_outputs_count != 1 || !processData->audio_outputs)
            return CLAP_PROCESS_ERROR;

        auto &output = processData->audio_outputs[0];
        if (output.channel_count != 2 || !output.data32 || output.data64 ||
            !output.data32[0] || !output.data32[1])
            return CLAP_PROCESS_ERROR;

        output.constant_mask = 0;

        const auto loadedRevision = loadedStateRevision_.load(std::memory_order_acquire);
        bool replayLoadedState = loadedRevision != appliedLoadedStateRevision_;
        const clap_input_events_t *inputEvents = processData->in_events;
        PendingParameterStateInput pendingInput;
        ParameterSnapshot pendingSnapshot{};
        if (replayLoadedState) {
            if (!tryReadPendingParameterSnapshot(pendingSnapshot) ||
                loadedRevision != loadedStateRevision_.load(std::memory_order_acquire)) {
                replayLoadedState = false;
            } else if (!pendingInput.prepare(
                           processData->in_events,
                           pendingSnapshot.fineTuneCents,
                           pendingSnapshot.masterGainDb,
                           pendingSnapshot.waveform,
                           pendingSnapshot.coarseTuneSemitones,
                           pendingSnapshot.pan,
                           pendingSnapshot.filterCutoffHz,
                           pendingSnapshot.filterResonance,
                           pendingSnapshot.filterEnvelopeAmount,
                           pendingSnapshot.ampLevel,
                           pendingSnapshot.ampAttackSeconds,
                           pendingSnapshot.ampDecaySeconds,
                           pendingSnapshot.ampSustain,
                           pendingSnapshot.ampReleaseSeconds)) {
                return CLAP_PROCESS_ERROR;
            } else {
                inputEvents = &pendingInput.input;
            }
        }

        NoteEndOutputSink noteEndSink{processData->out_events};
        const bool engineOk = engine_.process(inputEvents,
                                              processData->frames_count,
                                              output.data32[0],
                                              output.data32[1],
                                              noteEndSink);
        if (!engineOk || !noteEndSink.ok)
            return CLAP_PROCESS_ERROR;

        if (replayLoadedState)
            appliedLoadedStateRevision_ = loadedRevision;

        if (!syncParameterSnapshotsPreservingConcurrentStateLoad() ||
            !syncTailFromEngine(true))
            return CLAP_PROCESS_ERROR;

        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsTail() const noexcept override { return true; }

    std::uint32_t tailGet() const noexcept override {
        const auto loadedRevision = loadedStateRevision_.load(std::memory_order_acquire);
        const auto tailRevision =
            tailLoadedStateRevisionPublished_.load(std::memory_order_acquire);
        const auto publishedTail = currentTailSamples_.load(std::memory_order_acquire);
        if (loadedRevision == tailRevision || !active_)
            return publishedTail;
        const auto pendingTail =
            pendingLoadedTailSamples_.load(std::memory_order_acquire);
        return std::max(publishedTail, pendingTail);
    }

    bool implementsState() const noexcept override { return true; }

    bool stateSave(const clap_ostream_t *stream) noexcept override {
        ParameterSnapshot snapshot{};
        if (!readEffectiveParameterSnapshot(snapshot))
            return false;
        const auto bytes = encodeState(
            static_cast<double>(snapshot.fineTuneCents),
            snapshot.masterGainDb,
            snapshot.waveform,
            snapshot.coarseTuneSemitones,
            snapshot.pan,
            snapshot.filterCutoffHz,
            snapshot.filterResonance,
            snapshot.filterEnvelopeAmount,
            snapshot.ampLevel,
            snapshot.ampAttackSeconds,
            snapshot.ampDecaySeconds,
            snapshot.ampSustain,
            snapshot.ampReleaseSeconds);
        return writeAll(stream, bytes.data(), bytes.size());
    }

    bool stateLoad(const clap_istream_t *stream) noexcept override {
        std::array<std::uint8_t, kStateSize> bytes{};
        if (!readAll(stream, bytes.data(), kLegacyStateSize))
            return false;

        if (!std::equal(kStateMagic.begin(), kStateMagic.end(), bytes.begin()))
            return false;
        const auto version = loadU32Le(bytes.data() + 8);
        if (version < 1u || version > kStateVersion)
            return false;

        std::size_t encodedSize = kLegacyStateSize;
        if (version >= 3u) {
            if (!readAll(stream,
                         bytes.data() + kLegacyStateSize,
                         kStateV3Size - kLegacyStateSize))
                return false;
            encodedSize = kStateV3Size;
        }
        if (version >= 4u) {
            if (!readAll(stream,
                         bytes.data() + kStateV3Size,
                         kStateV4Size - kStateV3Size))
                return false;
            encodedSize = kStateV4Size;
        }
        if (version >= 5u) {
            if (!readAll(stream,
                         bytes.data() + kStateV4Size,
                         kStateV5Size - kStateV4Size))
                return false;
            encodedSize = kStateV5Size;
        }
        if (version >= 6u) {
            if (!readAll(stream,
                         bytes.data() + kStateV5Size,
                         kStateV6Size - kStateV5Size))
                return false;
            encodedSize = kStateV6Size;
        }
        if (version >= 7u) {
            if (!readAll(stream,
                         bytes.data() + kStateV6Size,
                         kStateV7Size - kStateV6Size))
                return false;
            encodedSize = kStateV7Size;
        }
        if (version >= 8u) {
            if (!readAll(stream,
                         bytes.data() + kStateV7Size,
                         kStateV8Size - kStateV7Size))
                return false;
            encodedSize = kStateV8Size;
        }
        if (version >= 9u) {
            if (!readAll(stream,
                         bytes.data() + kStateV8Size,
                         kStateV9Size - kStateV8Size))
                return false;
            encodedSize = kStateV9Size;
        }
        if (version >= 10u) {
            if (!readAll(stream,
                         bytes.data() + kStateV9Size,
                         kStateSize - kStateV9Size))
                return false;
            encodedSize = kStateSize;
        }

        std::uint8_t trailing = 0;
        const auto trailingRead = stream->read(stream, &trailing, 1);
        if (trailingRead != 0)
            return false;

        double fineTuneCents = 0.0;
        ParameterSnapshot loaded{};
        if (!decodeState(bytes,
                         encodedSize,
                         fineTuneCents,
                         loaded.masterGainDb,
                         loaded.waveform,
                         loaded.coarseTuneSemitones,
                         loaded.pan,
                         loaded.filterCutoffHz,
                         loaded.filterResonance,
                         loaded.filterEnvelopeAmount,
                         loaded.ampLevel,
                         loaded.ampAttackSeconds,
                         loaded.ampDecaySeconds,
                         loaded.ampSustain,
                         loaded.ampReleaseSeconds))
            return false;
        loaded.fineTuneCents = static_cast<float>(fineTuneCents);

        return commitPersistentParameterSnapshot(loaded, true);
    }

    bool implementsStateContext() const noexcept override { return true; }

    bool stateContextSave(const clap_ostream_t *stream,
                          std::uint32_t) noexcept override {
        return stateSave(stream);
    }

    bool stateContextLoad(const clap_istream_t *stream,
                          std::uint32_t) noexcept override {
        return stateLoad(stream);
    }

    bool implementsAudioPorts() const noexcept override { return true; }

    std::uint32_t audioPortsCount(bool isInput) const noexcept override {
        return isInput ? 0u : 1u;
    }

    bool audioPortsInfo(std::uint32_t index,
                        bool isInput,
                        clap_audio_port_info_t *info) const noexcept override {
        if (!info || isInput || index != 0)
            return false;
        *info = {};
        info->id = kPolySynthAudioOutputPortId;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return copyName(info->name, sizeof(info->name), "Stereo Out");
    }

    bool implementsNotePorts() const noexcept override { return true; }

    std::uint32_t notePortsCount(bool isInput) const noexcept override {
        return isInput ? 1u : 0u;
    }

    bool notePortsInfo(std::uint32_t index,
                       bool isInput,
                       clap_note_port_info_t *info) const noexcept override {
        if (!info || !isInput || index != 0)
            return false;
        *info = {};
        info->id = kPolySynthNoteInputPortId;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        return copyName(info->name, sizeof(info->name), "Notes In");
    }

    bool implementsNoteName() const noexcept override { return true; }

    std::uint32_t noteNameCount() noexcept override { return 128u; }

    bool noteNameGet(std::uint32_t index, clap_note_name *noteName) noexcept override {
        if (!noteName || index >= 128u)
            return false;

        static constexpr const char *kPitchClasses[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        *noteName = {};
        noteName->port = 0;
        noteName->key = static_cast<std::int16_t>(index);
        noteName->channel = -1;
        const int octave = static_cast<int>(index / 12u) - 1;
        const int written = std::snprintf(noteName->name,
                                          sizeof(noteName->name),
                                          "%s%d",
                                          kPitchClasses[index % 12u],
                                          octave);
        return written >= 0 && static_cast<std::size_t>(written) < sizeof(noteName->name);
    }

    bool implementsVoiceInfo() const noexcept override { return true; }

    bool voiceInfoGet(clap_voice_info_t *info) noexcept override {
        if (!active_ || !info)
            return false;

        *info = {};
        info->voice_count = static_cast<std::uint32_t>(kPolySynthDefaultVoiceCount);
        info->voice_capacity = VoiceAllocator::kMaximumVoices;
        info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES;
        return true;
    }

    bool implementRemoteControls() const noexcept override { return true; }

    std::uint32_t remoteControlsPageCount() noexcept override { return 4u; }

    bool remoteControlsPageGet(std::uint32_t pageIndex,
                               clap_remote_controls_page *page) noexcept override {
        if (!page || pageIndex >= remoteControlsPageCount())
            return false;

        *page = {};
        for (auto &paramId : page->param_ids)
            paramId = CLAP_INVALID_ID;
        page->is_for_preset = false;

        if (pageIndex == 0u) {
            page->page_id = kTuningRemoteControlsPageId;
            page->param_ids[0] = kHostFineTuneParameterId;
            page->param_ids[1] = kHostWaveformParameterId;
            page->param_ids[2] = kHostCoarseTuneParameterId;
            return copyName(page->section_name, sizeof(page->section_name), "Oscillator") &&
                   copyName(page->page_name, sizeof(page->page_name), "Tuning");
        }

        if (pageIndex == 1u) {
            page->page_id = kPerformanceRemoteControlsPageId;
            page->param_ids[0] = kHostMasterGainParameterId;
            page->param_ids[1] = kHostPanParameterId;
            page->param_ids[2] = kHostAmpLevelParameterId;
            return copyName(page->section_name, sizeof(page->section_name), "Output") &&
                   copyName(page->page_name, sizeof(page->page_name), "Performance");
        }

        if (pageIndex == 2u) {
            page->page_id = kFilterRemoteControlsPageId;
            page->param_ids[0] = kHostFilterCutoffParameterId;
            page->param_ids[1] = kHostFilterResonanceParameterId;
            page->param_ids[2] = kHostFilterEnvelopeAmountParameterId;
            return copyName(page->section_name, sizeof(page->section_name), "Filter") &&
                   copyName(page->page_name, sizeof(page->page_name), "Tone");
        }

        page->page_id = kAmpEnvelopeRemoteControlsPageId;
        page->param_ids[0] = kHostAmpAttackParameterId;
        page->param_ids[1] = kHostAmpDecayParameterId;
        page->param_ids[2] = kHostAmpSustainParameterId;
        page->param_ids[3] = kHostAmpReleaseParameterId;
        return copyName(page->section_name, sizeof(page->section_name), "Amp Envelope") &&
               copyName(page->page_name, sizeof(page->page_name), "ADSR");
    }

    bool implementsParams() const noexcept override { return true; }

    std::uint32_t paramsCount() const noexcept override {
        return static_cast<std::uint32_t>(kPublishedHostParameterIds.size());
    }

    bool paramsInfo(std::uint32_t paramIndex,
                    clap_param_info_t *info) const noexcept override {
        if (!info || paramIndex >= kPublishedHostParameterIds.size())
            return false;

        const clap_id paramId = kPublishedHostParameterIds[paramIndex];
        const auto *spec = parameterSpecForId(paramId);
        if (!spec)
            return false;

        *info = {};
        info->id = spec->id;
        info->flags = spec->flags;
        info->cookie = const_cast<void *>(static_cast<const void *>(spec));
        info->min_value = spec->minValue;
        info->max_value = spec->maxValue;
        info->default_value = spec->defaultValue;
        return copyName(info->name, sizeof(info->name), spec->name) &&
               copyName(info->module, sizeof(info->module), spec->module);
    }

    bool paramsValue(clap_id paramId, double *value) noexcept override {
        if (!value || !isPublishedHostParameter(paramId))
            return false;
        ParameterSnapshot snapshot{};
        if (!readEffectiveParameterSnapshot(snapshot))
            return false;
        return parameterSnapshotValue(snapshot, paramId, *value);
    }

    bool paramsValueToText(clap_id paramId,
                           double value,
                           char *display,
                           std::uint32_t size) noexcept override {
        if (!isPublishedHostParameter(paramId))
            return false;
        return parameterTextForValue(paramId, value, display, size);
    }

    bool paramsTextToValue(clap_id paramId,
                           const char *display,
                           double *value) noexcept override {
        if (!value || !isPublishedHostParameter(paramId))
            return false;
        return parameterValueFromText(paramId, display, *value);
    }

    void paramsFlush(const clap_input_events_t *in,
                     const clap_output_events_t *) noexcept override {
        if (!in || !in->size || !in->get)
            return;

        if (active_) {
            if (!applyPendingParameterSnapshotToEngine())
                return;
        } else if (!materializePendingParameterSnapshotOnMainThread()) {
            return;
        }

        const auto count = in->size(in);
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto *header = in->get(in, index);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
                header->size < sizeof(clap_event_header_t))
                continue;

            if (header->type == CLAP_EVENT_PARAM_VALUE) {
                if (header->size < sizeof(clap_event_param_value_t))
                    continue;
                const auto &event =
                    *reinterpret_cast<const clap_event_param_value_t *>(header);
                const auto *spec = parameterSpecForId(event.param_id);
                if (!spec || !isPublishedHostParameter(event.param_id) ||
                    !std::isfinite(event.value) ||
                    event.value < spec->minValue || event.value > spec->maxValue)
                    continue;

                const bool isGlobal = isGlobalParameterAddress(event.note_id,
                                                                event.port_index,
                                                                event.channel,
                                                                event.key);
                if (active_) {
                    if (!engine_.applyParameterFlushEvent(*header))
                        continue;
                    if (isGlobal) {
                        storeGlobalHostParameterSnapshot(event.param_id, event.value);
                        if (event.param_id == kHostAmpReleaseParameterId)
                            (void)syncTailFromEngine(true);
                    }
                } else if (isGlobal) {
                    storeGlobalHostParameterSnapshot(event.param_id, event.value);
                }
                continue;
            }

            if (header->type == CLAP_EVENT_PARAM_MOD) {
                if (header->size < sizeof(clap_event_param_mod_t))
                    continue;
                const auto &event =
                    *reinterpret_cast<const clap_event_param_mod_t *>(header);
                if (!isPublishedHostParameter(event.param_id) ||
                    !parameterSpecForId(event.param_id) || !std::isfinite(event.amount))
                    continue;
                if (active_)
                    (void)engine_.applyParameterFlushEvent(*header);
            }
        }
    }

private:
    bool commitPersistentParameterSnapshot(const ParameterSnapshot &loaded,
                                           bool notifyHostParams) noexcept {
        ParameterSnapshot current{};
        const bool haveCurrent = readEffectiveParameterSnapshot(current);
        const bool parameterValueChanged =
            !haveCurrent || !parameterSnapshotsEqual(current, loaded);

        if (active_) {
            std::uint32_t loadedTailSamples = 0u;
            if (!releaseSecondsToTailSamples(loaded.ampReleaseSeconds,
                                             activeSampleRate_,
                                             loadedTailSamples))
                return false;
            const auto loadedRevisionBefore =
                loadedStateRevision_.load(std::memory_order_acquire);
            const auto tailRevision =
                tailLoadedStateRevisionPublished_.load(std::memory_order_acquire);
            if (loadedRevisionBefore != tailRevision) {
                loadedTailSamples = std::max(
                    loadedTailSamples,
                    pendingLoadedTailSamples_.load(std::memory_order_acquire));
            }
            pendingLoadedTailSamples_.store(loadedTailSamples,
                                            std::memory_order_release);
        }

        publishPendingParameterSnapshot(loaded);
        const auto loadedRevision =
            loadedStateRevision_.fetch_add(1u, std::memory_order_release) + 1u;

        if (!active_) {
            publishHostParameterSnapshot(loaded);
            appliedLoadedStateRevision_ = loadedRevision;
            appliedLoadedStateRevisionPublished_.store(loadedRevision,
                                                       std::memory_order_release);
        }

        if (notifyHostParams && parameterValueChanged && hostParams_ && hostParams_->rescan)
            hostParams_->rescan(host_, CLAP_PARAM_RESCAN_VALUES);
        return true;
    }

    [[nodiscard]] presets::PresetDocument capturePolySynthPresetDocument(
        presets::PresetMetadata metadata) const {
        ParameterSnapshot snapshot{};
        if (!readEffectiveParameterSnapshot(snapshot))
            return {};
        return capturePolySynthPreset(snapshot, metadata);
    }

    presets::PresetStateAdapterError applyPolySynthPresetDocument(
        const presets::PresetDocument &document) noexcept {
        const auto mapped = makePolySynthPresetCandidate(document);
        if (!mapped.ok())
            return mapped.error;
        if (!commitPersistentParameterSnapshot(*mapped.candidate, false))
            return presets::PresetStateAdapterError::InvalidKnownParameter;
        return presets::PresetStateAdapterError::None;
    }

    ParameterSnapshot loadHostParameterSnapshotRelaxed() const noexcept {
        ParameterSnapshot snapshot{};
        snapshot.fineTuneCents = hostFineTuneCents_.load(std::memory_order_relaxed);
        snapshot.masterGainDb = hostMasterGainDb_.load(std::memory_order_relaxed);
        snapshot.waveform = hostWaveform_.load(std::memory_order_relaxed);
        snapshot.coarseTuneSemitones =
            hostCoarseTuneSemitones_.load(std::memory_order_relaxed);
        snapshot.pan = hostPan_.load(std::memory_order_relaxed);
        snapshot.filterCutoffHz = hostFilterCutoffHz_.load(std::memory_order_relaxed);
        snapshot.filterResonance = hostFilterResonance_.load(std::memory_order_relaxed);
        snapshot.filterEnvelopeAmount =
            hostFilterEnvelopeAmount_.load(std::memory_order_relaxed);
        snapshot.ampLevel = hostAmpLevel_.load(std::memory_order_relaxed);
        snapshot.ampAttackSeconds = hostAmpAttackSeconds_.load(std::memory_order_relaxed);
        snapshot.ampDecaySeconds = hostAmpDecaySeconds_.load(std::memory_order_relaxed);
        snapshot.ampSustain = hostAmpSustain_.load(std::memory_order_relaxed);
        snapshot.ampReleaseSeconds = hostAmpReleaseSeconds_.load(std::memory_order_relaxed);
        return snapshot;
    }

    ParameterSnapshot loadPendingParameterSnapshotRelaxed() const noexcept {
        ParameterSnapshot snapshot{};
        snapshot.fineTuneCents = pendingLoadedFineTuneCents_.load(std::memory_order_relaxed);
        snapshot.masterGainDb = pendingLoadedMasterGainDb_.load(std::memory_order_relaxed);
        snapshot.waveform = pendingLoadedWaveform_.load(std::memory_order_relaxed);
        snapshot.coarseTuneSemitones =
            pendingLoadedCoarseTuneSemitones_.load(std::memory_order_relaxed);
        snapshot.pan = pendingLoadedPan_.load(std::memory_order_relaxed);
        snapshot.filterCutoffHz = pendingLoadedFilterCutoffHz_.load(std::memory_order_relaxed);
        snapshot.filterResonance = pendingLoadedFilterResonance_.load(std::memory_order_relaxed);
        snapshot.filterEnvelopeAmount =
            pendingLoadedFilterEnvelopeAmount_.load(std::memory_order_relaxed);
        snapshot.ampLevel = pendingLoadedAmpLevel_.load(std::memory_order_relaxed);
        snapshot.ampAttackSeconds =
            pendingLoadedAmpAttackSeconds_.load(std::memory_order_relaxed);
        snapshot.ampDecaySeconds =
            pendingLoadedAmpDecaySeconds_.load(std::memory_order_relaxed);
        snapshot.ampSustain = pendingLoadedAmpSustain_.load(std::memory_order_relaxed);
        snapshot.ampReleaseSeconds =
            pendingLoadedAmpReleaseSeconds_.load(std::memory_order_relaxed);
        return snapshot;
    }

    bool tryReadHostParameterSnapshot(ParameterSnapshot &snapshot) const noexcept {
        const auto before = hostSnapshotSequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0u)
            return false;
        const auto candidate = loadHostParameterSnapshotRelaxed();
        const auto after = hostSnapshotSequence_.load(std::memory_order_acquire);
        if (before != after || (after & 1u) != 0u)
            return false;
        snapshot = candidate;
        return true;
    }

    bool tryReadPendingParameterSnapshot(ParameterSnapshot &snapshot) const noexcept {
        const auto before = pendingSnapshotSequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0u)
            return false;
        const auto candidate = loadPendingParameterSnapshotRelaxed();
        const auto after = pendingSnapshotSequence_.load(std::memory_order_acquire);
        if (before != after || (after & 1u) != 0u)
            return false;
        snapshot = candidate;
        return true;
    }

    bool readHostParameterSnapshot(ParameterSnapshot &snapshot) const noexcept {
        while (!tryReadHostParameterSnapshot(snapshot)) {
        }
        return true;
    }

    bool readPendingParameterSnapshot(ParameterSnapshot &snapshot) const noexcept {
        while (!tryReadPendingParameterSnapshot(snapshot)) {
        }
        return true;
    }

    bool readEffectiveParameterSnapshot(ParameterSnapshot &snapshot,
                                        std::uint32_t *revision = nullptr) const noexcept {
        for (;;) {
            const auto loadedRevision = loadedStateRevision_.load(std::memory_order_acquire);
            const auto appliedRevision =
                appliedLoadedStateRevisionPublished_.load(std::memory_order_acquire);
            if (loadedRevision != appliedRevision) {
                if (!readPendingParameterSnapshot(snapshot))
                    continue;
                if (loadedRevision != loadedStateRevision_.load(std::memory_order_acquire))
                    continue;
                if (revision)
                    *revision = loadedRevision;
                return true;
            }

            if (!readHostParameterSnapshot(snapshot))
                continue;
            if (loadedRevision != loadedStateRevision_.load(std::memory_order_acquire))
                continue;
            if (revision)
                *revision = loadedRevision;
            return true;
        }
    }

    void publishHostParameterSnapshot(const ParameterSnapshot &snapshot) noexcept {
        hostSnapshotSequence_.fetch_add(1u, std::memory_order_acq_rel);
        hostFineTuneCents_.store(snapshot.fineTuneCents, std::memory_order_relaxed);
        hostMasterGainDb_.store(snapshot.masterGainDb, std::memory_order_relaxed);
        hostWaveform_.store(snapshot.waveform, std::memory_order_relaxed);
        hostCoarseTuneSemitones_.store(snapshot.coarseTuneSemitones,
                                       std::memory_order_relaxed);
        hostPan_.store(snapshot.pan, std::memory_order_relaxed);
        hostFilterCutoffHz_.store(snapshot.filterCutoffHz, std::memory_order_relaxed);
        hostFilterResonance_.store(snapshot.filterResonance, std::memory_order_relaxed);
        hostFilterEnvelopeAmount_.store(snapshot.filterEnvelopeAmount,
                                        std::memory_order_relaxed);
        hostAmpLevel_.store(snapshot.ampLevel, std::memory_order_relaxed);
        hostAmpAttackSeconds_.store(snapshot.ampAttackSeconds, std::memory_order_relaxed);
        hostAmpDecaySeconds_.store(snapshot.ampDecaySeconds, std::memory_order_relaxed);
        hostAmpSustain_.store(snapshot.ampSustain, std::memory_order_relaxed);
        hostAmpReleaseSeconds_.store(snapshot.ampReleaseSeconds, std::memory_order_relaxed);
        hostSnapshotSequence_.fetch_add(1u, std::memory_order_release);
    }

    void publishPendingParameterSnapshot(const ParameterSnapshot &snapshot) noexcept {
        pendingSnapshotSequence_.fetch_add(1u, std::memory_order_acq_rel);
        pendingLoadedFineTuneCents_.store(snapshot.fineTuneCents, std::memory_order_relaxed);
        pendingLoadedMasterGainDb_.store(snapshot.masterGainDb, std::memory_order_relaxed);
        pendingLoadedWaveform_.store(snapshot.waveform, std::memory_order_relaxed);
        pendingLoadedCoarseTuneSemitones_.store(snapshot.coarseTuneSemitones,
                                                std::memory_order_relaxed);
        pendingLoadedPan_.store(snapshot.pan, std::memory_order_relaxed);
        pendingLoadedFilterCutoffHz_.store(snapshot.filterCutoffHz, std::memory_order_relaxed);
        pendingLoadedFilterResonance_.store(snapshot.filterResonance,
                                            std::memory_order_relaxed);
        pendingLoadedFilterEnvelopeAmount_.store(snapshot.filterEnvelopeAmount,
                                                 std::memory_order_relaxed);
        pendingLoadedAmpLevel_.store(snapshot.ampLevel, std::memory_order_relaxed);
        pendingLoadedAmpAttackSeconds_.store(snapshot.ampAttackSeconds,
                                             std::memory_order_relaxed);
        pendingLoadedAmpDecaySeconds_.store(snapshot.ampDecaySeconds,
                                            std::memory_order_relaxed);
        pendingLoadedAmpSustain_.store(snapshot.ampSustain, std::memory_order_relaxed);
        pendingLoadedAmpReleaseSeconds_.store(snapshot.ampReleaseSeconds,
                                              std::memory_order_relaxed);
        pendingSnapshotSequence_.fetch_add(1u, std::memory_order_release);
    }

    void storeGlobalHostParameterSnapshot(clap_id paramId, double value) noexcept {
        hostSnapshotSequence_.fetch_add(1u, std::memory_order_acq_rel);
        if (paramId == kHostFineTuneParameterId) {
            hostFineTuneCents_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostWaveformParameterId) {
            hostWaveform_.store(static_cast<std::uint32_t>(value), std::memory_order_relaxed);
        } else if (paramId == kHostCoarseTuneParameterId) {
            hostCoarseTuneSemitones_.store(static_cast<std::int32_t>(value),
                                           std::memory_order_relaxed);
        } else if (paramId == kHostPanParameterId) {
            hostPan_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostFilterCutoffParameterId) {
            hostFilterCutoffHz_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostFilterResonanceParameterId) {
            hostFilterResonance_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostFilterEnvelopeAmountParameterId) {
            hostFilterEnvelopeAmount_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostAmpLevelParameterId) {
            hostAmpLevel_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostAmpAttackParameterId) {
            hostAmpAttackSeconds_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostAmpDecayParameterId) {
            hostAmpDecaySeconds_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostAmpSustainParameterId) {
            hostAmpSustain_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostAmpReleaseParameterId) {
            hostAmpReleaseSeconds_.store(static_cast<float>(value), std::memory_order_relaxed);
        } else if (paramId == kHostMasterGainParameterId) {
            hostMasterGainDb_.store(static_cast<float>(value), std::memory_order_relaxed);
        }
        hostSnapshotSequence_.fetch_add(1u, std::memory_order_release);
    }

    bool applyRetainedParameterBaseToEngine(clap_id paramId, double value) noexcept {
        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = paramId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        return engine_.applyParameterFlushEvent(event.header);
    }

    bool applyParameterSnapshotToEngine(const ParameterSnapshot &snapshot) noexcept {
        return applyRetainedParameterBaseToEngine(
                   kHostFineTuneParameterId, snapshot.fineTuneCents) &&
               applyRetainedParameterBaseToEngine(
                   kHostMasterGainParameterId, snapshot.masterGainDb) &&
               applyRetainedParameterBaseToEngine(
                   kHostWaveformParameterId, snapshot.waveform) &&
               applyRetainedParameterBaseToEngine(
                   kHostCoarseTuneParameterId, snapshot.coarseTuneSemitones) &&
               applyRetainedParameterBaseToEngine(kHostPanParameterId, snapshot.pan) &&
               applyRetainedParameterBaseToEngine(
                   kHostFilterCutoffParameterId, snapshot.filterCutoffHz) &&
               applyRetainedParameterBaseToEngine(
                   kHostFilterResonanceParameterId, snapshot.filterResonance) &&
               applyRetainedParameterBaseToEngine(
                   kHostFilterEnvelopeAmountParameterId, snapshot.filterEnvelopeAmount) &&
               applyRetainedParameterBaseToEngine(
                   kHostAmpLevelParameterId, snapshot.ampLevel) &&
               applyRetainedParameterBaseToEngine(
                   kHostAmpAttackParameterId, snapshot.ampAttackSeconds) &&
               applyRetainedParameterBaseToEngine(
                   kHostAmpDecayParameterId, snapshot.ampDecaySeconds) &&
               applyRetainedParameterBaseToEngine(
                   kHostAmpSustainParameterId, snapshot.ampSustain) &&
               applyRetainedParameterBaseToEngine(
                   kHostAmpReleaseParameterId, snapshot.ampReleaseSeconds);
    }

    bool applyPendingParameterSnapshotToEngine() noexcept {
        const auto loadedRevision = loadedStateRevision_.load(std::memory_order_acquire);
        if (loadedRevision == appliedLoadedStateRevision_)
            return true;

        ParameterSnapshot pending{};
        if (!tryReadPendingParameterSnapshot(pending) ||
            loadedRevision != loadedStateRevision_.load(std::memory_order_acquire))
            return true;
        if (!applyParameterSnapshotToEngine(pending))
            return false;

        appliedLoadedStateRevision_ = loadedRevision;
        if (!syncParameterSnapshotsPreservingConcurrentStateLoad())
            return false;
        return syncTailFromEngine(true);
    }

    bool materializePendingParameterSnapshotOnMainThread() noexcept {
        const auto loadedRevision = loadedStateRevision_.load(std::memory_order_acquire);
        const auto appliedRevision =
            appliedLoadedStateRevisionPublished_.load(std::memory_order_acquire);
        if (loadedRevision == appliedRevision) {
            appliedLoadedStateRevision_ = loadedRevision;
            return true;
        }

        ParameterSnapshot pending{};
        if (!readPendingParameterSnapshot(pending))
            return false;
        if (loadedRevision != loadedStateRevision_.load(std::memory_order_acquire))
            return materializePendingParameterSnapshotOnMainThread();

        publishHostParameterSnapshot(pending);
        appliedLoadedStateRevision_ = loadedRevision;
        appliedLoadedStateRevisionPublished_.store(loadedRevision,
                                                   std::memory_order_release);
        return true;
    }

    bool syncTailFromEngine(bool notifyHost) noexcept {
        double releaseSeconds = 0.0;
        std::uint32_t defaultTailSamples = 0;
        if (!engine_.parameterBaseValue(kHostAmpReleaseParameterId, releaseSeconds) ||
            !releaseSecondsToTailSamples(releaseSeconds,
                                         activeSampleRate_,
                                         defaultTailSamples))
            return false;

        // Release is a NOTE_ON snapshot. The exact advertised tail must cover the
        // current default for future generations and every release snapshot that
        // is still owned by an active generation. VoiceEngine performs a bounded
        // fixed-capacity scan on this same serialized audio-thread path.
        const auto tailSamples = std::max(defaultTailSamples,
                                          engine_.maximumActiveReleaseSamples());
        const auto previous = currentTailSamples_.load(std::memory_order_acquire);
        const bool changed = tailSamples != previous;
        if (changed)
            currentTailSamples_.store(tailSamples, std::memory_order_release);
        tailLoadedStateRevisionPublished_.store(appliedLoadedStateRevision_,
                                                std::memory_order_release);
        if (changed && notifyHost && hostTail_ && hostTail_->changed)
            hostTail_->changed(host_);
        return true;
    }

    bool snapshotFromEngine(ParameterSnapshot &snapshot) noexcept {
        double fineTune = 0.0;
        double masterGain = 0.0;
        double waveform = 0.0;
        double coarseTune = 0.0;
        double pan = 0.0;
        double filterCutoff = 0.0;
        double filterResonance = 0.0;
        double filterEnvelopeAmount = 0.0;
        double ampLevel = 1.0;
        double ampAttackSeconds = 0.01;
        double ampDecaySeconds = 0.1;
        double ampSustain = 0.8;
        double ampReleaseSeconds = 0.25;
        if (!engine_.parameterBaseValue(kHostFineTuneParameterId, fineTune) ||
            !engine_.parameterBaseValue(kHostMasterGainParameterId, masterGain) ||
            !engine_.parameterBaseValue(kHostWaveformParameterId, waveform) ||
            !engine_.parameterBaseValue(kHostCoarseTuneParameterId, coarseTune) ||
            !engine_.parameterBaseValue(kHostPanParameterId, pan) ||
            !engine_.parameterBaseValue(kHostFilterCutoffParameterId, filterCutoff) ||
            !engine_.parameterBaseValue(kHostFilterResonanceParameterId, filterResonance) ||
            !engine_.parameterBaseValue(kHostFilterEnvelopeAmountParameterId,
                                        filterEnvelopeAmount) ||
            !engine_.parameterBaseValue(kHostAmpLevelParameterId, ampLevel) ||
            !engine_.parameterBaseValue(kHostAmpAttackParameterId, ampAttackSeconds) ||
            !engine_.parameterBaseValue(kHostAmpDecayParameterId, ampDecaySeconds) ||
            !engine_.parameterBaseValue(kHostAmpSustainParameterId, ampSustain) ||
            !engine_.parameterBaseValue(kHostAmpReleaseParameterId, ampReleaseSeconds))
            return false;

        snapshot.fineTuneCents = static_cast<float>(fineTune);
        snapshot.masterGainDb = static_cast<float>(masterGain);
        snapshot.waveform = static_cast<std::uint32_t>(waveform);
        snapshot.coarseTuneSemitones = static_cast<std::int32_t>(coarseTune);
        snapshot.pan = static_cast<float>(pan);
        snapshot.filterCutoffHz = static_cast<float>(filterCutoff);
        snapshot.filterResonance = static_cast<float>(filterResonance);
        snapshot.filterEnvelopeAmount = static_cast<float>(filterEnvelopeAmount);
        snapshot.ampLevel = static_cast<float>(ampLevel);
        snapshot.ampAttackSeconds = static_cast<float>(ampAttackSeconds);
        snapshot.ampDecaySeconds = static_cast<float>(ampDecaySeconds);
        snapshot.ampSustain = static_cast<float>(ampSustain);
        snapshot.ampReleaseSeconds = static_cast<float>(ampReleaseSeconds);
        return true;
    }

    bool syncParameterSnapshotsPreservingConcurrentStateLoad() noexcept {
        ParameterSnapshot snapshot{};
        if (!snapshotFromEngine(snapshot))
            return false;
        publishHostParameterSnapshot(snapshot);
        appliedLoadedStateRevisionPublished_.store(appliedLoadedStateRevision_,
                                                   std::memory_order_release);
        return true;
    }

    const clap_host_t *host_ = nullptr;
    const clap_host_params_t *hostParams_ = nullptr;
    const clap_host_tail_t *hostTail_ = nullptr;
    ParameterVoiceEngine engine_{};
    std::atomic<std::uint32_t> hostSnapshotSequence_{0u};
    std::atomic<float> hostFineTuneCents_{0.0f};
    std::atomic<float> hostMasterGainDb_{0.0f};
    std::atomic<std::uint32_t> hostWaveform_{0u};
    std::atomic<std::int32_t> hostCoarseTuneSemitones_{0};
    std::atomic<float> hostPan_{0.0f};
    std::atomic<float> hostFilterCutoffHz_{6000.0f};
    std::atomic<float> hostFilterResonance_{0.0f};
    std::atomic<float> hostFilterEnvelopeAmount_{0.0f};
    std::atomic<float> hostAmpLevel_{1.0f};
    std::atomic<float> hostAmpAttackSeconds_{0.01f};
    std::atomic<float> hostAmpDecaySeconds_{0.1f};
    std::atomic<float> hostAmpSustain_{0.8f};
    std::atomic<float> hostAmpReleaseSeconds_{0.25f};
    std::atomic<std::uint32_t> pendingSnapshotSequence_{0u};
    std::atomic<float> pendingLoadedFineTuneCents_{0.0f};
    std::atomic<float> pendingLoadedMasterGainDb_{0.0f};
    std::atomic<std::uint32_t> pendingLoadedWaveform_{0u};
    std::atomic<std::int32_t> pendingLoadedCoarseTuneSemitones_{0};
    std::atomic<float> pendingLoadedPan_{0.0f};
    std::atomic<float> pendingLoadedFilterCutoffHz_{6000.0f};
    std::atomic<float> pendingLoadedFilterResonance_{0.0f};
    std::atomic<float> pendingLoadedFilterEnvelopeAmount_{0.0f};
    std::atomic<float> pendingLoadedAmpLevel_{1.0f};
    std::atomic<float> pendingLoadedAmpAttackSeconds_{0.01f};
    std::atomic<float> pendingLoadedAmpDecaySeconds_{0.1f};
    std::atomic<float> pendingLoadedAmpSustain_{0.8f};
    std::atomic<float> pendingLoadedAmpReleaseSeconds_{0.25f};
    std::atomic<std::uint32_t> currentTailSamples_{kPolySynthBootstrapReleaseSamples};
    std::atomic<std::uint32_t> pendingLoadedTailSamples_{kPolySynthBootstrapReleaseSamples};
    std::atomic<std::uint32_t> tailLoadedStateRevisionPublished_{0u};
    std::atomic<std::uint32_t> loadedStateRevision_{0u};
    std::atomic<std::uint32_t> appliedLoadedStateRevisionPublished_{0u};
    double activeSampleRate_ = 0.0;
    std::uint32_t appliedLoadedStateRevision_ = 0u;
    bool active_ = false;
};

std::uint32_t CLAP_ABI factoryGetPluginCount(const clap_plugin_factory_t *) { return 1u; }

const clap_plugin_descriptor_t *CLAP_ABI factoryGetPluginDescriptor(
    const clap_plugin_factory_t *, std::uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t *CLAP_ABI factoryCreatePlugin(
    const clap_plugin_factory_t *, const clap_host_t *host, const char *pluginId) {
    if (!host || !pluginId || !clap_version_is_compatible(host->clap_version) ||
        std::strcmp(pluginId, kPolySynthPluginId) != 0)
        return nullptr;
    auto *instance = new (std::nothrow) PolySynthPlugin(host);
    return instance ? instance->clapPlugin() : nullptr;
}

const clap_plugin_factory_t kFactory{
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

} // namespace

const clap_plugin_factory_t *polysynthFactory() noexcept { return &kFactory; }

} // namespace webview_gui::examples::polysynth
