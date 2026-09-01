#include "polysynth_parameters.h"
#include "polysynth_plugin.h"
#include "polysynth_voice_allocator.h"
#include "wclap/polysynth_wclap_proxy.h"

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/voice-info.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
using namespace webview_gui::examples::polysynth;
namespace bridge = webview_gui::examples::polysynth::wclap;
namespace detail = webview_gui::examples::polysynth::wclap::detail;

struct Host {
    clap_host_t host{};
    clap_host_params_t params{};
    ::webview_gui::clap_host_webview webview{};
    bool exposeWebview = false;
    std::uint32_t flushRequests = 0u;
    std::uint32_t processRequests = 0u;
    std::uint32_t sends = 0u;
    bool sawMasterBase = false;
    double masterBase = 0.0;
    bool sawTelemetry = false;
    std::uint32_t lastModParam = CLAP_INVALID_ID;
    float lastModAmount = 0.0f;

    Host(const char *name, bool withWebview) noexcept : exposeWebview(withWebview) {
        params.rescan = paramsRescan;
        params.clear = paramsClear;
        params.request_flush = paramsRequestFlush;
        webview.send = webviewSend;
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = name;
        host.vendor = "webview-gui";
        host.url = "https://github.com/hemduf/webview-gui";
        host.version = "0.1.0";
        host.get_extension = getExtension;
        host.request_restart = requestRestart;
        host.request_process = requestProcess;
        host.request_callback = requestCallback;
    }

    static Host *from(const clap_host_t *host) noexcept {
        return host ? static_cast<Host *>(host->host_data) : nullptr;
    }

    static const void *CLAP_ABI getExtension(const clap_host_t *host, const char *id) {
        auto *self = from(host);
        if (!self || !id)
            return nullptr;
        if (self->exposeWebview && std::strcmp(id, ::webview_gui::CLAP_EXT_WEBVIEW) == 0)
            return &self->webview;
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &self->params;
        return nullptr;
    }

    static void CLAP_ABI requestRestart(const clap_host_t *) {}
    static void CLAP_ABI requestCallback(const clap_host_t *) {}
    static void CLAP_ABI requestProcess(const clap_host_t *host) {
        if (auto *self = from(host))
            ++self->processRequests;
    }
    static void CLAP_ABI paramsRescan(const clap_host_t *, clap_param_rescan_flags) {}
    static void CLAP_ABI paramsClear(const clap_host_t *, clap_id, clap_param_clear_flags) {}
    static void CLAP_ABI paramsRequestFlush(const clap_host_t *host) {
        if (auto *self = from(host))
            ++self->flushRequests;
    }

    static bool CLAP_ABI webviewSend(const clap_host_t *host,
                                     const void *buffer,
                                     std::uint32_t size) {
        auto *self = from(host);
        if (!self || (!buffer && size != 0u))
            return false;
        ++self->sends;
        const auto *bytes = static_cast<const std::uint8_t *>(buffer);
        if (size == 16u && bytes[0] == 'W' && bytes[1] == 'V' && bytes[2] == 'B' &&
            bytes[3] == '1' && detail::loadU32Le(bytes + 4u) == kFirstParameterId) {
            const auto bits = detail::loadU64Le(bytes + 8u);
            std::memcpy(&self->masterBase, &bits, sizeof(self->masterBase));
            self->sawMasterBase = true;
        } else if (size == 32u && bytes[0] == 'W' && bytes[1] == 'V' && bytes[2] == 'T' &&
                   bytes[3] == '1') {
            self->lastModParam = detail::loadU32Le(bytes + 16u);
            self->lastModAmount = detail::floatFromBits(detail::loadU32Le(bytes + 20u));
            self->sawTelemetry = true;
        }
        return true;
    }
};

struct BufferStream {
    clap_ostream_t stream{};
    std::array<std::uint8_t, 32768> bytes{};
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
        if (length > self.bytes.size() - self.size)
            return -1;
        if (length)
            std::memcpy(self.bytes.data() + self.size, buffer, static_cast<std::size_t>(length));
        self.size += static_cast<std::size_t>(length);
        return static_cast<std::int64_t>(length);
    }
};

struct OutputCollector {
    clap_output_events_t output{};
    std::array<std::uint16_t, 128> types{};
    std::array<clap_id, 128> ids{};
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
        const auto i = self.count++;
        self.types[i] = header->type;
        self.ids[i] = CLAP_INVALID_ID;
        if (header->type == CLAP_EVENT_PARAM_VALUE && header->size >= sizeof(clap_event_param_value_t))
            self.ids[i] = reinterpret_cast<const clap_event_param_value_t *>(header)->param_id;
        else if ((header->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
                  header->type == CLAP_EVENT_PARAM_GESTURE_END) &&
                 header->size >= sizeof(clap_event_param_gesture_t))
            self.ids[i] = reinterpret_cast<const clap_event_param_gesture_t *>(header)->param_id;
        return true;
    }
};

struct SingleInput {
    clap_input_events_t input{};
    const clap_event_header_t *event = nullptr;
    explicit SingleInput(const clap_event_header_t *header) noexcept : event(header) {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }
    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) {
        return events && events->ctx ? 1u : 0u;
    }
    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) {
        return events && events->ctx && index == 0u
                   ? static_cast<const SingleInput *>(events->ctx)->event
                   : nullptr;
    }
};

const char *nativeApi() noexcept {
#if defined(__APPLE__)
    return CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32) || defined(_WIN64)
    return CLAP_WINDOW_API_WIN32;
#elif defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(__wasm__) && \
      !defined(__wasm32__) && !defined(__wasm64__)
    return CLAP_WINDOW_API_X11;
#else
    return nullptr;
#endif
}

const clap_plugin_t *create(const clap_plugin_factory_t *factory, Host &host) noexcept {
    const auto *inner = factory && factory->create_plugin
                            ? factory->create_plugin(factory, &host.host, kPolySynthPluginId)
                            : nullptr;
    if (!inner)
        return nullptr;
    const auto *outer = bridge::wrapPolySynthWclapPlugin(inner, &host.host);
    if (!outer && inner->destroy)
        inner->destroy(inner);
    return outer;
}

std::array<std::uint8_t, detail::UiParameterQueue::kMessageSize>
edit(std::uint8_t kind, clap_id paramId, double value = 0.0) noexcept {
    std::array<std::uint8_t, detail::UiParameterQueue::kMessageSize> bytes{};
    bytes[0] = 'W'; bytes[1] = 'V'; bytes[2] = 'P'; bytes[3] = '1'; bytes[4] = kind;
    detail::storeU32Le(bytes.data() + 8u, paramId);
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    detail::storeU64Le(bytes.data() + 16u, bits);
    return bytes;
}

clap_event_param_value_t baseEvent(clap_id id, double value) noexcept {
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = id;
    event.note_id = -1; event.port_index = -1; event.channel = -1; event.key = -1;
    event.value = value;
    return event;
}

clap_event_param_mod_t modEvent(clap_id id, double amount) noexcept {
    clap_event_param_mod_t event{};
    event.header.size = sizeof(event);
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_MOD;
    event.param_id = id;
    event.note_id = -1; event.port_index = -1; event.channel = -1; event.key = -1;
    event.amount = amount;
    return event;
}

int testNative(const clap_plugin_factory_t *factory) {
    Host host{"polysynth-native", false};
    const auto *plugin = create(factory, host);
    if (!plugin) return 1;
    if (!plugin->init(plugin)) { plugin->destroy(plugin); return 2; }

    const auto *voice = static_cast<const clap_plugin_voice_info_t *>(plugin->get_extension(plugin, CLAP_EXT_VOICE_INFO));
    const auto *gui = static_cast<const clap_plugin_gui_t *>(plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto *webview = static_cast<const clap_plugin_webview_t *>(plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
    const auto *api = nativeApi();
    if (!voice || !voice->get || !gui || !webview || !api ||
        !gui->is_api_supported(plugin, api, false) || gui->is_api_supported(plugin, api, true) ||
        gui->is_api_supported(plugin, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false)) {
        plugin->destroy(plugin); return 3;
    }
    const char *preferred = nullptr;
    bool floating = true;
    if (!gui->get_preferred_api(plugin, &preferred, &floating) || !preferred ||
        std::strcmp(preferred, api) != 0 || floating) {
        plugin->destroy(plugin); return 4;
    }

    char uri[64]{};
    if (webview->get_uri(plugin, uri, sizeof(uri)) <= 0 || std::strcmp(uri, "/index.html") != 0) {
        plugin->destroy(plugin); return 5;
    }
    BufferStream html;
    char mime[64]{};
    if (!webview->get_resource(plugin, "/index.html", mime, sizeof(mime), &html.stream) ||
        std::strcmp(mime, "text/html; charset=utf-8") != 0 || html.size == 0u) {
        plugin->destroy(plugin); return 6;
    }
    BufferStream script;
    std::memset(mime, 0, sizeof(mime));
    if (!webview->get_resource(plugin, "/polysynth.js", mime, sizeof(mime), &script.stream) ||
        std::strcmp(mime, "text/javascript; charset=utf-8") != 0 || script.size == 0u) {
        plugin->destroy(plugin); return 7;
    }

    if (!plugin->activate(plugin, 48000.0, 1u, 64u)) { plugin->destroy(plugin); return 8; }
    clap_voice_info_t info{};
    const bool valid = voice->get(plugin, &info) &&
                       info.voice_count == kPolySynthDefaultVoiceCount &&
                       info.voice_capacity == VoiceAllocator::kMaximumVoices &&
                       (info.flags & CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES) != 0u &&
                       !voice->get(plugin, nullptr);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return valid ? 0 : 9;
}

int testHostedWebview(const clap_plugin_factory_t *factory) {
    Host hostA{"polysynth-A", true};
    Host hostB{"polysynth-B", true};
    const auto *a = create(factory, hostA);
    const auto *b = create(factory, hostB);
    if (!a || !b) { if (a) a->destroy(a); if (b) b->destroy(b); return 1; }
    if (!a->init(a) || !b->init(b)) { a->destroy(a); b->destroy(b); return 2; }

    const auto *guiA = static_cast<const clap_plugin_gui_t *>(a->get_extension(a, CLAP_EXT_GUI));
    const auto *guiB = static_cast<const clap_plugin_gui_t *>(b->get_extension(b, CLAP_EXT_GUI));
    const auto *wvA = static_cast<const clap_plugin_webview_t *>(a->get_extension(a, CLAP_EXT_WEBVIEW));
    const auto *wvB = static_cast<const clap_plugin_webview_t *>(b->get_extension(b, CLAP_EXT_WEBVIEW));
    const auto *paramsA = static_cast<const clap_plugin_params_t *>(a->get_extension(a, CLAP_EXT_PARAMS));
    const auto *paramsB = static_cast<const clap_plugin_params_t *>(b->get_extension(b, CLAP_EXT_PARAMS));
    if (!guiA || !guiB || !wvA || !wvB || !paramsA || !paramsB ||
        !guiA->create(a, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false) ||
        !guiB->create(b, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false)) {
        a->destroy(a); b->destroy(b); return 3;
    }

    const std::array<std::uint8_t, 4> sync{{'W','V','S','1'}};
    if (!wvA->receive(a, sync.data(), sync.size()) || hostA.sends != 14u || hostB.sends != 0u ||
        !hostA.sawMasterBase || hostA.masterBase != 0.0) {
        a->destroy(a); b->destroy(b); return 4;
    }

    const auto beginGain = edit(1u, kFirstParameterId);
    const auto valueGain = edit(2u, kFirstParameterId, 6.0);
    const auto endGain = edit(3u, kFirstParameterId);
    if (!wvA->receive(a, beginGain.data(), beginGain.size()) ||
        !wvA->receive(a, valueGain.data(), valueGain.size()) ||
        !wvA->receive(a, endGain.data(), endGain.size()) ||
        hostA.flushRequests < 3u || hostB.flushRequests != 0u) {
        a->destroy(a); b->destroy(b); return 5;
    }

    OutputCollector output;
    paramsA->flush(a, nullptr, &output.output);
    double gainA = 0.0, gainB = 0.0;
    if (output.count != 3u || output.types[0] != CLAP_EVENT_PARAM_GESTURE_BEGIN ||
        output.types[1] != CLAP_EVENT_PARAM_VALUE || output.types[2] != CLAP_EVENT_PARAM_GESTURE_END ||
        output.ids[0] != kFirstParameterId || output.ids[1] != kFirstParameterId || output.ids[2] != kFirstParameterId ||
        !paramsA->get_value(a, kFirstParameterId, &gainA) || gainA != 6.0 ||
        !paramsB->get_value(b, kFirstParameterId, &gainB) || gainB != 0.0) {
        a->destroy(a); b->destroy(b); return 6;
    }

    auto hostUpdate = baseEvent(kFirstParameterId, -3.0);
    SingleInput hostInput{&hostUpdate.header};
    output.reset();
    paramsB->flush(b, &hostInput.input, &output.output);
    hostA.sawMasterBase = hostB.sawMasterBase = false;
    const auto sendsA = hostA.sends, sendsB = hostB.sends;
    if (!wvA->receive(a, sync.data(), sync.size()) || !wvB->receive(b, sync.data(), sync.size()) ||
        hostA.sends != sendsA + 14u || hostB.sends != sendsB + 14u ||
        !hostA.sawMasterBase || !hostB.sawMasterBase || hostA.masterBase != 6.0 || hostB.masterBase != -3.0) {
        a->destroy(a); b->destroy(b); return 7;
    }

    constexpr clap_id cutoffId = kFirstParameterId + 4u;
    const auto beginCutoff = edit(1u, cutoffId);
    const auto valueCutoff = edit(2u, cutoffId, 7000.0);
    const auto endCutoff = edit(3u, cutoffId);
    if (!wvB->receive(b, beginCutoff.data(), beginCutoff.size())) { a->destroy(a); b->destroy(b); return 8; }
    std::uint32_t accepted = 0u;
    bool rejected = false;
    for (std::uint32_t i = 0; i < detail::UiParameterQueue::kCapacity * 2u; ++i) {
        if (!wvB->receive(b, valueCutoff.data(), valueCutoff.size())) { rejected = true; break; }
        ++accepted;
    }
    if (!rejected || accepted == 0u || !wvB->receive(b, endCutoff.data(), endCutoff.size())) {
        a->destroy(a); b->destroy(b); return 9;
    }
    output.reset();
    paramsB->flush(b, nullptr, &output.output);
    if (output.count != accepted + 2u || output.types[0] != CLAP_EVENT_PARAM_GESTURE_BEGIN ||
        output.types[output.count - 1u] != CLAP_EVENT_PARAM_GESTURE_END ||
        output.ids[0] != cutoffId || output.ids[output.count - 1u] != cutoffId) {
        a->destroy(a); b->destroy(b); return 10;
    }

    // RT->UI is a capacity-one atomic snapshot: multiple audio blocks coalesce
    // without blocking, and the next UI sync observes the latest modulation.
    if (!b->activate(b, 48000.0, 1u, 8u) || !b->start_processing(b)) {
        a->destroy(a); b->destroy(b); return 11;
    }
    std::array<float, 8> left{}, right{};
    float *channels[] = {left.data(), right.data()};
    clap_audio_buffer_t audio{};
    audio.data32 = channels;
    audio.channel_count = 2u;
    clap_process_t process{};
    process.frames_count = 8u;
    process.out_events = &output.output;
    process.audio_outputs = &audio;
    process.audio_outputs_count = 1u;
    auto mod1 = modEvent(cutoffId, 0.25);
    auto mod2 = modEvent(cutoffId, 0.5);
    SingleInput input1{&mod1.header};
    SingleInput input2{&mod2.header};
    output.reset();
    process.in_events = &input1.input;
    if (b->process(b, &process) == CLAP_PROCESS_ERROR) { b->stop_processing(b); b->deactivate(b); a->destroy(a); b->destroy(b); return 12; }
    output.reset();
    process.in_events = &input2.input;
    if (b->process(b, &process) == CLAP_PROCESS_ERROR) { b->stop_processing(b); b->deactivate(b); a->destroy(a); b->destroy(b); return 13; }
    b->stop_processing(b);
    b->deactivate(b);

    hostB.sawTelemetry = false;
    double cutoffBase = 0.0;
    if (!wvB->receive(b, sync.data(), sync.size()) || !hostB.sawTelemetry ||
        hostB.lastModParam != cutoffId || hostB.lastModAmount != 0.5f ||
        !paramsB->get_value(b, cutoffId, &cutoffBase) || cutoffBase != 7000.0) {
        a->destroy(a); b->destroy(b); return 14;
    }

    guiA->destroy(a);
    const auto bBefore = hostB.sends;
    if (wvA->receive(a, sync.data(), sync.size()) || !wvB->receive(b, sync.data(), sync.size()) ||
        hostB.sends != bBefore + 14u) {
        a->destroy(a); b->destroy(b); return 15;
    }
    guiB->destroy(b);
    a->destroy(a);
    b->destroy(b);
    return 0;
}
} // namespace

int main() {
    const auto *factory = webview_gui::examples::polysynth::polysynthFactory();
    if (!factory)
        return 1;
    const auto nativeResult = testNative(factory);
    if (nativeResult != 0)
        return 10 + nativeResult;
    const auto hostedResult = testHostedWebview(factory);
    return hostedResult == 0 ? 0 : 100 + hostedResult;
}
