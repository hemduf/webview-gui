#include "polysynth_plugin.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>
#include <clap/ext/params.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id parameterId(ParameterSlot slot) noexcept {
    return webview_gui::examples::polysynth::kFirstParameterId +
           static_cast<clap_id>(slot);
}

constexpr clap_id kFineTuneId = parameterId(ParameterSlot::FineTuning);
constexpr clap_id kAmpAttackId = parameterId(ParameterSlot::AmpAttack);
constexpr clap_id kAmpDecayId = parameterId(ParameterSlot::AmpDecay);
constexpr clap_id kAmpSustainId = parameterId(ParameterSlot::AmpSustain);
constexpr clap_id kAmpReleaseId = parameterId(ParameterSlot::AmpRelease);
constexpr double kSampleRate = 48000.0;
constexpr std::uint32_t kFrames = 8;
constexpr std::int32_t kNoteId = 901;
constexpr std::int16_t kKey = 69;

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) {
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui PolySynth polyphonic flush tests",
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

    bool pushNoteOn() noexcept {
        note = {};
        note.header.size = sizeof(note);
        note.header.time = 0;
        note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        note.header.type = CLAP_EVENT_NOTE_ON;
        note.note_id = kNoteId;
        note.port_index = 0;
        note.channel = 0;
        note.key = kKey;
        note.velocity = 1.0;
        count = 1;
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
        return index == 0 && self.count == 1 ? &self.note.header : nullptr;
    }

    clap_event_note_t note{};
    std::uint32_t count = 0;
    clap_input_events_t input{};
};

struct OutputEvents {
    OutputEvents() noexcept {
        output.ctx = this;
        output.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t *,
                                 const clap_event_header_t *) noexcept {
        return true;
    }

    clap_output_events_t output{};
};

struct FlushModEvent {
    explicit FlushModEvent(double amount) noexcept {
        event = {};
        event.header.size = sizeof(event);
        event.header.time = 73; // params.flush() intentionally ignores sample offsets.
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_MOD;
        event.param_id = kFineTuneId;
        event.note_id = kNoteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = kKey;
        event.amount = amount;
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
        return &static_cast<const FlushModEvent *>(events->ctx)->event.header;
    }

    clap_event_param_mod_t event{};
    clap_input_events_t input{};
};

struct FlushValueEvent {
    explicit FlushValueEvent(double value) noexcept {
        event = {};
        event.header.size = sizeof(event);
        event.header.time = 91; // params.flush() intentionally ignores sample offsets.
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kFineTuneId;
        event.note_id = kNoteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = kKey;
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

struct GlobalFlushValueEvent {
    GlobalFlushValueEvent(clap_id paramId, double value) noexcept {
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
        return &static_cast<const GlobalFlushValueEvent *>(events->ctx)->event.header;
    }

    clap_event_param_value_t event{};
    clap_input_events_t input{};
};

bool setGlobalParameter(const clap_plugin_t *plugin,
                        const clap_plugin_params_t *params,
                        clap_id paramId,
                        double value) noexcept {
    GlobalFlushValueEvent event(paramId, value);
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

bool processBlock(const clap_plugin_t *plugin,
                  InputEvents &events,
                  std::array<float, kFrames> &left,
                  std::array<float, kFrames> &right) noexcept {
    std::array<float *, 2> channels{left.data(), right.data()};
    clap_audio_buffer_t output{};
    output.data32 = channels.data();
    output.channel_count = 2;
    OutputEvents outEvents;
    clap_process_t process{};
    process.frames_count = kFrames;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1;
    process.in_events = &events.input;
    process.out_events = &outEvents.output;
    return plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
}

bool sameBlock(const std::array<float, kFrames> &aLeft,
               const std::array<float, kFrames> &aRight,
               const std::array<float, kFrames> &bLeft,
               const std::array<float, kFrames> &bRight,
               float tolerance = 1.0e-6f) noexcept {
    for (std::size_t i = 0; i < aLeft.size(); ++i) {
        if (std::fabs(aLeft[i] - bLeft[i]) > tolerance ||
            std::fabs(aRight[i] - bRight[i]) > tolerance)
            return false;
    }
    return true;
}

struct PluginFixture {
    const clap_plugin_t *plugin = nullptr;
    const clap_plugin_params_t *params = nullptr;

    bool create(const clap_plugin_factory_t *factory) noexcept {
        plugin = factory->create_plugin(
            factory, &kHost, webview_gui::examples::polysynth::kPolySynthPluginId);
        if (!plugin || !plugin->init(plugin))
            return false;
        params = static_cast<const clap_plugin_params_t *>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        return params && params->count && params->get_info && params->get_value &&
               params->flush && params->count(plugin) == 13u &&
               configureImmediateEnvelope(plugin, params);
    }

    bool activate() noexcept {
        return plugin->activate(plugin, kSampleRate, 1, 64) &&
               plugin->start_processing(plugin);
    }

    void destroy() noexcept {
        if (!plugin)
            return;
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        plugin = nullptr;
        params = nullptr;
    }
};

} // namespace

int main() {
    const auto *factory = webview_gui::examples::polysynth::polysynthFactory();
    if (!factory)
        return 1;

    PluginFixture subject;
    PluginFixture reference;
    if (!subject.create(factory) || !reference.create(factory)) {
        subject.destroy();
        reference.destroy();
        return 2;
    }

    if (!subject.activate() || !reference.activate()) {
        subject.destroy();
        reference.destroy();
        return 3;
    }

    // Both real plugin instances begin with the same production filter, envelope,
    // oscillator and note state. The reference instance therefore provides a
    // filter-aware oracle without duplicating DSP equations in this CLAP test.
    InputEvents subjectNoteOn;
    InputEvents referenceNoteOn;
    subjectNoteOn.pushNoteOn();
    referenceNoteOn.pushNoteOn();
    std::array<float, kFrames> subjectFirstLeft{};
    std::array<float, kFrames> subjectFirstRight{};
    std::array<float, kFrames> referenceFirstLeft{};
    std::array<float, kFrames> referenceFirstRight{};
    if (!processBlock(subject.plugin, subjectNoteOn, subjectFirstLeft, subjectFirstRight) ||
        !processBlock(reference.plugin, referenceNoteOn, referenceFirstLeft, referenceFirstRight) ||
        !sameBlock(subjectFirstLeft,
                   subjectFirstRight,
                   referenceFirstLeft,
                   referenceFirstRight)) {
        subject.destroy();
        reference.destroy();
        return 4;
    }

    // Targeted +100-cent modulation on the subject must be audibly identical to
    // a +100-cent global base on the otherwise identical reference. Only the
    // subject's host-visible global base must remain zero.
    subject.plugin->stop_processing(subject.plugin);
    reference.plugin->stop_processing(reference.plugin);
    OutputEvents flushOutput;
    FlushModEvent targetedMod(100.0);
    subject.params->flush(subject.plugin, &targetedMod.input, &flushOutput.output);
    if (!setGlobalParameter(reference.plugin, reference.params, kFineTuneId, 100.0) ||
        !subject.plugin->start_processing(subject.plugin) ||
        !reference.plugin->start_processing(reference.plugin)) {
        subject.destroy();
        reference.destroy();
        return 5;
    }

    InputEvents noSubjectEvents;
    InputEvents noReferenceEvents;
    std::array<float, kFrames> subjectSecondLeft{};
    std::array<float, kFrames> subjectSecondRight{};
    std::array<float, kFrames> referenceSecondLeft{};
    std::array<float, kFrames> referenceSecondRight{};
    if (!processBlock(subject.plugin, noSubjectEvents, subjectSecondLeft, subjectSecondRight) ||
        !processBlock(reference.plugin,
                      noReferenceEvents,
                      referenceSecondLeft,
                      referenceSecondRight) ||
        !sameBlock(subjectSecondLeft,
                   subjectSecondRight,
                   referenceSecondLeft,
                   referenceSecondRight)) {
        std::fprintf(stderr,
                     "targeted PARAM_MOD from active params.flush was not applied at the next sample\n");
        subject.destroy();
        reference.destroy();
        return 6;
    }

    double hostBase = -1.0;
    if (!subject.params->get_value(subject.plugin, kFineTuneId, &hostBase) || hostBase != 0.0) {
        subject.destroy();
        reference.destroy();
        return 7;
    }

    // Targeted -100-cent base + retained targeted +100-cent modulation must
    // compose back to zero. Make the reference global base zero; with identical
    // oscillator/filter histories the next blocks must match exactly.
    subject.plugin->stop_processing(subject.plugin);
    reference.plugin->stop_processing(reference.plugin);
    FlushValueEvent targetedBase(-100.0);
    subject.params->flush(subject.plugin, &targetedBase.input, &flushOutput.output);
    if (!setGlobalParameter(reference.plugin, reference.params, kFineTuneId, 0.0) ||
        !subject.plugin->start_processing(subject.plugin) ||
        !reference.plugin->start_processing(reference.plugin)) {
        subject.destroy();
        reference.destroy();
        return 8;
    }

    std::array<float, kFrames> subjectThirdLeft{};
    std::array<float, kFrames> subjectThirdRight{};
    std::array<float, kFrames> referenceThirdLeft{};
    std::array<float, kFrames> referenceThirdRight{};
    if (!processBlock(subject.plugin, noSubjectEvents, subjectThirdLeft, subjectThirdRight) ||
        !processBlock(reference.plugin,
                      noReferenceEvents,
                      referenceThirdLeft,
                      referenceThirdRight) ||
        !sameBlock(subjectThirdLeft,
                   subjectThirdRight,
                   referenceThirdLeft,
                   referenceThirdRight)) {
        std::fprintf(stderr,
                     "targeted PARAM_VALUE from active params.flush was not composed with modulation\n");
        subject.destroy();
        reference.destroy();
        return 9;
    }

    if (!subject.params->get_value(subject.plugin, kFineTuneId, &hostBase) || hostBase != 0.0) {
        subject.destroy();
        reference.destroy();
        return 10;
    }

    clap_param_info_t info{};
    if (!subject.params->get_info(subject.plugin, 0, &info) || info.id != kFineTuneId ||
        info.flags != webview_gui::examples::polysynth::kPolyphonicParameterFlags) {
        std::fprintf(stderr,
                     "Fine Tune did not advertise the qualified polyphonic/modulation capability flags\n");
        subject.destroy();
        reference.destroy();
        return 11;
    }

    subject.destroy();
    reference.destroy();
    return 0;
}
