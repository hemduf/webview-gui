#include "polysynth_plugin.h"
#include "polysynth_parameter_voice_engine.h"

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
constexpr clap_id kHostPanParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::Pan);
constexpr std::array<clap_id, 5> kPublishedHostParameterIds{{
    kHostFineTuneParameterId,
    kHostMasterGainParameterId,
    kHostWaveformParameterId,
    kHostCoarseTuneParameterId,
    kHostPanParameterId,
}};
constexpr clap_id kTuningRemoteControlsPageId = 0x3200u;
constexpr clap_id kPerformanceRemoteControlsPageId = 0x3201u;
constexpr std::uint32_t kPolySynthReleaseTailSamples = 64u;

static_assert(std::atomic<float>::is_always_lock_free,
              "PolySynth requires lock-free host-visible float parameter snapshots");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "PolySynth state handoff revision must be lock-free");
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
                  kTuningRemoteControlsPageId != kPerformanceRemoteControlsPageId,
              "PolySynth remote-controls page IDs must be valid and unique");
static_assert(kPolySynthReleaseTailSamples <
                  static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
              "PolySynth tail must remain finite under the CLAP tail contract");

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
constexpr std::uint32_t kStateVersion = 5u;
constexpr std::size_t kLegacyStateSize = 24u;
constexpr std::size_t kStateV3Size = 28u;
constexpr std::size_t kStateV4Size = 32u;
constexpr std::size_t kStateSize = 36u;

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
                                                  float pan) noexcept {
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
    return bytes;
}

bool decodeState(const std::array<std::uint8_t, kStateSize> &bytes,
                 std::size_t encodedSize,
                 double &fineTuneCents,
                 float &masterGainDb,
                 std::uint32_t &waveform,
                 std::int32_t &coarseTuneSemitones,
                 float &pan) noexcept {
    if (!std::equal(kStateMagic.begin(), kStateMagic.end(), bytes.begin()))
        return false;

    const auto version = loadU32Le(bytes.data() + 8);
    if (version < 1u || version > kStateVersion)
        return false;
    const auto expectedSize = version < 3u ? kLegacyStateSize
                              : version == 3u ? kStateV3Size
                              : version == 4u ? kStateV4Size
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
    return panSpec && std::isfinite(pan) &&
           static_cast<double>(pan) >= panSpec->minValue &&
           static_cast<double>(pan) <= panSpec->maxValue;
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
                 float pan) noexcept {
        hostInput = newHostInput;
        hostCount = 0;
        valid = true;

        prepareEvent(events[0], kHostFineTuneParameterId, fineTuneCents);
        prepareEvent(events[1], kHostMasterGainParameterId, masterGainDb);
        prepareEvent(events[2], kHostWaveformParameterId, waveform);
        prepareEvent(events[3], kHostCoarseTuneParameterId, coarseTuneSemitones);
        prepareEvent(events[4], kHostPanParameterId, pan);

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
    std::array<clap_event_param_value_t, 5> events{};
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
        }
        return true;
    }

    bool activate(double sampleRate,
                  std::uint32_t minFrameCount,
                  std::uint32_t maxFrameCount) noexcept override {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || minFrameCount == 0 ||
            maxFrameCount < minFrameCount)
            return false;
        if (!engine_.configure(kPolySynthDefaultVoiceCount,
                               sampleRate,
                               kPolySynthReleaseTailSamples) ||
            !engine_.setFineTuningCents(
                hostFineTuneCents_.load(std::memory_order_acquire)) ||
            !applyRetainedMasterGainBaseToEngine() ||
            !applyRetainedWaveformBaseToEngine() ||
            !applyRetainedCoarseTuneBaseToEngine() ||
            !applyRetainedPanBaseToEngine())
            return false;

        appliedLoadedStateRevision_ = loadedStateRevision_.load(std::memory_order_acquire);
        active_ = true;
        return true;
    }

    void deactivate() noexcept override {
        active_ = false;
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
        const bool replayLoadedState = loadedRevision != appliedLoadedStateRevision_;
        const clap_input_events_t *inputEvents = processData->in_events;
        PendingParameterStateInput pendingInput;
        if (replayLoadedState) {
            if (!pendingInput.prepare(
                    processData->in_events,
                    hostFineTuneCents_.load(std::memory_order_acquire),
                    hostMasterGainDb_.load(std::memory_order_acquire),
                    hostWaveform_.load(std::memory_order_acquire),
                    hostCoarseTuneSemitones_.load(std::memory_order_acquire),
                    hostPan_.load(std::memory_order_acquire)))
                return CLAP_PROCESS_ERROR;
            inputEvents = &pendingInput.input;
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

        if (!syncParameterSnapshotsPreservingConcurrentStateLoad())
            return CLAP_PROCESS_ERROR;

        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsTail() const noexcept override { return true; }

    std::uint32_t tailGet() const noexcept override {
        return kPolySynthReleaseTailSamples;
    }

    bool implementsState() const noexcept override { return true; }

    bool stateSave(const clap_ostream_t *stream) noexcept override {
        const auto bytes = encodeState(
            static_cast<double>(hostFineTuneCents_.load(std::memory_order_relaxed)),
            hostMasterGainDb_.load(std::memory_order_relaxed),
            hostWaveform_.load(std::memory_order_relaxed),
            hostCoarseTuneSemitones_.load(std::memory_order_relaxed),
            hostPan_.load(std::memory_order_relaxed));
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
                         kStateSize - kStateV4Size))
                return false;
            encodedSize = kStateSize;
        }

        std::uint8_t trailing = 0;
        const auto trailingRead = stream->read(stream, &trailing, 1);
        if (trailingRead != 0)
            return false;

        double fineTuneCents = 0.0;
        float masterGainDb = 0.0f;
        std::uint32_t waveform = 0u;
        std::int32_t coarseTuneSemitones = 0;
        float pan = 0.0f;
        if (!decodeState(bytes,
                         encodedSize,
                         fineTuneCents,
                         masterGainDb,
                         waveform,
                         coarseTuneSemitones,
                         pan))
            return false;

        const auto fineTune = static_cast<float>(fineTuneCents);
        const bool parameterValueChanged =
            hostFineTuneCents_.load(std::memory_order_relaxed) != fineTune ||
            hostMasterGainDb_.load(std::memory_order_relaxed) != masterGainDb ||
            hostWaveform_.load(std::memory_order_relaxed) != waveform ||
            hostCoarseTuneSemitones_.load(std::memory_order_relaxed) != coarseTuneSemitones ||
            hostPan_.load(std::memory_order_relaxed) != pan;

        pendingLoadedFineTuneCents_.store(fineTune, std::memory_order_relaxed);
        pendingLoadedMasterGainDb_.store(masterGainDb, std::memory_order_relaxed);
        pendingLoadedWaveform_.store(waveform, std::memory_order_relaxed);
        pendingLoadedCoarseTuneSemitones_.store(coarseTuneSemitones,
                                                std::memory_order_relaxed);
        pendingLoadedPan_.store(pan, std::memory_order_relaxed);
        hostFineTuneCents_.store(fineTune, std::memory_order_relaxed);
        hostMasterGainDb_.store(masterGainDb, std::memory_order_relaxed);
        hostWaveform_.store(waveform, std::memory_order_relaxed);
        hostCoarseTuneSemitones_.store(coarseTuneSemitones, std::memory_order_relaxed);
        hostPan_.store(pan, std::memory_order_relaxed);
        loadedStateRevision_.fetch_add(1u, std::memory_order_release);

        if (parameterValueChanged && hostParams_ && hostParams_->rescan)
            hostParams_->rescan(host_, CLAP_PARAM_RESCAN_VALUES);
        return true;
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

    std::uint32_t remoteControlsPageCount() noexcept override { return 2u; }

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

        page->page_id = kPerformanceRemoteControlsPageId;
        page->param_ids[0] = kHostMasterGainParameterId;
        page->param_ids[1] = kHostPanParameterId;
        return copyName(page->section_name, sizeof(page->section_name), "Output") &&
               copyName(page->page_name, sizeof(page->page_name), "Performance");
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
        if (paramId == kHostFineTuneParameterId) {
            *value = static_cast<double>(hostFineTuneCents_.load(std::memory_order_acquire));
            return true;
        }
        if (paramId == kHostWaveformParameterId) {
            *value = static_cast<double>(hostWaveform_.load(std::memory_order_acquire));
            return true;
        }
        if (paramId == kHostCoarseTuneParameterId) {
            *value = static_cast<double>(hostCoarseTuneSemitones_.load(std::memory_order_acquire));
            return true;
        }
        if (paramId == kHostPanParameterId) {
            *value = static_cast<double>(hostPan_.load(std::memory_order_acquire));
            return true;
        }
        *value = static_cast<double>(hostMasterGainDb_.load(std::memory_order_acquire));
        return true;
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
                    if (isGlobal)
                        storeGlobalHostParameterSnapshot(event.param_id, event.value);
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
    void storeGlobalHostParameterSnapshot(clap_id paramId, double value) noexcept {
        if (paramId == kHostFineTuneParameterId) {
            hostFineTuneCents_.store(static_cast<float>(value), std::memory_order_release);
        } else if (paramId == kHostWaveformParameterId) {
            hostWaveform_.store(static_cast<std::uint32_t>(value), std::memory_order_release);
        } else if (paramId == kHostCoarseTuneParameterId) {
            hostCoarseTuneSemitones_.store(static_cast<std::int32_t>(value),
                                           std::memory_order_release);
        } else if (paramId == kHostPanParameterId) {
            hostPan_.store(static_cast<float>(value), std::memory_order_release);
        } else {
            hostMasterGainDb_.store(static_cast<float>(value), std::memory_order_release);
        }
    }

    bool applyRetainedMasterGainBaseToEngine() noexcept {
        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kHostMasterGainParameterId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = static_cast<double>(hostMasterGainDb_.load(std::memory_order_acquire));
        return engine_.applyParameterFlushEvent(event.header);
    }

    bool applyRetainedWaveformBaseToEngine() noexcept {
        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kHostWaveformParameterId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = static_cast<double>(hostWaveform_.load(std::memory_order_acquire));
        return engine_.applyParameterFlushEvent(event.header);
    }

    bool applyRetainedCoarseTuneBaseToEngine() noexcept {
        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kHostCoarseTuneParameterId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = static_cast<double>(
            hostCoarseTuneSemitones_.load(std::memory_order_acquire));
        return engine_.applyParameterFlushEvent(event.header);
    }

    bool applyRetainedPanBaseToEngine() noexcept {
        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kHostPanParameterId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = static_cast<double>(hostPan_.load(std::memory_order_acquire));
        return engine_.applyParameterFlushEvent(event.header);
    }

    bool syncParameterSnapshotsPreservingConcurrentStateLoad() noexcept {
        const auto revisionBefore = loadedStateRevision_.load(std::memory_order_acquire);
        if (revisionBefore != appliedLoadedStateRevision_) {
            hostFineTuneCents_.store(
                pendingLoadedFineTuneCents_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            hostMasterGainDb_.store(
                pendingLoadedMasterGainDb_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            hostWaveform_.store(
                pendingLoadedWaveform_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            hostCoarseTuneSemitones_.store(
                pendingLoadedCoarseTuneSemitones_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            hostPan_.store(pendingLoadedPan_.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
            return true;
        }

        double fineTune = 0.0;
        double masterGain = 0.0;
        double waveform = 0.0;
        double coarseTune = 0.0;
        double pan = 0.0;
        if (!engine_.parameterBaseValue(kHostFineTuneParameterId, fineTune) ||
            !engine_.parameterBaseValue(kHostMasterGainParameterId, masterGain) ||
            !engine_.parameterBaseValue(kHostWaveformParameterId, waveform) ||
            !engine_.parameterBaseValue(kHostCoarseTuneParameterId, coarseTune) ||
            !engine_.parameterBaseValue(kHostPanParameterId, pan))
            return false;
        hostFineTuneCents_.store(static_cast<float>(fineTune), std::memory_order_relaxed);
        hostMasterGainDb_.store(static_cast<float>(masterGain), std::memory_order_relaxed);
        hostWaveform_.store(static_cast<std::uint32_t>(waveform), std::memory_order_relaxed);
        hostCoarseTuneSemitones_.store(static_cast<std::int32_t>(coarseTune),
                                      std::memory_order_relaxed);
        hostPan_.store(static_cast<float>(pan), std::memory_order_relaxed);

        const auto revisionAfter = loadedStateRevision_.load(std::memory_order_acquire);
        if (revisionAfter != revisionBefore) {
            hostFineTuneCents_.store(
                pendingLoadedFineTuneCents_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            hostMasterGainDb_.store(
                pendingLoadedMasterGainDb_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            hostWaveform_.store(
                pendingLoadedWaveform_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            hostCoarseTuneSemitones_.store(
                pendingLoadedCoarseTuneSemitones_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            hostPan_.store(pendingLoadedPan_.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
        }
        return true;
    }

    const clap_host_t *host_ = nullptr;
    const clap_host_params_t *hostParams_ = nullptr;
    ParameterVoiceEngine engine_{};
    std::atomic<float> hostFineTuneCents_{0.0f};
    std::atomic<float> hostMasterGainDb_{0.0f};
    std::atomic<std::uint32_t> hostWaveform_{0u};
    std::atomic<std::int32_t> hostCoarseTuneSemitones_{0};
    std::atomic<float> hostPan_{0.0f};
    std::atomic<float> pendingLoadedFineTuneCents_{0.0f};
    std::atomic<float> pendingLoadedMasterGainDb_{0.0f};
    std::atomic<std::uint32_t> pendingLoadedWaveform_{0u};
    std::atomic<std::int32_t> pendingLoadedCoarseTuneSemitones_{0};
    std::atomic<float> pendingLoadedPan_{0.0f};
    std::atomic<std::uint32_t> loadedStateRevision_{0u};
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
