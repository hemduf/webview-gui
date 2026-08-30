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

constexpr clap_id kFineTuneId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::FineTuning);
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kTwoPi = 2.0 * kPi;
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
        event.header.time = 73; // flush() intentionally loses sample offsets.
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
        event.header.time = 91; // flush() intentionally loses sample offsets.
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

double phaseIncrement(double fineCents) noexcept {
    return (440.0 * std::exp2((fineCents / 100.0) / 12.0)) / kSampleRate;
}

double wrappedPhase(double phase) noexcept {
    return phase - std::floor(phase);
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

bool matchesSineBlock(const std::array<float, kFrames> &left,
                      const std::array<float, kFrames> &right,
                      double startPhase,
                      double increment) noexcept {
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        const auto expected = static_cast<float>(std::sin(
            wrappedPhase(startPhase + static_cast<double>(frame) * increment) * kTwoPi));
        if (std::fabs(left[frame] - expected) > 1.0e-5f ||
            std::fabs(right[frame] - expected) > 1.0e-5f)
            return false;
    }
    return true;
}

} // namespace

int main() {
    const auto *factory = webview_gui::examples::polysynth::polysynthFactory();
    if (!factory)
        return 1;

    const auto *plugin = factory->create_plugin(
        factory, &kHost, webview_gui::examples::polysynth::kPolySynthPluginId);
    if (!plugin)
        return 2;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 3;
    }

    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params || !params->count || !params->get_info || !params->get_value ||
        !params->flush || params->count(plugin) != 2u) {
        plugin->destroy(plugin);
        return 4;
    }

    if (!plugin->activate(plugin, kSampleRate, 1, 64) ||
        !plugin->start_processing(plugin)) {
        plugin->destroy(plugin);
        return 5;
    }

    InputEvents noteOn;
    noteOn.pushNoteOn();
    std::array<float, kFrames> firstLeft{};
    std::array<float, kFrames> firstRight{};
    if (!processBlock(plugin, noteOn, firstLeft, firstRight)) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 6;
    }

    const double baseIncrement = phaseIncrement(0.0);
    const double modulatedIncrement = phaseIncrement(100.0);
    const double secondStartPhase = 0.25 + static_cast<double>(kFrames) * baseIncrement;

    plugin->stop_processing(plugin);
    OutputEvents flushOutput;
    FlushModEvent modulateOneSemitone(100.0);
    params->flush(plugin, &modulateOneSemitone.input, &flushOutput.output);
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 7;
    }

    InputEvents noEvents;
    std::array<float, kFrames> secondLeft{};
    std::array<float, kFrames> secondRight{};
    if (!processBlock(plugin, noEvents, secondLeft, secondRight) ||
        !matchesSineBlock(secondLeft,
                          secondRight,
                          secondStartPhase,
                          modulatedIncrement)) {
        std::fprintf(stderr,
                     "targeted PARAM_MOD from active params.flush was not applied at the next sample\n");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 8;
    }

    double hostBase = -1.0;
    if (!params->get_value(plugin, kFineTuneId, &hostBase) || hostBase != 0.0) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 9;
    }

    const double thirdStartPhase =
        secondStartPhase + static_cast<double>(kFrames) * modulatedIncrement;
    plugin->stop_processing(plugin);
    FlushValueEvent targetedBase(-100.0);
    params->flush(plugin, &targetedBase.input, &flushOutput.output);
    if (!plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 10;
    }

    std::array<float, kFrames> thirdLeft{};
    std::array<float, kFrames> thirdRight{};
    if (!processBlock(plugin, noEvents, thirdLeft, thirdRight) ||
        !matchesSineBlock(thirdLeft, thirdRight, thirdStartPhase, baseIncrement)) {
        std::fprintf(stderr,
                     "targeted PARAM_VALUE from active params.flush was not composed with modulation\n");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 11;
    }

    if (!params->get_value(plugin, kFineTuneId, &hostBase) || hostBase != 0.0) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 12;
    }

    clap_param_info_t info{};
    if (!params->get_info(plugin, 0, &info) || info.id != kFineTuneId ||
        info.flags != webview_gui::examples::polysynth::kPolyphonicParameterFlags) {
        std::fprintf(stderr,
                     "Fine Tune did not advertise the qualified polyphonic/modulation capability flags\n");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 13;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
