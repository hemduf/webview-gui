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
using webview_gui::examples::polysynth::parameterSpecForId;

constexpr clap_id parameterId(ParameterSlot slot) noexcept {
    return webview_gui::examples::polysynth::kFirstParameterId +
           static_cast<clap_id>(slot);
}

constexpr clap_id kMasterGainId = parameterId(ParameterSlot::MasterGain);
constexpr clap_id kWaveformId = parameterId(ParameterSlot::Waveform);
constexpr clap_id kCoarseTuneId = parameterId(ParameterSlot::CoarseTuning);
constexpr clap_id kFineTuneId = parameterId(ParameterSlot::FineTuning);
constexpr clap_id kCutoffId = parameterId(ParameterSlot::FilterCutoff);
constexpr clap_id kResonanceId = parameterId(ParameterSlot::FilterResonance);
constexpr clap_id kAmpAttackId = parameterId(ParameterSlot::AmpAttack);
constexpr clap_id kAmpDecayId = parameterId(ParameterSlot::AmpDecay);
constexpr clap_id kAmpSustainId = parameterId(ParameterSlot::AmpSustain);
constexpr clap_id kAmpReleaseId = parameterId(ParameterSlot::AmpRelease);
constexpr clap_id kFilterEnvelopeAmountId = parameterId(ParameterSlot::FilterEnvelopeAmount);
constexpr clap_id kPanId = parameterId(ParameterSlot::Pan);
constexpr clap_id kAmpLevelId = parameterId(ParameterSlot::AmpLevel);
constexpr double kHalfGainDb = -6.020599913279624;
constexpr double kSampleRate = 48000.0;

constexpr std::array<clap_id, 13> kPublishedIds{{
    kFineTuneId,
    kMasterGainId,
    kWaveformId,
    kCoarseTuneId,
    kPanId,
    kCutoffId,
    kResonanceId,
    kFilterEnvelopeAmountId,
    kAmpLevelId,
    kAmpAttackId,
    kAmpDecayId,
    kAmpSustainId,
    kAmpReleaseId,
}};

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

template <std::size_t Capacity = 8>
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
        if (count >= Capacity)
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
        if (count >= Capacity)
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

    std::array<clap_event_note_t, Capacity> notes{};
    std::array<clap_event_param_value_t, Capacity> values{};
    std::array<const clap_event_header_t *, Capacity> headers{};
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

    std::array<clap_event_note_t, 8> notes{};
    std::uint32_t count = 0;
    clap_output_events_t output{};
};

struct FlushValueEvent {
    FlushValueEvent(clap_id paramId, double value) noexcept {
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
        return &static_cast<const FlushValueEvent *>(events->ctx)->event.header;
    }

    clap_event_param_value_t event{};
    clap_input_events_t input{};
};

bool setGlobalParameter(const clap_plugin_t *plugin,
                        const clap_plugin_params_t *params,
                        clap_id paramId,
                        double value) noexcept {
    FlushValueEvent event(paramId, value);
    params->flush(plugin, &event.input, nullptr);
    double observed = 0.0;
    return params->get_value(plugin, paramId, &observed) &&
           std::fabs(observed - value) <= 1.0e-6;
}

bool configureImmediateEnvelope(const clap_plugin_t *plugin,
                                const clap_plugin_params_t *params) noexcept {
    return setGlobalParameter(plugin, params, kAmpAttackId, 0.0) &&
           setGlobalParameter(plugin, params, kAmpDecayId, 0.0) &&
           setGlobalParameter(plugin, params, kAmpSustainId, 1.0) &&
           setGlobalParameter(plugin, params, kAmpReleaseId, 0.001);
}

struct PluginFixture {
    const clap_plugin_t *plugin = nullptr;
    const clap_plugin_params_t *params = nullptr;
    bool processing = false;
    bool activated = false;

    bool create(const clap_plugin_factory_t *factory,
                bool immediateEnvelope = true) noexcept {
        plugin = factory->create_plugin(
            factory, &kHost, webview_gui::examples::polysynth::kPolySynthPluginId);
        if (!plugin || !plugin->init(plugin))
            return false;
        params = static_cast<const clap_plugin_params_t *>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        if (!params || !params->count || !params->get_info || !params->get_value ||
            !params->value_to_text || !params->text_to_value || !params->flush)
            return false;
        return !immediateEnvelope || configureImmediateEnvelope(plugin, params);
    }

    bool activate(std::uint32_t maximumFrames = 128) noexcept {
        activated = plugin->activate(plugin, kSampleRate, 1, maximumFrames);
        if (!activated)
            return false;
        processing = plugin->start_processing(plugin);
        return processing;
    }

    bool stop() noexcept {
        if (!processing)
            return true;
        plugin->stop_processing(plugin);
        processing = false;
        return true;
    }

    bool start() noexcept {
        if (processing)
            return true;
        processing = plugin->start_processing(plugin);
        return processing;
    }

    void destroy() noexcept {
        if (!plugin)
            return;
        if (processing)
            plugin->stop_processing(plugin);
        if (activated)
            plugin->deactivate(plugin);
        plugin->destroy(plugin);
        plugin = nullptr;
        params = nullptr;
        processing = false;
        activated = false;
    }
};

template <std::size_t Frames, std::size_t Capacity>
bool processBlock(const clap_plugin_t *plugin,
                  InputEvents<Capacity> &events,
                  std::array<float, Frames> &left,
                  std::array<float, Frames> &right,
                  OutputEvents &outEvents) noexcept {
    std::array<float *, 2> channels{left.data(), right.data()};
    clap_audio_buffer_t output{};
    output.data32 = channels.data();
    output.channel_count = 2;
    clap_process_t process{};
    process.frames_count = static_cast<std::uint32_t>(Frames);
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    process.in_events = &events.input;
    process.out_events = &outEvents.output;
    return plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
}

template <std::size_t Frames>
bool sameBlock(const std::array<float, Frames> &aLeft,
               const std::array<float, Frames> &aRight,
               const std::array<float, Frames> &bLeft,
               const std::array<float, Frames> &bRight,
               float scaleA = 1.0f,
               float scaleB = 1.0f,
               float tolerance = 1.0e-6f) noexcept {
    for (std::size_t i = 0; i < Frames; ++i) {
        if (std::fabs(aLeft[i] * scaleA - bLeft[i] * scaleB) > tolerance ||
            std::fabs(aRight[i] * scaleA - bRight[i] * scaleB) > tolerance)
            return false;
    }
    return true;
}

bool checkPortsAndMetadata(const clap_plugin_t *plugin,
                           const clap_plugin_params_t *params) noexcept {
    const auto *audioPorts = static_cast<const clap_plugin_audio_ports_t *>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto *notePorts = static_cast<const clap_plugin_note_ports_t *>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    const auto *noteNames = static_cast<const clap_plugin_note_name_t *>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME));
    if (!audioPorts || !audioPorts->count || !audioPorts->get ||
        !notePorts || !notePorts->count || !notePorts->get ||
        !noteNames || !noteNames->count || !noteNames->get)
        return false;

    if (audioPorts->count(plugin, true) != 0u || audioPorts->count(plugin, false) != 1u)
        return false;
    clap_audio_port_info_t audio{};
    if (!audioPorts->get(plugin, 0, false, &audio) ||
        audio.id != webview_gui::examples::polysynth::kPolySynthAudioOutputPortId ||
        (audio.flags & CLAP_AUDIO_PORT_IS_MAIN) == 0u || audio.channel_count != 2u ||
        !audio.port_type || std::strcmp(audio.port_type, CLAP_PORT_STEREO) != 0 ||
        audio.in_place_pair != CLAP_INVALID_ID)
        return false;

    if (notePorts->count(plugin, true) != 1u || notePorts->count(plugin, false) != 0u)
        return false;
    clap_note_port_info_t note{};
    if (!notePorts->get(plugin, 0, true, &note) ||
        note.id != webview_gui::examples::polysynth::kPolySynthNoteInputPortId ||
        note.supported_dialects != CLAP_NOTE_DIALECT_CLAP ||
        note.preferred_dialect != CLAP_NOTE_DIALECT_CLAP)
        return false;

    if (noteNames->count(plugin) != 128u)
        return false;
    constexpr std::array<std::pair<std::uint32_t, const char *>, 3> names{{
        {0u, "C-1"}, {69u, "A4"}, {127u, "G9"},
    }};
    for (const auto &expected : names) {
        clap_note_name_t value{};
        if (!noteNames->get(plugin, expected.first, &value) ||
            value.port != 0 || value.channel != -1 ||
            value.key != static_cast<std::int16_t>(expected.first) ||
            std::strcmp(value.name, expected.second) != 0)
            return false;
    }

    if (params->count(plugin) != kPublishedIds.size())
        return false;
    for (std::uint32_t index = 0; index < kPublishedIds.size(); ++index) {
        clap_param_info_t info{};
        if (!params->get_info(plugin, index, &info) || info.id != kPublishedIds[index] ||
            !info.cookie)
            return false;
        const auto *spec = parameterSpecForId(info.id);
        if (!spec || info.flags != spec->flags || info.min_value != spec->minValue ||
            info.max_value != spec->maxValue || info.default_value != spec->defaultValue ||
            std::strcmp(info.name, spec->name) != 0 ||
            std::strcmp(info.module, spec->module) != 0)
            return false;
        double value = 0.0;
        if (!params->get_value(plugin, info.id, &value) ||
            std::fabs(value - spec->defaultValue) > 1.0e-6)
            return false;
    }
    clap_param_info_t invalid{};
    if (params->get_info(plugin, static_cast<std::uint32_t>(kPublishedIds.size()), &invalid))
        return false;

    char text[32]{};
    double parsed = 0.0;
    return params->value_to_text(plugin, kWaveformId, 1.0, text, sizeof(text)) &&
           std::strcmp(text, "Saw") == 0 &&
           params->text_to_value(plugin, kWaveformId, "Square", &parsed) && parsed == 2.0 &&
           params->value_to_text(plugin, kCoarseTuneId, -12.0, text, sizeof(text)) &&
           std::strcmp(text, "-12") == 0 &&
           params->text_to_value(plugin, kFineTuneId, "12.5", &parsed) &&
           std::fabs(parsed - 12.5) <= 1.0e-12;
}

bool checkRemoteControls(const clap_plugin_t *plugin) noexcept {
    const auto *remote = static_cast<const clap_plugin_remote_controls_t *>(
        plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS));
    const auto *compat = static_cast<const clap_plugin_remote_controls_t *>(
        plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS_COMPAT));
    if (!remote || !compat || !remote->count || !remote->get ||
        !compat->count || !compat->get || remote->count(plugin) != 4u ||
        compat->count(plugin) != 4u)
        return false;

    constexpr std::array<std::array<clap_id, 4>, 4> expected{{
        {{kFineTuneId, kWaveformId, kCoarseTuneId, CLAP_INVALID_ID}},
        {{kMasterGainId, kPanId, kAmpLevelId, CLAP_INVALID_ID}},
        {{kCutoffId, kResonanceId, kFilterEnvelopeAmountId, CLAP_INVALID_ID}},
        {{kAmpAttackId, kAmpDecayId, kAmpSustainId, kAmpReleaseId}},
    }};
    std::array<clap_id, 4> pageIds{};
    for (std::uint32_t pageIndex = 0; pageIndex < expected.size(); ++pageIndex) {
        clap_remote_controls_page_t page{};
        clap_remote_controls_page_t old{};
        if (!remote->get(plugin, pageIndex, &page) ||
            !compat->get(plugin, pageIndex, &old) ||
            page.page_id == CLAP_INVALID_ID || page.page_id != old.page_id ||
            page.is_for_preset || old.is_for_preset)
            return false;
        pageIds[pageIndex] = page.page_id;
        for (std::size_t i = 0; i < expected[pageIndex].size(); ++i) {
            if (page.param_ids[i] != expected[pageIndex][i] ||
                old.param_ids[i] != expected[pageIndex][i])
                return false;
        }
        for (std::size_t i = expected[pageIndex].size(); i < CLAP_REMOTE_CONTROLS_COUNT; ++i) {
            if (page.param_ids[i] != CLAP_INVALID_ID || old.param_ids[i] != CLAP_INVALID_ID)
                return false;
        }
    }
    for (std::size_t i = 0; i < pageIds.size(); ++i)
        for (std::size_t j = i + 1; j < pageIds.size(); ++j)
            if (pageIds[i] == pageIds[j])
                return false;
    return true;
}

bool checkActiveFlushHandoff(const clap_plugin_factory_t *factory) noexcept {
    PluginFixture subject;
    PluginFixture reference;
    if (!subject.create(factory) || !reference.create(factory) ||
        !subject.activate() || !reference.activate()) {
        subject.destroy();
        reference.destroy();
        return false;
    }

    InputEvents<> subjectOn;
    InputEvents<> referenceOn;
    subjectOn.pushNote(CLAP_EVENT_NOTE_ON, 0, 811, 60);
    referenceOn.pushNote(CLAP_EVENT_NOTE_ON, 0, 811, 60);
    std::array<float, 8> subjectFirstL{}, subjectFirstR{}, referenceFirstL{}, referenceFirstR{};
    OutputEvents subjectOut;
    OutputEvents referenceOut;
    if (!processBlock(subject.plugin, subjectOn, subjectFirstL, subjectFirstR, subjectOut) ||
        !processBlock(reference.plugin, referenceOn, referenceFirstL, referenceFirstR, referenceOut) ||
        !sameBlock(subjectFirstL, subjectFirstR, referenceFirstL, referenceFirstR)) {
        subject.destroy();
        reference.destroy();
        return false;
    }

    subject.stop();
    reference.stop();
    if (!setGlobalParameter(subject.plugin, subject.params, kFineTuneId, 100.0) ||
        !setGlobalParameter(subject.plugin, subject.params, kAmpLevelId, 0.5) ||
        !setGlobalParameter(reference.plugin, reference.params, kFineTuneId, 100.0) ||
        !subject.start() || !reference.start()) {
        subject.destroy();
        reference.destroy();
        return false;
    }

    InputEvents<> noneA;
    InputEvents<> noneB;
    std::array<float, 8> subjectSecondL{}, subjectSecondR{}, referenceSecondL{}, referenceSecondR{};
    OutputEvents subjectOut2;
    OutputEvents referenceOut2;
    const bool ok = processBlock(subject.plugin, noneA, subjectSecondL, subjectSecondR, subjectOut2) &&
                    processBlock(reference.plugin, noneB, referenceSecondL, referenceSecondR, referenceOut2) &&
                    sameBlock(subjectSecondL,
                              subjectSecondR,
                              referenceSecondL,
                              referenceSecondR,
                              2.0f,
                              1.0f,
                              2.0e-6f);
    double fine = 0.0;
    double amp = 0.0;
    const bool valuesOk = subject.params->get_value(subject.plugin, kFineTuneId, &fine) &&
                          fine == 100.0 &&
                          subject.params->get_value(subject.plugin, kAmpLevelId, &amp) &&
                          amp == 0.5;
    subject.destroy();
    reference.destroy();
    return ok && valuesOk;
}

bool checkProcessBridge(const clap_plugin_factory_t *factory) noexcept {
    PluginFixture fixture;
    if (!fixture.create(factory) || !fixture.activate()) {
        fixture.destroy();
        return false;
    }
    InputEvents<> events;
    events.pushValue(0, kFineTuneId, 25.0);
    events.pushNote(CLAP_EVENT_NOTE_ON, 4, 701, 69);
    events.pushNote(CLAP_EVENT_NOTE_CHOKE, 8, 701, 69);
    std::array<float, 16> left{}, right{};
    OutputEvents output;
    if (!processBlock(fixture.plugin, events, left, right, output)) {
        fixture.destroy();
        return false;
    }
    for (std::size_t i = 0; i < 4; ++i)
        if (left[i] != 0.0f || right[i] != 0.0f) {
            fixture.destroy();
            return false;
        }
    double magnitude = 0.0;
    for (std::size_t i = 4; i < 8; ++i) {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i]) ||
            std::fabs(left[i] - right[i]) > 1.0e-6f) {
            fixture.destroy();
            return false;
        }
        magnitude += std::fabs(left[i]);
    }
    for (std::size_t i = 8; i < left.size(); ++i)
        if (left[i] != 0.0f || right[i] != 0.0f) {
            fixture.destroy();
            return false;
        }
    const bool noteEndOk = magnitude > 1.0e-6 && output.count == 1u &&
                           output.notes[0].header.time == 8u &&
                           output.notes[0].note_id == 701;
    fixture.destroy();
    return noteEndOk;
}

bool checkMasterGainProcess(const clap_plugin_factory_t *factory) noexcept {
    PluginFixture subject;
    PluginFixture reference;
    if (!subject.create(factory) || !reference.create(factory) ||
        !subject.activate() || !reference.activate()) {
        subject.destroy();
        reference.destroy();
        return false;
    }
    InputEvents<> subjectEvents;
    InputEvents<> referenceEvents;
    subjectEvents.pushNote(CLAP_EVENT_NOTE_ON, 0, 901, 69);
    subjectEvents.pushValue(4, kMasterGainId, kHalfGainDb);
    referenceEvents.pushNote(CLAP_EVENT_NOTE_ON, 0, 901, 69);
    std::array<float, 8> subjectL{}, subjectR{}, referenceL{}, referenceR{};
    OutputEvents subjectOut;
    OutputEvents referenceOut;
    if (!processBlock(subject.plugin, subjectEvents, subjectL, subjectR, subjectOut) ||
        !processBlock(reference.plugin, referenceEvents, referenceL, referenceR, referenceOut)) {
        subject.destroy();
        reference.destroy();
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < subjectL.size(); ++i) {
        const float scale = i < 4 ? 1.0f : 2.0f;
        if (std::fabs(subjectL[i] * scale - referenceL[i]) > 2.0e-6f ||
            std::fabs(subjectR[i] * scale - referenceR[i]) > 2.0e-6f) {
            ok = false;
            break;
        }
    }
    double gain = 0.0;
    ok = ok && subject.params->get_value(subject.plugin, kMasterGainId, &gain) &&
         std::fabs(gain - kHalfGainDb) <= 1.0e-5;
    subject.destroy();
    reference.destroy();
    return ok;
}

bool checkProductionFilterProcess(const clap_plugin_factory_t *factory) noexcept {
    PluginFixture moved;
    PluginFixture reference;
    if (!moved.create(factory) || !reference.create(factory) ||
        !moved.activate(128) || !reference.activate(128)) {
        moved.destroy();
        reference.destroy();
        return false;
    }

    InputEvents<> movedEvents;
    InputEvents<> referenceEvents;
    movedEvents.pushNote(CLAP_EVENT_NOTE_ON, 0, 1201, 84);
    movedEvents.pushValue(32, kCutoffId, 200.0);
    referenceEvents.pushNote(CLAP_EVENT_NOTE_ON, 0, 1201, 84);
    std::array<float, 128> movedL{}, movedR{}, referenceL{}, referenceR{};
    OutputEvents movedOut;
    OutputEvents referenceOut;
    if (!processBlock(moved.plugin, movedEvents, movedL, movedR, movedOut) ||
        !processBlock(reference.plugin, referenceEvents, referenceL, referenceR, referenceOut)) {
        moved.destroy();
        reference.destroy();
        return false;
    }

    // Before the parameter event both complete production plugin instances must
    // be bit-equivalent. At and after sample 32 the low cutoff must change the
    // actual rendered audio. This regression fails if activate() forgets to enable
    // the filter and the voice-local Cutoff setter becomes a silent no-op.
    bool prefixEqual = true;
    for (std::size_t i = 0; i < 32; ++i) {
        if (std::fabs(movedL[i] - referenceL[i]) > 1.0e-6f ||
            std::fabs(movedR[i] - referenceR[i]) > 1.0e-6f) {
            prefixEqual = false;
            break;
        }
    }
    double delta = 0.0;
    for (std::size_t i = 32; i < movedL.size(); ++i)
        delta += std::fabs(static_cast<double>(movedL[i] - referenceL[i])) +
                 std::fabs(static_cast<double>(movedR[i] - referenceR[i]));
    double cutoff = 0.0;
    const bool hostValueOk = moved.params->get_value(moved.plugin, kCutoffId, &cutoff) &&
                             std::fabs(cutoff - 200.0) <= 1.0e-6;
    moved.destroy();
    reference.destroy();
    return prefixEqual && delta > 1.0e-4 && hostValueOk;
}

} // namespace

int main() {
    const auto *factory = webview_gui::examples::polysynth::polysynthFactory();
    if (!factory || factory->get_plugin_count(factory) != 1u)
        return 1;
    const auto *descriptor = factory->get_plugin_descriptor(factory, 0);
    if (!descriptor || !descriptor->id ||
        std::strcmp(descriptor->id,
                    webview_gui::examples::polysynth::kPolySynthPluginId) != 0 ||
        factory->get_plugin_descriptor(factory, 1) != nullptr)
        return 2;

    PluginFixture metadata;
    if (!metadata.create(factory, false)) {
        metadata.destroy();
        return 3;
    }
    const bool metadataOk = checkPortsAndMetadata(metadata.plugin, metadata.params) &&
                            checkRemoteControls(metadata.plugin) &&
                            metadata.plugin->get_extension(metadata.plugin,
                                                           "clap.example.unimplemented") == nullptr;
    metadata.destroy();
    if (!metadataOk)
        return 4;

    if (!checkActiveFlushHandoff(factory))
        return 5;
    if (!checkProcessBridge(factory))
        return 6;
    if (!checkMasterGainProcess(factory))
        return 7;
    if (!checkProductionFilterProcess(factory))
        return 8;
    return 0;
}
