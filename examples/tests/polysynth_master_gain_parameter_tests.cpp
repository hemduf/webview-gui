#include "polysynth_plugin.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/remote-controls.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id kMasterGainId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::MasterGain);
constexpr clap_id kFineTuneId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::FineTuning);
constexpr double kSampleRate = 48000.0;
constexpr std::uint32_t kFrames = 8;
constexpr std::int32_t kNoteId = 4101;
constexpr std::int16_t kKey = 69;
constexpr double kHalfGainDb = -6.020599913279624;
constexpr double kQuarterGainDb = -12.041199826559248;
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kTwoPi = 2.0 * kPi;

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) {
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui PolySynth Master Gain tests",
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

    bool pushNoteOn(std::uint32_t time = 0,
                    std::int32_t noteId = kNoteId,
                    std::int16_t key = kKey) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = notes[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = 1.0;
        headers[count++] = &event.header;
        return true;
    }

    bool pushValue(std::uint32_t time,
                   clap_id paramId,
                   double value,
                   std::int32_t noteId = -1,
                   std::int16_t portIndex = -1,
                   std::int16_t channel = -1,
                   std::int16_t key = -1) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = values[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = paramId;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.value = value;
        headers[count++] = &event.header;
        return true;
    }

    bool pushMod(std::uint32_t time,
                 clap_id paramId,
                 double amount,
                 std::int32_t noteId = -1,
                 std::int16_t portIndex = -1,
                 std::int16_t channel = -1,
                 std::int16_t key = -1) noexcept {
        if (count >= headers.size())
            return false;
        auto &event = mods[count];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_MOD;
        event.param_id = paramId;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.amount = amount;
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

    std::array<clap_event_note_t, 12> notes{};
    std::array<clap_event_param_value_t, 12> values{};
    std::array<clap_event_param_mod_t, 12> mods{};
    std::array<const clap_event_header_t *, 12> headers{};
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

struct SingleFlushEvent {
    SingleFlushEvent(std::uint16_t type,
                     clap_id paramId,
                     double valueOrAmount,
                     std::int32_t noteId = -1,
                     std::int16_t portIndex = -1,
                     std::int16_t channel = -1,
                     std::int16_t key = -1) noexcept {
        if (type == CLAP_EVENT_PARAM_VALUE) {
            value = {};
            value.header.size = sizeof(value);
            value.header.time = 79;
            value.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            value.header.type = CLAP_EVENT_PARAM_VALUE;
            value.param_id = paramId;
            value.note_id = noteId;
            value.port_index = portIndex;
            value.channel = channel;
            value.key = key;
            value.value = valueOrAmount;
            header = &value.header;
        } else {
            mod = {};
            mod.header.size = sizeof(mod);
            mod.header.time = 83;
            mod.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            mod.header.type = CLAP_EVENT_PARAM_MOD;
            mod.param_id = paramId;
            mod.note_id = noteId;
            mod.port_index = portIndex;
            mod.channel = channel;
            mod.key = key;
            mod.amount = valueOrAmount;
            header = &mod.header;
        }
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
        return static_cast<const SingleFlushEvent *>(events->ctx)->header;
    }

    clap_event_param_value_t value{};
    clap_event_param_mod_t mod{};
    const clap_event_header_t *header = nullptr;
    clap_input_events_t input{};
};

struct StateOutput {
    StateOutput() noexcept {
        stream.ctx = this;
        stream.write = write;
    }

    static std::int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                       const void *source,
                                       std::uint64_t size) noexcept {
        if (!stream || !stream->ctx || (!source && size != 0))
            return -1;
        auto &self = *static_cast<StateOutput *>(stream->ctx);
        if (size > self.bytes.size() - self.used)
            return -1;
        std::memcpy(self.bytes.data() + self.used, source, static_cast<std::size_t>(size));
        self.used += static_cast<std::size_t>(size);
        return static_cast<std::int64_t>(size);
    }

    std::array<std::uint8_t, 128> bytes{};
    std::size_t used = 0;
    clap_ostream_t stream{};
};

struct StateInput {
    StateInput(const std::uint8_t *data, std::size_t size) noexcept
        : bytes(data), sizeBytes(size) {
        stream.ctx = this;
        stream.read = read;
    }

    static std::int64_t CLAP_ABI read(const clap_istream_t *stream,
                                      void *destination,
                                      std::uint64_t size) noexcept {
        if (!stream || !stream->ctx || (!destination && size != 0))
            return -1;
        auto &self = *static_cast<StateInput *>(stream->ctx);
        if (self.offset >= self.sizeBytes)
            return 0;
        const auto count = std::min<std::uint64_t>(
            size, static_cast<std::uint64_t>(self.sizeBytes - self.offset));
        std::memcpy(destination, self.bytes + self.offset, static_cast<std::size_t>(count));
        self.offset += static_cast<std::size_t>(count);
        return static_cast<std::int64_t>(count);
    }

    const std::uint8_t *bytes = nullptr;
    std::size_t sizeBytes = 0;
    std::size_t offset = 0;
    clap_istream_t stream{};
};

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

std::array<std::uint8_t, 24> legacyState(double fineTune) noexcept {
    std::array<std::uint8_t, 24> bytes{};
    constexpr std::array<std::uint8_t, 8> magic{{'W', 'V', 'P', 'S', 'Y', 'N', 'T', 'H'}};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    storeU32Le(bytes.data() + 8, 1u);
    std::uint64_t bits = 0;
    std::memcpy(&bits, &fineTune, sizeof(bits));
    storeU64Le(bytes.data() + 12, bits);
    return bytes;
}

double phaseIncrement(std::int16_t key, double fineCents = 0.0) noexcept {
    const double semitones = static_cast<double>(key - 69) + fineCents / 100.0;
    return (440.0 * std::exp2(semitones / 12.0)) / kSampleRate;
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

bool matchesSineGain(const std::array<float, kFrames> &left,
                     const std::array<float, kFrames> &right,
                     double startPhase,
                     double increment,
                     std::uint32_t gainChangeFrame,
                     float beforeGain,
                     float afterGain) noexcept {
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        const float gain = frame < gainChangeFrame ? beforeGain : afterGain;
        const auto expected = static_cast<float>(
            std::sin(wrappedPhase(startPhase + static_cast<double>(frame) * increment) * kTwoPi) *
            gain);
        if (std::fabs(left[frame] - expected) > 1.0e-5f ||
            std::fabs(right[frame] - expected) > 1.0e-5f)
            return false;
    }
    return true;
}

bool getValue(const clap_plugin_t *plugin,
              const clap_plugin_params_t *params,
              clap_id id,
              double expected,
              double tolerance = 1.0e-5) noexcept {
    double value = 0.0;
    return params->get_value(plugin, id, &value) &&
           std::fabs(value - expected) <= tolerance;
}

bool flush(const clap_plugin_t *plugin,
           const clap_plugin_params_t *params,
           std::uint16_t type,
           clap_id id,
           double valueOrAmount,
           std::int32_t noteId = -1,
           std::int16_t portIndex = -1,
           std::int16_t channel = -1,
           std::int16_t key = -1) noexcept {
    SingleFlushEvent event(type,
                           id,
                           valueOrAmount,
                           noteId,
                           portIndex,
                           channel,
                           key);
    OutputEvents output;
    params->flush(plugin, &event.input, &output.output);
    return true;
}

const clap_plugin_t *createPlugin() noexcept {
    const auto *factory = webview_gui::examples::polysynth::polysynthFactory();
    if (!factory)
        return nullptr;
    const auto *plugin = factory->create_plugin(
        factory, &kHost, webview_gui::examples::polysynth::kPolySynthPluginId);
    if (!plugin)
        return nullptr;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return nullptr;
    }
    return plugin;
}

} // namespace

int main() {
    using namespace webview_gui::examples::polysynth;

    const auto *plugin = createPlugin();
    if (!plugin)
        return 1;

    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *remote = static_cast<const clap_plugin_remote_controls_t *>(
        plugin->get_extension(plugin, CLAP_EXT_REMOTE_CONTROLS));
    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!params || !params->count || !params->get_info || !params->get_value ||
        !params->value_to_text || !params->text_to_value || !params->flush ||
        !remote || !remote->count || !remote->get ||
        !state || !state->save || !state->load) {
        plugin->destroy(plugin);
        return 2;
    }

    if (params->count(plugin) != 2u) {
        std::fprintf(stderr, "Master Gain was not published as the second host parameter\n");
        plugin->destroy(plugin);
        return 3;
    }

    clap_param_info_t fineInfo{};
    clap_param_info_t masterInfo{};
    clap_param_info_t repeatedMasterInfo{};
    if (!params->get_info(plugin, 0u, &fineInfo) || fineInfo.id != kFineTuneId ||
        !params->get_info(plugin, 1u, &masterInfo) || masterInfo.id != kMasterGainId ||
        !masterInfo.cookie ||
        !params->get_info(plugin, 1u, &repeatedMasterInfo) ||
        repeatedMasterInfo.cookie != masterInfo.cookie ||
        masterInfo.flags != kGlobalModulatableFlags ||
        masterInfo.min_value != -60.0 || masterInfo.max_value != 12.0 ||
        masterInfo.default_value != 0.0 ||
        std::strcmp(masterInfo.name, "Master Gain") != 0 ||
        std::strcmp(masterInfo.module, "Output") != 0) {
        std::fprintf(stderr, "Master Gain CLAP metadata contract is incomplete\n");
        plugin->destroy(plugin);
        return 4;
    }

    if (!getValue(plugin, params, kMasterGainId, 0.0)) {
        plugin->destroy(plugin);
        return 5;
    }

    char text[32]{};
    double parsed = 0.0;
    if (!params->value_to_text(plugin, kMasterGainId, kHalfGainDb, text, sizeof(text)) ||
        text[0] == '\0' ||
        !params->text_to_value(plugin, kMasterGainId, text, &parsed) ||
        std::fabs(parsed - kHalfGainDb) > 1.0e-5) {
        std::fprintf(stderr, "Master Gain text conversion did not round-trip\n");
        plugin->destroy(plugin);
        return 6;
    }

    if (remote->count(plugin) != 2u) {
        std::fprintf(stderr, "Master Gain did not expand remote controls to a second page\n");
        plugin->destroy(plugin);
        return 7;
    }
    clap_remote_controls_page_t page{};
    if (!remote->get(plugin, 1u, &page) || page.page_id == CLAP_INVALID_ID ||
        page.is_for_preset || std::strcmp(page.section_name, "Output") != 0 ||
        std::strcmp(page.page_name, "Performance") != 0 ||
        page.param_ids[0] != kMasterGainId) {
        std::fprintf(stderr, "Master Gain remote-control page is incorrect\n");
        plugin->destroy(plugin);
        return 8;
    }
    for (std::size_t index = 1; index < CLAP_REMOTE_CONTROLS_COUNT; ++index) {
        if (page.param_ids[index] != CLAP_INVALID_ID) {
            plugin->destroy(plugin);
            return 9;
        }
    }

    if (!plugin->activate(plugin, kSampleRate, 1, 64) ||
        !plugin->start_processing(plugin)) {
        plugin->destroy(plugin);
        return 10;
    }

    InputEvents firstEvents;
    if (!firstEvents.pushNoteOn() ||
        !firstEvents.pushValue(4, kMasterGainId, kHalfGainDb)) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 11;
    }
    std::array<float, kFrames> firstLeft{};
    std::array<float, kFrames> firstRight{};
    const double increment = phaseIncrement(kKey);
    if (!processBlock(plugin, firstEvents, firstLeft, firstRight) ||
        !matchesSineGain(firstLeft,
                         firstRight,
                         0.25,
                         increment,
                         4,
                         1.0f,
                         0.5f)) {
        std::fprintf(stderr, "Master Gain PARAM_VALUE was not sample-accurate\n");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 12;
    }
    if (!getValue(plugin, params, kMasterGainId, kHalfGainDb)) {
        std::fprintf(stderr, "Master Gain PARAM_VALUE was not retained as the host base\n");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 13;
    }

    InputEvents modulationEvents;
    if (!modulationEvents.pushMod(0, kMasterGainId, -kHalfGainDb)) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 14;
    }
    std::array<float, kFrames> secondLeft{};
    std::array<float, kFrames> secondRight{};
    const double secondPhase = 0.25 + static_cast<double>(kFrames) * increment;
    if (!processBlock(plugin, modulationEvents, secondLeft, secondRight) ||
        !matchesSineGain(secondLeft,
                         secondRight,
                         secondPhase,
                         increment,
                         0,
                         1.0f,
                         1.0f) ||
        !getValue(plugin, params, kMasterGainId, kHalfGainDb)) {
        std::fprintf(stderr, "Master Gain modulation did not compose independently from the base\n");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 15;
    }

    InputEvents targetedEvents;
    if (!targetedEvents.pushValue(0, kMasterGainId, -60.0, kNoteId, 0, 0, kKey)) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 16;
    }
    std::array<float, kFrames> targetedLeft{};
    std::array<float, kFrames> targetedRight{};
    const double targetedPhase = secondPhase + static_cast<double>(kFrames) * increment;
    if (!processBlock(plugin, targetedEvents, targetedLeft, targetedRight) ||
        !matchesSineGain(targetedLeft,
                         targetedRight,
                         targetedPhase,
                         increment,
                         0,
                         1.0f,
                         1.0f) ||
        !getValue(plugin, params, kMasterGainId, kHalfGainDb)) {
        std::fprintf(stderr, "non-polyphonic Master Gain accepted a targeted per-note value\n");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 17;
    }

    plugin->stop_processing(plugin);
    flush(plugin, params, CLAP_EVENT_PARAM_VALUE, kMasterGainId, kQuarterGainDb);
    if (!getValue(plugin, params, kMasterGainId, kQuarterGainDb) ||
        !plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 18;
    }
    InputEvents noEvents;
    std::array<float, kFrames> flushedLeft{};
    std::array<float, kFrames> flushedRight{};
    const double flushedPhase = targetedPhase + static_cast<double>(kFrames) * increment;
    if (!processBlock(plugin, noEvents, flushedLeft, flushedRight) ||
        !matchesSineGain(flushedLeft,
                         flushedRight,
                         flushedPhase,
                         increment,
                         0,
                         0.5f,
                         0.5f)) {
        std::fprintf(stderr, "active params.flush Master Gain value did not apply at the next sample\n");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 19;
    }

    plugin->stop_processing(plugin);
    flush(plugin, params, CLAP_EVENT_PARAM_MOD, kMasterGainId, -kQuarterGainDb);
    flush(plugin,
          params,
          CLAP_EVENT_PARAM_VALUE,
          kMasterGainId,
          -60.0,
          kNoteId,
          0,
          0,
          kKey);
    if (!getValue(plugin, params, kMasterGainId, kQuarterGainDb) ||
        !plugin->start_processing(plugin)) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 20;
    }
    std::array<float, kFrames> flushModLeft{};
    std::array<float, kFrames> flushModRight{};
    const double flushModPhase = flushedPhase + static_cast<double>(kFrames) * increment;
    if (!processBlock(plugin, noEvents, flushModLeft, flushModRight) ||
        !matchesSineGain(flushModLeft,
                         flushModRight,
                         flushModPhase,
                         increment,
                         0,
                         1.0f,
                         1.0f)) {
        std::fprintf(stderr, "active params.flush Master Gain modulation or targeting is incorrect\n");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 21;
    }

    plugin->stop_processing(plugin);
    flush(plugin, params, CLAP_EVENT_PARAM_VALUE, kFineTuneId, 37.5);
    StateOutput saved;
    if (!state->save(plugin, &saved.stream) || saved.used != 24u ||
        loadU32Le(saved.bytes.data() + 8) != 2u) {
        std::fprintf(stderr, "Master Gain state did not advance to the version-2 payload\n");
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 22;
    }

    const auto *clone = createPlugin();
    if (!clone) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 23;
    }
    const auto *cloneParams = static_cast<const clap_plugin_params_t *>(
        clone->get_extension(clone, CLAP_EXT_PARAMS));
    const auto *cloneState = static_cast<const clap_plugin_state_t *>(
        clone->get_extension(clone, CLAP_EXT_STATE));
    StateInput savedInput(saved.bytes.data(), saved.used);
    if (!cloneParams || !cloneState ||
        !cloneState->load(clone, &savedInput.stream) ||
        !getValue(clone, cloneParams, kFineTuneId, 37.5) ||
        !getValue(clone, cloneParams, kMasterGainId, kQuarterGainDb) ||
        !clone->activate(clone, kSampleRate, 1, 64) ||
        !clone->start_processing(clone)) {
        clone->destroy(clone);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 24;
    }

    InputEvents cloneNote;
    cloneNote.pushNoteOn(0, 5101, kKey);
    std::array<float, kFrames> cloneLeft{};
    std::array<float, kFrames> cloneRight{};
    const double cloneIncrement = phaseIncrement(kKey, 37.5);
    if (!processBlock(clone, cloneNote, cloneLeft, cloneRight) ||
        !matchesSineGain(cloneLeft,
                         cloneRight,
                         0.25,
                         cloneIncrement,
                         0,
                         0.25f,
                         0.25f)) {
        std::fprintf(stderr, "state persisted ephemeral Master Gain modulation or missed the base\n");
        clone->stop_processing(clone);
        clone->deactivate(clone);
        clone->destroy(clone);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 25;
    }
    clone->stop_processing(clone);
    clone->deactivate(clone);
    clone->destroy(clone);

    const auto oldBytes = legacyState(-25.0);
    const auto *legacy = createPlugin();
    if (!legacy) {
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 26;
    }
    const auto *legacyParams = static_cast<const clap_plugin_params_t *>(
        legacy->get_extension(legacy, CLAP_EXT_PARAMS));
    const auto *legacyStateExt = static_cast<const clap_plugin_state_t *>(
        legacy->get_extension(legacy, CLAP_EXT_STATE));
    StateInput oldInput(oldBytes.data(), oldBytes.size());
    if (!legacyParams || !legacyStateExt ||
        !legacyStateExt->load(legacy, &oldInput.stream) ||
        !getValue(legacy, legacyParams, kFineTuneId, -25.0) ||
        !getValue(legacy, legacyParams, kMasterGainId, 0.0)) {
        std::fprintf(stderr, "version-1 state did not default Master Gain to 0 dB\n");
        legacy->destroy(legacy);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 27;
    }
    legacy->destroy(legacy);

    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 0;
}
