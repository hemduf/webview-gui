#include "polysynth_plugin.h"
#include "polysynth_parameter_voice_engine.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <atomic>
#include <charconv>
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

constexpr clap_id kHostFineTuneParameterId =
    kFirstParameterId + static_cast<clap_id>(ParameterSlot::FineTuning);

static_assert(std::atomic<float>::is_always_lock_free,
              "PolySynth requires a lock-free host-visible parameter snapshot");
static_assert(std::atomic<bool>::is_always_lock_free,
              "PolySynth requires a lock-free pending parameter handoff");

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

bool copyName(char *destination, std::size_t capacity, const char *text) noexcept {
    if (!destination || capacity == 0 || !text)
        return false;
    const int written = std::snprintf(destination, capacity, "%s", text);
    return written >= 0 && static_cast<std::size_t>(written) < capacity;
}

bool isGlobalParameterAddress(const clap_event_param_value_t &event) noexcept {
    return event.note_id == -1 && event.port_index == -1 &&
           event.channel == -1 && event.key == -1;
}

struct NoteEndOutputSink {
    const clap_output_events_t *events = nullptr;
    bool ok = true;

    void operator()(const clap_event_note_t &event) noexcept {
        if (!events || !events->try_push || !events->try_push(events, &event.header))
            ok = false;
    }
};

// params.flush() has no sample offsets, but when the plug-in is activated a
// flushed base value must still reach already-running voices before rendering
// resumes. Prepend one fixed sample-zero PARAM_VALUE to the next process block;
// host-provided sample-zero events remain later in the stream and therefore win.
struct PendingFineTuneInput {
    PendingFineTuneInput() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool prepare(const clap_input_events_t *newHostInput, float value) noexcept {
        hostInput = newHostInput;
        hostCount = 0;
        valid = true;

        event = {};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kHostFineTuneParameterId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = static_cast<double>(value);

        if (hostInput && hostInput->size && hostInput->get)
            hostCount = hostInput->size(hostInput);
        else if (hostInput)
            valid = false;

        if (hostCount == std::numeric_limits<std::uint32_t>::max())
            valid = false;
        return valid;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        if (!events || !events->ctx)
            return 0;
        const auto &self = *static_cast<const PendingFineTuneInput *>(events->ctx);
        return self.valid ? self.hostCount + 1u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx)
            return nullptr;
        const auto &self = *static_cast<const PendingFineTuneInput *>(events->ctx);
        if (!self.valid)
            return nullptr;
        if (index == 0)
            return &self.event.header;
        if (!self.hostInput || index > self.hostCount)
            return nullptr;
        return self.hostInput->get(self.hostInput, index - 1u);
    }

    const clap_input_events_t *hostInput = nullptr;
    clap_event_param_value_t event{};
    clap_input_events_t input{};
    std::uint32_t hostCount = 0;
    bool valid = false;
};

class PolySynthPlugin final : public PolySynthBase {
public:
    explicit PolySynthPlugin(const clap_host_t *host)
        : PolySynthBase(&kDescriptor, host) {}

    ~PolySynthPlugin() override = default;

protected:
    bool init() noexcept override { return true; }

    bool activate(double sampleRate,
                  std::uint32_t minFrameCount,
                  std::uint32_t maxFrameCount) noexcept override {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || minFrameCount == 0 ||
            maxFrameCount < minFrameCount)
            return false;
        if (!engine_.configure(kPolySynthDefaultVoiceCount, sampleRate, 64u) ||
            !engine_.setFineTuningCents(
                hostFineTuneCents_.load(std::memory_order_acquire)))
            return false;

        // configure() starts with no active generations, so the retained base
        // value is already authoritative and no process-time replay is needed.
        pendingFineTuneFlush_.store(false, std::memory_order_release);
        return true;
    }

    void deactivate() noexcept override { engine_.reset(); }

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

        // The port does not advertise CLAP_AUDIO_PORT_SUPPORTS_64BITS, so the
        // host must provide the mandatory 32-bit format. PolySynth always writes
        // every frame, hence neither channel can be advertised as constant.
        output.constant_mask = 0;

        const bool replayFlushedValue =
            pendingFineTuneFlush_.load(std::memory_order_acquire);
        const clap_input_events_t *inputEvents = processData->in_events;
        PendingFineTuneInput pendingInput;
        if (replayFlushedValue) {
            if (!pendingInput.prepare(
                    processData->in_events,
                    hostFineTuneCents_.load(std::memory_order_acquire)))
                return CLAP_PROCESS_ERROR;
            inputEvents = &pendingInput.input;
        }

        NoteEndOutputSink noteEndSink{processData->out_events};
        const bool engineOk = engine_.process(inputEvents,
                                              processData->frames_count,
                                              output.data32[0],
                                              output.data32[1],
                                              noteEndSink);
        if (engineOk && replayFlushedValue)
            pendingFineTuneFlush_.store(false, std::memory_order_release);
        if (!engineOk || !noteEndSink.ok)
            return CLAP_PROCESS_ERROR;

        // Publish a lock-free block-boundary snapshot for main-thread get_value().
        // The DSP/event adapter remains the authority for sample-accurate changes;
        // this mirror never feeds the audio path and therefore cannot quantize it.
        double fineTune = 0.0;
        if (!engine_.parameterBaseValue(kHostFineTuneParameterId, fineTune))
            return CLAP_PROCESS_ERROR;
        hostFineTuneCents_.store(static_cast<float>(fineTune), std::memory_order_release);

        return CLAP_PROCESS_CONTINUE;
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

    bool implementsParams() const noexcept override { return true; }

    std::uint32_t paramsCount() const noexcept override { return 1u; }

    bool paramsInfo(std::uint32_t paramIndex,
                    clap_param_info_t *info) const noexcept override {
        if (!info || paramIndex != 0)
            return false;

        const auto *spec = parameterSpecForId(kHostFineTuneParameterId);
        if (!spec)
            return false;

        *info = {};
        info->id = spec->id;
        // The process adapter already supports polyphonic value/modulation
        // addressing, but this first plugin-facing increment deliberately
        // advertises base automation only. The per-note/modulation flags are
        // enabled only once params.flush has an equivalent non-process handoff.
        info->flags = kBaseAutomatableFlags;
        info->cookie = nullptr;
        info->min_value = spec->minValue;
        info->max_value = spec->maxValue;
        info->default_value = spec->defaultValue;
        return copyName(info->name, sizeof(info->name), spec->name) &&
               copyName(info->module, sizeof(info->module), spec->module);
    }

    bool paramsValue(clap_id paramId, double *value) noexcept override {
        if (!value || paramId != kHostFineTuneParameterId)
            return false;
        *value = static_cast<double>(
            hostFineTuneCents_.load(std::memory_order_acquire));
        return true;
    }

    bool paramsValueToText(clap_id paramId,
                           double value,
                           char *display,
                           std::uint32_t size) noexcept override {
        const auto *spec = parameterSpecForId(paramId);
        if (!spec || paramId != kHostFineTuneParameterId || !display || size == 0 ||
            !std::isfinite(value) || value < spec->minValue || value > spec->maxValue)
            return false;

        auto result = std::to_chars(display,
                                    display + size - 1,
                                    value,
                                    std::chars_format::general,
                                    8);
        if (result.ec != std::errc())
            return false;
        *result.ptr = '\0';
        return true;
    }

    bool paramsTextToValue(clap_id paramId,
                           const char *display,
                           double *value) noexcept override {
        const auto *spec = parameterSpecForId(paramId);
        if (!spec || paramId != kHostFineTuneParameterId || !display || !value)
            return false;

        const char *end = display + std::strlen(display);
        double parsed = 0.0;
        const auto result = std::from_chars(display, end, parsed, std::chars_format::general);
        if (result.ec != std::errc() || result.ptr != end || !std::isfinite(parsed) ||
            parsed < spec->minValue || parsed > spec->maxValue)
            return false;
        *value = parsed;
        return true;
    }

    void paramsFlush(const clap_input_events_t *in,
                     const clap_output_events_t *) noexcept override {
        if (!in || !in->size || !in->get)
            return;

        const auto count = in->size(in);
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto *header = in->get(in, index);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
                header->type != CLAP_EVENT_PARAM_VALUE ||
                header->size < sizeof(clap_event_param_value_t))
                continue;

            const auto &event =
                *reinterpret_cast<const clap_event_param_value_t *>(header);
            const auto *spec = parameterSpecForId(event.param_id);
            if (!spec || event.param_id != kHostFineTuneParameterId ||
                !isGlobalParameterAddress(event) || !std::isfinite(event.value) ||
                event.value < spec->minValue || event.value > spec->maxValue)
                continue;

            const auto fineTune = static_cast<float>(event.value);
            hostFineTuneCents_.store(fineTune, std::memory_order_release);

            // flush() is never concurrent with process(). The default setter is
            // enough before activation, but active generations need the event
            // adapter's voice-local update. Queue one fixed sample-zero replay
            // for the next process call; any host sample-zero event comes later
            // and overrides this retained flush value deterministically.
            (void)engine_.setFineTuningCents(fineTune);
            pendingFineTuneFlush_.store(true, std::memory_order_release);
        }
    }

private:
    ParameterVoiceEngine engine_{};
    std::atomic<float> hostFineTuneCents_{0.0f};
    std::atomic<bool> pendingFineTuneFlush_{false};
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
