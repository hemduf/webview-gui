#include "polysynth_wclap_proxy.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using namespace webview_gui::examples::polysynth;
using namespace webview_gui::examples::polysynth::wclap;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, nullptr};
const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    "com.webview-gui.test.polysynth-wclap",
    "PolySynth WCLAP proxy contract",
    "webview-gui",
    "",
    "",
    "",
    "0",
    "test",
    kFeatures,
};

struct FakeInner {
    clap_plugin_t plugin{};
    clap_plugin_params_t params{};
    std::array<double, kParameterCount> values{};
    bool initialized = false;
    bool active = false;
    bool destroyed = false;
    std::uint32_t processCalls = 0u;
    std::uint32_t flushCalls = 0u;

    FakeInner() noexcept {
        for (std::size_t index = 0; index < kParameterSpecs.size(); ++index)
            values[index] = kParameterSpecs[index].defaultValue;

        params.count = paramsCount;
        params.get_info = paramsGetInfo;
        params.get_value = paramsGetValue;
        params.value_to_text = paramsValueToText;
        params.text_to_value = paramsTextToValue;
        params.flush = paramsFlush;

        plugin.desc = &kDescriptor;
        plugin.plugin_data = this;
        plugin.init = init;
        plugin.destroy = destroy;
        plugin.activate = activate;
        plugin.deactivate = deactivate;
        plugin.start_processing = startProcessing;
        plugin.stop_processing = stopProcessing;
        plugin.reset = reset;
        plugin.process = process;
        plugin.get_extension = getExtension;
        plugin.on_main_thread = nullptr;
    }

    static FakeInner *from(const clap_plugin_t *plugin) noexcept {
        return plugin ? static_cast<FakeInner *>(plugin->plugin_data) : nullptr;
    }

    static bool CLAP_ABI init(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        if (!self || self->initialized)
            return false;
        self->initialized = true;
        return true;
    }

    static void CLAP_ABI destroy(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        if (self)
            self->destroyed = true;
    }

    static bool CLAP_ABI activate(const clap_plugin_t *plugin,
                                  double,
                                  std::uint32_t,
                                  std::uint32_t) {
        auto *self = from(plugin);
        if (!self || !self->initialized || self->active)
            return false;
        self->active = true;
        return true;
    }

    static void CLAP_ABI deactivate(const clap_plugin_t *plugin) {
        auto *self = from(plugin);
        if (self)
            self->active = false;
    }

    static bool CLAP_ABI startProcessing(const clap_plugin_t *plugin) {
        const auto *self = from(plugin);
        return self && self->active;
    }

    static void CLAP_ABI stopProcessing(const clap_plugin_t *) {}
    static void CLAP_ABI reset(const clap_plugin_t *) {}

    static const void *CLAP_ABI getExtension(const clap_plugin_t *plugin, const char *id) {
        auto *self = from(plugin);
        if (!self || !self->initialized || !id)
            return nullptr;
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &self->params;
        return nullptr;
    }

    static std::uint32_t CLAP_ABI paramsCount(const clap_plugin_t *) {
        return static_cast<std::uint32_t>(kParameterCount);
    }

    static bool CLAP_ABI paramsGetInfo(const clap_plugin_t *,
                                       std::uint32_t index,
                                       clap_param_info_t *info) {
        const auto *spec = parameterSpecByIndex(index);
        if (!spec || !info)
            return false;
        *info = {};
        info->id = spec->id;
        info->flags = spec->flags;
        info->min_value = spec->minValue;
        info->max_value = spec->maxValue;
        info->default_value = spec->defaultValue;
        std::snprintf(info->name, sizeof(info->name), "%s", spec->name);
        std::snprintf(info->module, sizeof(info->module), "%s", spec->module);
        return true;
    }

    static bool CLAP_ABI paramsGetValue(const clap_plugin_t *plugin,
                                        clap_id paramId,
                                        double *value) {
        auto *self = from(plugin);
        const auto *spec = parameterSpecForId(paramId);
        if (!self || !spec || !value)
            return false;
        *value = self->values[static_cast<std::size_t>(spec->slot)];
        return true;
    }

    static bool CLAP_ABI paramsValueToText(const clap_plugin_t *,
                                           clap_id,
                                           double,
                                           char *,
                                           std::uint32_t) {
        return false;
    }

    static bool CLAP_ABI paramsTextToValue(const clap_plugin_t *,
                                           clap_id,
                                           const char *,
                                           double *) {
        return false;
    }

    static void applyInput(FakeInner &self, const clap_input_events_t *input) noexcept {
        if (!input || !input->size || !input->get)
            return;
        const auto count = input->size(input);
        for (std::uint32_t index = 0u; index < count; ++index) {
            const auto *header = input->get(input, index);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
                header->type != CLAP_EVENT_PARAM_VALUE ||
                header->size < sizeof(clap_event_param_value_t))
                continue;
            const auto &event = *reinterpret_cast<const clap_event_param_value_t *>(header);
            const auto *spec = parameterSpecForId(event.param_id);
            if (spec)
                self.values[static_cast<std::size_t>(spec->slot)] = event.value;
        }
    }

    static void CLAP_ABI paramsFlush(const clap_plugin_t *plugin,
                                     const clap_input_events_t *input,
                                     const clap_output_events_t *) {
        auto *self = from(plugin);
        if (!self)
            return;
        ++self->flushCalls;
        applyInput(*self, input);
    }

    static clap_process_status CLAP_ABI process(const clap_plugin_t *plugin,
                                                const clap_process_t *process) {
        auto *self = from(plugin);
        if (!self || !self->active || !process)
            return CLAP_PROCESS_ERROR;
        ++self->processCalls;
        applyInput(*self, process->in_events);
        if (process->audio_outputs && process->audio_outputs_count == 1u) {
            auto &output = process->audio_outputs[0];
            if (output.data32 && output.channel_count >= 2u && output.data32[0] && output.data32[1]) {
                for (std::uint32_t frame = 0u; frame < process->frames_count; ++frame) {
                    output.data32[0][frame] = 0.25f;
                    output.data32[1][frame] = -0.5f;
                }
            }
        }
        return CLAP_PROCESS_CONTINUE;
    }
};

struct HostState {
    clap_host_t host{};
    clap_host_params_t params{};
    ::webview_gui::clap_host_webview webview{};
    std::uint32_t flushRequests = 0u;
    std::uint32_t processRequests = 0u;
    std::uint32_t sends = 0u;
    std::array<std::uint8_t, 32> lastTelemetry{};

    HostState() noexcept {
        params.rescan = hostParamsRescan;
        params.clear = hostParamsClear;
        params.request_flush = hostParamsRequestFlush;
        webview.send = hostWebviewSend;

        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "wclap-contract";
        host.vendor = "webview-gui";
        host.url = "";
        host.version = "0";
        host.get_extension = hostGetExtension;
        host.request_restart = hostRequestRestart;
        host.request_process = hostRequestProcess;
        host.request_callback = hostRequestCallback;
    }

    static HostState *from(const clap_host_t *host) noexcept {
        return host ? static_cast<HostState *>(host->host_data) : nullptr;
    }

    static const void *CLAP_ABI hostGetExtension(const clap_host_t *host, const char *id) {
        auto *self = from(host);
        if (!self || !id)
            return nullptr;
        if (std::strcmp(id, ::webview_gui::CLAP_EXT_WEBVIEW) == 0)
            return &self->webview;
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &self->params;
        return nullptr;
    }

    static void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
    static void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

    static void CLAP_ABI hostRequestProcess(const clap_host_t *host) {
        auto *self = from(host);
        if (self)
            ++self->processRequests;
    }

    static void CLAP_ABI hostParamsRescan(const clap_host_t *, clap_param_rescan_flags) {}
    static void CLAP_ABI hostParamsClear(const clap_host_t *, clap_id, clap_param_clear_flags) {}

    static void CLAP_ABI hostParamsRequestFlush(const clap_host_t *host) {
        auto *self = from(host);
        if (self)
            ++self->flushRequests;
    }

    static bool CLAP_ABI hostWebviewSend(const clap_host_t *host,
                                         const void *buffer,
                                         std::uint32_t size) {
        auto *self = from(host);
        if (!self || (!buffer && size != 0u))
            return false;
        ++self->sends;
        if (size == self->lastTelemetry.size()) {
            const auto *bytes = static_cast<const std::uint8_t *>(buffer);
            if (bytes[0] == 'W' && bytes[1] == 'V' && bytes[2] == 'T' && bytes[3] == '1')
                std::memcpy(self->lastTelemetry.data(), bytes, size);
        }
        return true;
    }
};

struct BufferStream {
    clap_ostream_t stream{};
    std::array<char, 32768> data{};
    std::size_t size = 0u;

    BufferStream() noexcept {
        stream.ctx = this;
        stream.write = write;
    }

    static std::int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                       const void *buffer,
                                       std::uint64_t length) {
        if (!stream || !stream->ctx || (!buffer && length != 0u))
            return -1;
        auto &self = *static_cast<BufferStream *>(stream->ctx);
        if (length > self.data.size() - self.size)
            return -1;
        std::memcpy(self.data.data() + self.size, buffer, static_cast<std::size_t>(length));
        self.size += static_cast<std::size_t>(length);
        return static_cast<std::int64_t>(length);
    }
};

struct OutputCollector {
    clap_output_events_t output{};
    std::array<std::uint16_t, 32> types{};
    std::array<clap_id, 32> ids{};
    std::array<double, 32> values{};
    std::uint32_t count = 0u;

    OutputCollector() noexcept {
        output.ctx = this;
        output.try_push = tryPush;
    }

    void reset() noexcept { count = 0u; }

    static bool CLAP_ABI tryPush(const clap_output_events_t *events,
                                 const clap_event_header_t *header) {
        if (!events || !events->ctx || !header)
            return false;
        auto &self = *static_cast<OutputCollector *>(events->ctx);
        if (self.count >= self.types.size())
            return false;
        const auto index = self.count++;
        self.types[index] = header->type;
        self.ids[index] = CLAP_INVALID_ID;
        self.values[index] = 0.0;
        if (header->type == CLAP_EVENT_PARAM_VALUE &&
            header->size >= sizeof(clap_event_param_value_t)) {
            const auto &event = *reinterpret_cast<const clap_event_param_value_t *>(header);
            self.ids[index] = event.param_id;
            self.values[index] = event.value;
        } else if ((header->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
                    header->type == CLAP_EVENT_PARAM_GESTURE_END) &&
                   header->size >= sizeof(clap_event_param_gesture_t)) {
            const auto &event = *reinterpret_cast<const clap_event_param_gesture_t *>(header);
            self.ids[index] = event.param_id;
        }
        return true;
    }
};

struct InputList {
    clap_input_events_t input{};
    std::array<const clap_event_header_t *, 3> events{};

    InputList() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *input) {
        return input && input->ctx ? 3u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *input,
                                                    std::uint32_t index) {
        if (!input || !input->ctx || index >= 3u)
            return nullptr;
        return static_cast<const InputList *>(input->ctx)->events[index];
    }
};

std::array<std::uint8_t, 24> editMessage(std::uint8_t kind,
                                         clap_id paramId,
                                         double value = 0.0) noexcept {
    std::array<std::uint8_t, 24> bytes{};
    bytes[0] = 'W';
    bytes[1] = 'V';
    bytes[2] = 'P';
    bytes[3] = '1';
    bytes[4] = kind;
    detail::storeU32Le(bytes.data() + 8u, paramId);
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    detail::storeU64Le(bytes.data() + 16u, bits);
    return bytes;
}

bool near(float a, float b) noexcept {
    return std::fabs(a - b) < 1.0e-6f;
}

} // namespace

int main() {
    FakeInner inner;
    HostState host;
    const auto *plugin = wrapPolySynthWclapPlugin(&inner.plugin, &host.host);
    if (!plugin)
        return 1;

    const auto *preInitWebview = static_cast<const ::webview_gui::clap_plugin_webview *>(
        plugin->get_extension(plugin, ::webview_gui::CLAP_EXT_WEBVIEW));
    if (!preInitWebview || preInitWebview->get_uri(plugin, nullptr, 0u) != -1)
        return 2;

    if (!plugin->init(plugin))
        return 3;

    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto *webview = static_cast<const ::webview_gui::clap_plugin_webview *>(
        plugin->get_extension(plugin, ::webview_gui::CLAP_EXT_WEBVIEW));
    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!gui || !webview || !params)
        return 4;

    if (!gui->is_api_supported(plugin, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false) ||
        !gui->create(plugin, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false))
        return 5;

    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    if (!gui->get_size(plugin, &width, &height) || width != 760u || height != 580u)
        return 6;

    char uri[64]{};
    if (webview->get_uri(plugin, uri, sizeof(uri)) <= 0 ||
        std::strcmp(uri, "/index.html") != 0)
        return 7;

    BufferStream html;
    char mime[64]{};
    if (!webview->get_resource(plugin,
                               "/index.html",
                               mime,
                               sizeof(mime),
                               &html.stream) ||
        std::strcmp(mime, "text/html; charset=utf-8") != 0 || html.size == 0u)
        return 8;

    const std::array<std::uint8_t, 4> sync{{'W','V','S','1'}};
    if (!webview->receive(plugin, sync.data(), sync.size()) || host.sends != 14u)
        return 9;

    const auto beginGain = editMessage(1u, 1000u);
    const auto valueGain = editMessage(2u, 1000u, 6.0);
    const auto endGain = editMessage(3u, 1000u);
    if (!webview->receive(plugin, beginGain.data(), beginGain.size()) ||
        !webview->receive(plugin, valueGain.data(), valueGain.size()) ||
        !webview->receive(plugin, endGain.data(), endGain.size()) ||
        host.flushRequests < 3u)
        return 10;

    OutputCollector output;
    params->flush(plugin, nullptr, &output.output);
    if (output.count != 3u || output.types[0] != CLAP_EVENT_PARAM_GESTURE_BEGIN ||
        output.types[1] != CLAP_EVENT_PARAM_VALUE ||
        output.types[2] != CLAP_EVENT_PARAM_GESTURE_END ||
        output.ids[1] != 1000u || output.values[1] != 6.0 ||
        inner.values[static_cast<std::size_t>(ParameterSlot::MasterGain)] != 6.0)
        return 11;

    if (!plugin->activate(plugin, 48000.0, 1u, 64u) || !plugin->start_processing(plugin))
        return 12;

    const auto beginCutoff = editMessage(1u, 1004u);
    const auto valueCutoff = editMessage(2u, 1004u, 4000.0);
    const auto endCutoff = editMessage(3u, 1004u);
    if (!webview->receive(plugin, beginCutoff.data(), beginCutoff.size()) ||
        !webview->receive(plugin, valueCutoff.data(), valueCutoff.size()) ||
        !webview->receive(plugin, endCutoff.data(), endCutoff.size()) ||
        host.processRequests < 3u)
        return 13;

    clap_event_note_t noteOn{};
    noteOn.header.size = sizeof(noteOn);
    noteOn.header.time = 0u;
    noteOn.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    noteOn.header.type = CLAP_EVENT_NOTE_ON;
    noteOn.note_id = 42;
    noteOn.port_index = 0;
    noteOn.channel = 0;
    noteOn.key = 60;
    noteOn.velocity = 1.0;

    clap_event_param_mod_t modulation{};
    modulation.header.size = sizeof(modulation);
    modulation.header.time = 0u;
    modulation.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    modulation.header.type = CLAP_EVENT_PARAM_MOD;
    modulation.param_id = 1004u;
    modulation.note_id = 42;
    modulation.port_index = 0;
    modulation.channel = 0;
    modulation.key = 60;
    modulation.amount = 0.25;

    clap_event_note_expression_t expression{};
    expression.header.size = sizeof(expression);
    expression.header.time = 0u;
    expression.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    expression.header.type = CLAP_EVENT_NOTE_EXPRESSION;
    expression.expression_id = CLAP_NOTE_EXPRESSION_PRESSURE;
    expression.note_id = 42;
    expression.port_index = 0;
    expression.channel = 0;
    expression.key = 60;
    expression.value = 0.7;

    InputList input;
    input.events = {&noteOn.header, &modulation.header, &expression.header};
    std::array<float, 8> left{};
    std::array<float, 8> right{};
    float *channels[] = {left.data(), right.data()};
    clap_audio_buffer_t audio{};
    audio.data32 = channels;
    audio.channel_count = 2u;
    output.reset();

    clap_process_t process{};
    process.frames_count = 8u;
    process.in_events = &input.input;
    process.out_events = &output.output;
    process.audio_outputs = &audio;
    process.audio_outputs_count = 1u;
    if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR || output.count != 3u ||
        inner.values[static_cast<std::size_t>(ParameterSlot::FilterCutoff)] != 4000.0)
        return 14;

    const auto sendsBeforeTelemetry = host.sends;
    if (!webview->receive(plugin, sync.data(), sync.size()) ||
        host.sends != sendsBeforeTelemetry + 14u)
        return 15;

    const auto &telemetry = host.lastTelemetry;
    if (detail::loadU32Le(telemetry.data() + 4u) != 1u ||
        !near(detail::floatFromBits(detail::loadU32Le(telemetry.data() + 8u)), 0.25f) ||
        !near(detail::floatFromBits(detail::loadU32Le(telemetry.data() + 12u)), 0.5f) ||
        detail::loadU32Le(telemetry.data() + 16u) != 1004u ||
        !near(detail::floatFromBits(detail::loadU32Le(telemetry.data() + 20u)), 0.25f) ||
        detail::loadU32Le(telemetry.data() + 24u) != CLAP_NOTE_EXPRESSION_PRESSURE ||
        !near(detail::floatFromBits(detail::loadU32Le(telemetry.data() + 28u)), 0.7f))
        return 16;

    gui->destroy(plugin);
    if (webview->receive(plugin, sync.data(), sync.size()))
        return 17;

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    if (!inner.destroyed)
        return 18;

    return 0;
}
