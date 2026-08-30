#include "polysynth_plugin.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/remote-controls.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id kMasterGainId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::MasterGain);
constexpr clap_id kWaveformId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::Waveform);
constexpr clap_id kFineTuneId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::FineTuning);
constexpr double kHalfGainDb = -6.020599913279624;
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kSampleRate = 48000.0;

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) {
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui PolySynth tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

struct InputEvents {
    InputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool pushNote(std::uint16_t type,
                  std::uint32_t time,
                  std::int32_t noteId,
                  std::int16_t key) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = notes[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = 1.0;
        headers[count++] = &event.header;
        return true;
    }

    bool pushValue(std::uint32_t time, clap_id paramId, double value) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = values[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = paramId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        headers[count++] = &event.header;
        return true;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        return events && events->ctx
                   ? static_cast<const InputEvents *>(events->ctx)->count
                   : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx)
            return nullptr;
        const auto &self = *static_cast<const InputEvents *>(events->ctx);
        return index < self.count ? self.headers[index] : nullptr;
    }

    std::array<clap_event_note_t, 6> notes{};
    std::array<clap_event_param_value_t, 6> values{};
    std::array<const clap_event_header_t *, 6> headers{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

struct OutputEvents {
    OutputEvents() noexcept {
        output.ctx = this;
        output.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t *events,
                                 const clap_event_header_t *header) noexcept {
        if (!events || !events->ctx || !header ||
            header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
            header->type != CLAP_EVENT_NOTE_END ||
            header->size != sizeof(clap_event_note_t))
            return false;

        auto &self = *static_cast<OutputEvents *>(events->ctx);
        if (self.count >= self.notes.size())
            return false;
        self.notes[self.count++] = *reinterpret_cast<const clap_event_note_t *>(header);
        return true;
    }

    std::array<clap_event_note_t, 4> notes{};
    std::uint32_t count = 0;
    clap_output_events_t output{};
};

struct FlushInputEvents {
    FlushInputEvents(clap_id paramId, double value) noexcept {
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
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        return events && events->ctx ? 1u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx || index != 0)
            return nullptr;
        return &static_cast<const FlushInputEvents *>(events->ctx)->event.header;
    }

    clap_event_param_value_t event{};
    clap_input_events_t input{};
};

struct RejectingOutputEvents {
    RejectingOutputEvents() noexcept {
        output.ctx = this;
        output.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t *,
                                 const clap_event_header_t *) noexcept {
        return false;
    }

    clap_output_events_t output{};
};

double phaseIncrement(std::int16_t key, double fineCents = 0.0) noexcept {
    const double semitones = static_cast<double>(key - 69) + fineCents / 100.0;
    return (440.0 * std::exp2(semitones / 12.0)) / kSampleRate;
}

double wrappedPhase(double phase) noexcept {
    phase -= std::floor(phase);
    return phase;
}

bool checkAudioPorts(const clap_plugin_t *plugin) {
    const auto *audioPorts = static_cast<const clap_plugin_audio_ports_t *>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (!audioPorts || !audioPorts->count || !audioPorts->get)
        return false;
    if (audioPorts->count(plugin, true) != 0 || audioPorts->count(plugin, false) != 1)
        return false;

    // Once count() reports zero, asking get() for index 0 is host misuse under
    // CLAP. Probe only valid indices so the test itself obeys the ABI contract.
    clap_audio_port_info_t info{};
    if (!audioPorts->get(plugin, 0, false, &info))
        return false;
    if (info.id != webview_gui::examples::polysynth::kPolySynthAudioOutputPortId ||
        (info.flags & CLAP_AUDIO_PORT_IS_MAIN) == 0 ||
        (info.flags & CLAP_AUDIO_PORT_SUPPORTS_64BITS) != 0 ||
        info.channel_count != 2 || !info.port_type ||
        std::strcmp(info.port_type, CLAP_PORT_STEREO) != 0 ||
        info.in_place_pair != CLAP_INVALID_ID ||
        std::strcmp(info.name, "Stereo Out") != 0)
        return false;
    return true;
}

bool checkNotePorts(const clap_plugin_t *plugin) {
    const auto *notePorts = static_cast<const clap_plugin_note_ports_t *>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    if (!notePorts || !notePorts->count || !notePorts->get)
        return false;

    // NOTE_END is an output event, but its address is explicitly matched against
    // the plugin's note input port. It does not require advertising a musical
    // note-output port for this instrument.
    if (notePorts->count(plugin, true) != 1 || notePorts->count(plugin, false) != 0)
        return false;

    clap_note_port_info_t input{};
    if (!notePorts->get(plugin, 0, true, &input) ||
        input.id != webview_gui::examples::polysynth::kPolySynthNoteInputPortId ||
        input.supported_dialects != CLAP_NOTE_DIALECT_CLAP ||
        input.preferred_dialect != CLAP_NOTE_DIALECT_CLAP ||
        std::strcmp(input.name, "Notes In") != 0)
        return false;

    return true;
}

bool checkNoteNames(const clap_plugin_t *plugin) {
    const auto *noteNames = static_cast<const clap_plugin_note_name_t *>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME));
    if (!noteNames || !noteNames->count || !noteNames->get ||
        noteNames->count(plugin) != 128u)
        return false;

    struct ExpectedName {
        std::uint32_t index;
        const char *name;
    };
    constexpr std::array<ExpectedName, 5> expected{{
        {0u, "C-1"},
        {1u, "C#-1"},
        {60u, "C4"},
        {69u, "A4"},
        {127u, "G9"},
    }};

    for (const auto &item : expected) {
        clap_note_name_t noteName{};
        if (!noteNames->get(plugin, item.index, &noteName) ||
            noteName.port != 0 || noteName.channel != -1 ||
            noteName.key != static_cast<std::int16_t>(item.index) ||
            std::strcmp(noteName.name, item.name) != 0)
            return false;
    }

    // `count()` defines the valid host query range. Calling get(count) is host
    // misuse and the pinned helper intentionally terminates under Minimal
    // checking, so this ABI test probes valid indices only.
    return true;
}

const clap_plugin_params_t *checkParams(const clap_plugin_t *plugin) {
    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params || !params->count || !params->get_info || !params->get_value ||
        !params->value_to_text || !params->text_to_value || !params->flush)
        return nullptr;

    // Preserve existing host indices while adding Waveform at index 2.
    if (params->count(plugin) != 3u)
        return nullptr;

    clap_param_info_t info{};
    clap_param_info_t repeatedInfo{};
    if (!params->get_info(plugin, 0, &info) || info.id != kFineTuneId ||
        !info.cookie ||
        !params->get_info(plugin, 0, &repeatedInfo) ||
        repeatedInfo.id != info.id || repeatedInfo.cookie != info.cookie ||
        info.flags != webview_gui::examples::polysynth::kPolyphonicParameterFlags ||
        info.min_value != -100.0 || info.max_value != 100.0 ||
        info.default_value != 0.0 ||
        std::strcmp(info.name, "Fine Tune") != 0 ||
        std::strcmp(info.module, "Oscillator") != 0)
        return nullptr;

    clap_param_info_t masterInfo{};
    clap_param_info_t repeatedMasterInfo{};
    if (!params->get_info(plugin, 1, &masterInfo) || masterInfo.id != kMasterGainId ||
        !masterInfo.cookie ||
        !params->get_info(plugin, 1, &repeatedMasterInfo) ||
        repeatedMasterInfo.id != masterInfo.id ||
        repeatedMasterInfo.cookie != masterInfo.cookie ||
        masterInfo.flags != webview_gui::examples::polysynth::kGlobalModulatableFlags ||
        masterInfo.min_value != -60.0 || masterInfo.max_value != 12.0 ||
        masterInfo.default_value != 0.0 ||
        std::strcmp(masterInfo.name, "Master Gain") != 0 ||
        std::strcmp(masterInfo.module, "Output") != 0)
        return nullptr;

    clap_param_info_t waveformInfo{};
    clap_param_info_t repeatedWaveformInfo{};
    const auto *waveformSpec = webview_gui::examples::polysynth::parameterSpecForId(kWaveformId);
    if (!waveformSpec ||
        !params->get_info(plugin, 2, &waveformInfo) || waveformInfo.id != kWaveformId ||
        !waveformInfo.cookie ||
        !params->get_info(plugin, 2, &repeatedWaveformInfo) ||
        repeatedWaveformInfo.id != waveformInfo.id ||
        repeatedWaveformInfo.cookie != waveformInfo.cookie ||
        waveformInfo.flags != waveformSpec->flags ||
        waveformInfo.min_value != 0.0 || waveformInfo.max_value != 2.0 ||
        waveformInfo.default_value != 0.0 ||
        std::strcmp(waveformInfo.name, "Waveform") != 0 ||
        std::strcmp(waveformInfo.module, "Oscillator") != 0)
        return nullptr;

    double value = -1.0;
    if (!params->get_value(plugin, kFineTuneId, &value) || value != 0.0 ||
        !params->get_value(plugin, kMasterGainId, &value) || value != 0.0 ||
        !params->get_value(plugin, kWaveformId, &value) || value != 0.0)
        return nullptr;

    char text[32]{};
    double parsed = 0.0;
    if (!params->value_to_text(plugin, kFineTuneId, 12.5, text, sizeof(text)) ||
        text[0] == '\0' ||
        !params->text_to_value(plugin, kFineTuneId, text, &parsed) ||
        std::fabs(parsed - 12.5) > 1.0e-6 ||
        !params->value_to_text(plugin, kMasterGainId, kHalfGainDb, text, sizeof(text)) ||
        text[0] == '\0' ||
        !params->text_to_value(plugin, kMasterGainId, text, &parsed) ||
        std::fabs(parsed - kHalfGainDb) > 1.0e-5 ||
        !params->value_to_text(plugin, kWaveformId, 1.75, text, sizeof(text)) ||
        std::strcmp(text, "Saw") != 0 ||
        !params->text_to_value(plugin, kWaveformId, "Square", &parsed) || parsed != 2.0)
        return nullptr;

    RejectingOutputEvents output;
    FlushInputEvents setFifty(kFineTuneId, 50.0);
    params->flush(plugin, &setFifty.input, &output.output);
    if (!params->get_value(plugin, kFineTuneId, &value) || value != 50.0)
        return nullptr;

    FlushInputEvents setSquare(kWaveformId, 2.0);
    params->flush(plugin, &setSquare.input, &output.output);
    if (!params->get_value(plugin, kWaveformId, &value) || value != 2.0)
        return nullptr;

    FlushInputEvents restoreZero(kFineTuneId, 0.0);
    params->flush(plugin, &restoreZero.input, &output.output);
    if (!params->get_value(plugin, kFineTuneId, &value) || value != 0.0)
        return nullptr;

    FlushInputEvents restoreSine(kWaveformId, 0.0);
    params->flush(plugin, &restoreSine.input, &output.output);
    if (!params->get_value(plugin, kWaveformId, &value) || value != 0.0)
        return nullptr;

    return params;
}

bool checkRemoteControls(const clap_plugin_t *plugin,
                         const clap_plugin_params_t *params) {
    const auto *remoteControls = static_cast<const clap_plugin_remote_controls_t *>(
        plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS));
    const auto *compat = static_cast<const clap_plugin_remote_controls_t *>(
        plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS_COMPAT));
    if (!remoteControls || !compat || !remoteControls->count || !remoteControls->get ||
        !compat->count || !compat->get || remoteControls->count(plugin) != 2u ||
        compat->count(plugin) != 2u)
        return false;

    clap_remote_controls_page_t first{};
    clap_remote_controls_page_t second{};
    clap_remote_controls_page_t compatible{};
    if (!remoteControls->get(plugin, 0u, &first) ||
        !remoteControls->get(plugin, 0u, &second) ||
        !compat->get(plugin, 0u, &compatible))
        return false;

    if (first.page_id == CLAP_INVALID_ID || first.page_id != second.page_id ||
        first.page_id != compatible.page_id || first.is_for_preset ||
        second.is_for_preset || compatible.is_for_preset ||
        std::strcmp(first.section_name, "Oscillator") != 0 ||
        std::strcmp(second.section_name, first.section_name) != 0 ||
        std::strcmp(compatible.section_name, first.section_name) != 0 ||
        std::strcmp(first.page_name, "Tuning") != 0 ||
        std::strcmp(second.page_name, first.page_name) != 0 ||
        std::strcmp(compatible.page_name, first.page_name) != 0)
        return false;

    if (!params || first.param_ids[0] != kFineTuneId ||
        second.param_ids[0] != kFineTuneId || compatible.param_ids[0] != kFineTuneId)
        return false;
    for (std::size_t index = 1; index < CLAP_REMOTE_CONTROLS_COUNT; ++index) {
        if (first.param_ids[index] != CLAP_INVALID_ID ||
            second.param_ids[index] != CLAP_INVALID_ID ||
            compatible.param_ids[index] != CLAP_INVALID_ID)
            return false;
    }

    clap_remote_controls_page_t outputPage{};
    clap_remote_controls_page_t compatibleOutputPage{};
    if (!remoteControls->get(plugin, 1u, &outputPage) ||
        !compat->get(plugin, 1u, &compatibleOutputPage) ||
        outputPage.page_id == CLAP_INVALID_ID ||
        outputPage.page_id == first.page_id ||
        outputPage.page_id != compatibleOutputPage.page_id ||
        outputPage.is_for_preset || compatibleOutputPage.is_for_preset ||
        std::strcmp(outputPage.section_name, "Output") != 0 ||
        std::strcmp(outputPage.page_name, "Performance") != 0 ||
        std::strcmp(compatibleOutputPage.section_name, outputPage.section_name) != 0 ||
        std::strcmp(compatibleOutputPage.page_name, outputPage.page_name) != 0 ||
        outputPage.param_ids[0] != kMasterGainId ||
        compatibleOutputPage.param_ids[0] != kMasterGainId)
        return false;
    for (std::size_t index = 1; index < CLAP_REMOTE_CONTROLS_COUNT; ++index) {
        if (outputPage.param_ids[index] != CLAP_INVALID_ID ||
            compatibleOutputPage.param_ids[index] != CLAP_INVALID_ID)
            return false;
    }

    clap_param_info_t mappedFineInfo{};
    clap_param_info_t mappedMasterInfo{};
    return params->get_info(plugin, 0u, &mappedFineInfo) &&
           params->get_info(plugin, 1u, &mappedMasterInfo) &&
           mappedFineInfo.id == first.param_ids[0] &&
           mappedMasterInfo.id == outputPage.param_ids[0];
}

bool checkActiveFlushHandoff(const clap_plugin_t *plugin,
                             const clap_plugin_params_t *params) {
    constexpr std::uint32_t kFrames = 8;
    std::array<float, kFrames> firstLeft{};
    std::array<float, kFrames> firstRight{};
    std::array<float *, 2> firstChannels{firstLeft.data(), firstRight.data()};
    clap_audio_buffer_t firstOutput{};
    firstOutput.data32 = firstChannels.data();
    firstOutput.channel_count = 2;

    InputEvents noteOn;
    if (!noteOn.pushNote(CLAP_EVENT_NOTE_ON, 0, 811, 60))
        return false;
    OutputEvents firstEvents;
    clap_process_t firstProcess{};
    firstProcess.frames_count = kFrames;
    firstProcess.audio_outputs = &firstOutput;
    firstProcess.audio_outputs_count = 1;
    firstProcess.in_events = &noteOn.input;
    firstProcess.out_events = &firstEvents.output;
    if (plugin->process(plugin, &firstProcess) == CLAP_PROCESS_ERROR || firstEvents.count != 0)
        return false;

    // CLAP permits params.flush while activated as long as it is not concurrent
    // with process(). A base-value update delivered in that state must affect an
    // already active generation when rendering resumes at the next sample.
    plugin->stop_processing(plugin);
    RejectingOutputEvents flushOutput;
    FlushInputEvents retune(kFineTuneId, 100.0);
    params->flush(plugin, &retune.input, &flushOutput.output);
    if (!plugin->start_processing(plugin))
        return false;

    std::array<float, kFrames> secondLeft{};
    std::array<float, kFrames> secondRight{};
    std::array<float *, 2> secondChannels{secondLeft.data(), secondRight.data()};
    clap_audio_buffer_t secondOutput{};
    secondOutput.data32 = secondChannels.data();
    secondOutput.channel_count = 2;
    InputEvents noEvents;
    OutputEvents secondEvents;
    clap_process_t secondProcess{};
    secondProcess.frames_count = kFrames;
    secondProcess.audio_outputs = &secondOutput;
    secondProcess.audio_outputs_count = 1;
    secondProcess.in_events = &noEvents.input;
    secondProcess.out_events = &secondEvents.output;
    if (plugin->process(plugin, &secondProcess) == CLAP_PROCESS_ERROR || secondEvents.count != 0)
        return false;

    const double firstIncrement = phaseIncrement(60, 0.0);
    const double retunedIncrement = phaseIncrement(60, 100.0);
    const double secondBlockPhase = 0.25 + static_cast<double>(kFrames) * firstIncrement;
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        const auto expected = static_cast<float>(std::sin(
            wrappedPhase(secondBlockPhase + static_cast<double>(frame) * retunedIncrement) *
            kTwoPi));
        if (std::fabs(secondLeft[frame] - expected) > 1.0e-5f ||
            std::fabs(secondRight[frame] - expected) > 1.0e-5f)
            return false;
    }

    double hostVisibleValue = 0.0;
    return params->get_value(plugin, kFineTuneId, &hostVisibleValue) &&
           hostVisibleValue == 100.0;
}

bool checkProcessBridge(const clap_plugin_t *plugin, const clap_plugin_params_t *params) {
    constexpr std::uint32_t kFrames = 16;
    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    std::array<float *, 2> channels{left.data(), right.data()};

    clap_audio_buffer_t outputBuffer{};
    outputBuffer.data32 = channels.data();
    outputBuffer.channel_count = static_cast<std::uint32_t>(channels.size());

    InputEvents inputEvents;
    if (!inputEvents.pushValue(0, kFineTuneId, 25.0) ||
        !inputEvents.pushNote(CLAP_EVENT_NOTE_ON, 4, 701, 69) ||
        !inputEvents.pushNote(CLAP_EVENT_NOTE_CHOKE, 8, 701, 69))
        return false;

    OutputEvents outputEvents;
    clap_process_t process{};
    process.steady_time = 0;
    process.frames_count = kFrames;
    process.transport = nullptr;
    process.audio_inputs = nullptr;
    process.audio_outputs = &outputBuffer;
    process.audio_inputs_count = 0;
    process.audio_outputs_count = 1;
    process.in_events = &inputEvents.input;
    process.out_events = &outputEvents.output;

    if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR)
        return false;

    // NOTE_ON at sample 4 must not leak audio into earlier samples. Do not
    // couple this bridge test to one exact oscillator phase: require finite,
    // centered, non-silent output somewhere in the active [4, 8) segment.
    for (std::uint32_t frame = 0; frame < 4; ++frame) {
        if (left[frame] != 0.0f || right[frame] != 0.0f)
            return false;
    }

    double activeMagnitude = 0.0;
    for (std::uint32_t frame = 4; frame < 8; ++frame) {
        if (!std::isfinite(left[frame]) || !std::isfinite(right[frame]) ||
            std::fabs(left[frame] - right[frame]) > 1.0e-6f)
            return false;
        activeMagnitude += std::fabs(static_cast<double>(left[frame]));
    }
    if (activeMagnitude <= 1.0e-6)
        return false;

    // NOTE_CHOKE is a hard lifecycle boundary, so no later sample in this
    // block may retain audio from the choked generation.
    for (std::uint32_t frame = 8; frame < kFrames; ++frame) {
        if (left[frame] != 0.0f || right[frame] != 0.0f)
            return false;
    }

    double hostVisibleValue = 0.0;
    if (!params || !params->get_value(plugin, kFineTuneId, &hostVisibleValue) ||
        hostVisibleValue != 25.0)
        return false;

    if (outputEvents.count != 1)
        return false;
    const auto &noteEnd = outputEvents.notes[0];
    return noteEnd.header.time == 8 &&
           noteEnd.header.type == CLAP_EVENT_NOTE_END &&
           noteEnd.note_id == 701 && noteEnd.port_index == 0 &&
           noteEnd.channel == 0 && noteEnd.key == 69;
}

bool checkMasterGainProcess(const clap_plugin_t *plugin,
                            const clap_plugin_params_t *params) {
    constexpr std::uint32_t kFrames = 8;
    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    std::array<float *, 2> channels{left.data(), right.data()};
    clap_audio_buffer_t output{};
    output.data32 = channels.data();
    output.channel_count = 2;

    InputEvents events;
    if (!events.pushValue(0, kFineTuneId, 0.0) ||
        !events.pushNote(CLAP_EVENT_NOTE_ON, 0, 901, 69) ||
        !events.pushValue(4, kMasterGainId, kHalfGainDb))
        return false;

    OutputEvents outputEvents;
    clap_process_t process{};
    process.frames_count = kFrames;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    process.in_events = &events.input;
    process.out_events = &outputEvents.output;
    if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR || outputEvents.count != 0)
        return false;

    const double increment = phaseIncrement(69, 0.0);
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        const float gain = frame < 4 ? 1.0f : 0.5f;
        const auto expected = static_cast<float>(std::sin(
            wrappedPhase(0.25 + static_cast<double>(frame) * increment) * kTwoPi) * gain);
        if (std::fabs(left[frame] - expected) > 1.0e-5f ||
            std::fabs(right[frame] - expected) > 1.0e-5f)
            return false;
    }

    double hostVisibleGain = 0.0;
    return params && params->get_value(plugin, kMasterGainId, &hostVisibleGain) &&
           std::fabs(hostVisibleGain - kHalfGainDb) <= 1.0e-5;
}

} // namespace

int main() {
    const auto *factory = webview_gui::examples::polysynth::polysynthFactory();
    if (!factory || factory->get_plugin_count(factory) != 1)
        return 1;

    const auto *descriptor = factory->get_plugin_descriptor(factory, 0);
    if (!descriptor || !descriptor->id ||
        std::strcmp(descriptor->id,
                    webview_gui::examples::polysynth::kPolySynthPluginId) != 0 ||
        factory->get_plugin_descriptor(factory, 1) != nullptr)
        return 2;

    const auto *plugin = factory->create_plugin(
        factory, &kHost, webview_gui::examples::polysynth::kPolySynthPluginId);
    if (!plugin)
        return 3;

    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 4;
    }

    const auto *params = checkParams(plugin);
    if (!checkAudioPorts(plugin) || !checkNotePorts(plugin) || !checkNoteNames(plugin) ||
        !params || !checkRemoteControls(plugin, params) ||
        plugin->get_extension(plugin, "clap.example.unimplemented") != nullptr) {
        plugin->destroy(plugin);
        return 5;
    }

    if (!plugin->activate(plugin, kSampleRate, 1, 128)) {
        plugin->destroy(plugin);
        return 6;
    }
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 7;
    }

    const bool flushHandoffOk = checkActiveFlushHandoff(plugin, params);
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    if (!flushHandoffOk) {
        plugin->destroy(plugin);
        return 8;
    }

    RejectingOutputEvents resetOutput;
    FlushInputEvents restoreZero(kFineTuneId, 0.0);
    params->flush(plugin, &restoreZero.input, &resetOutput.output);

    if (!plugin->activate(plugin, kSampleRate, 1, 128)) {
        plugin->destroy(plugin);
        return 9;
    }
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 10;
    }

    const bool processOk = checkProcessBridge(plugin, params);
    const bool masterGainOk = processOk && checkMasterGainProcess(plugin, params);
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return masterGainOk ? 0 : 11;
}
