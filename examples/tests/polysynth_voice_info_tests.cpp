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
namespace proxy = webview_gui::examples::polysynth::wclap;
namespace proxy_detail = webview_gui::examples::polysynth::wclap::detail;

const void *CLAP_ABI noHostExtension(const clap_host_t *, const char *) {
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kNativeHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui PolySynth native GUI tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    noHostExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

const char *expectedNativeApi() noexcept {
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
        if (length != 0u)
            std::memcpy(self.bytes.data() + self.size,
                        buffer,
                        static_cast<std::size_t>(length));
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
        const auto index = self.count++;
        self.types[index] = header->type;
        self.ids[index] = CLAP_INVALID_ID;
        if (header->type == CLAP_EVENT_PARAM_VALUE &&
            header->size >= sizeof(clap_event_param_value_t)) {
            self.ids[index] = reinterpret_cast<const clap_event_param_value_t *>(header)->param_id;
        } else if ((header->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
                    header->type == CLAP_EVENT_PARAM_GESTURE_END) &&
                   header->size >= sizeof(clap_event_param_gesture_t)) {
            self.ids[index] = reinterpret_cast<const clap_event_param_gesture_t *>(header)->param_id;
        }
        return true;
    }
};

struct SingleInput {
    clap_input_events_t input{};
    const clap_event_header_t *event = nullptr;

    explicit SingleInput(const clap_event_header_t *newEvent) noexcept : event(newEvent) {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) {
        return events && events->ctx ? 1u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) {
        if (!events || !events->ctx || index != 0u)
            return nullptr;
        return static_cast<const SingleInput *>(events->ctx)->event;
    }
};

struct WebviewHostState {
    clap_host_t host{};
    clap_host_params_t params{};
    ::webview_gui::clap_host_webview webview{};
    std::uint32_t flushRequests = 0u;
    std::uint32_t processRequests = 0u;
    std::uint32_t sends = 0u;
    bool sawMasterBase = false;
    double masterBase = 0.0;

    explicit WebviewHostState(const char *name) noexcept {
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

    static WebviewHostState *from(const clap_host_t *host) noexcept {
        return host ? static_cast<WebviewHostState *>(host->host_data) : nullptr;
    }

    static const void *CLAP_ABI getExtension(const clap_host_t *host, const char *id) {
        auto *self = from(host);
        if (!self || !id)
            return nullptr;
        if (std::strcmp(id, ::webview_gui::CLAP_EXT_WEBVIEW) == 0)
            return &self->webview;
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &self->params;
        return nullptr;
    }

    static void CLAP_ABI requestRestart(const clap_host_t *) {}
    static void CLAP_ABI requestCallback(const clap_host_t *) {}

    static void CLAP_ABI requestProcess(const clap_host_t *host) {
        auto *self = from(host);
        if (self)
            ++self->processRequests;
    }

    static void CLAP_ABI paramsRescan(const clap_host_t *, clap_param_rescan_flags) {}
    static void CLAP_ABI paramsClear(const clap_host_t *, clap_id, clap_param_clear_flags) {}

    static void CLAP_ABI paramsRequestFlush(const clap_host_t *host) {
        auto *self = from(host);
        if (self)
            ++self->flushRequests;
    }

    static bool CLAP_ABI webviewSend(const clap_host_t *host,
                                     const void *buffer,
                                     std::uint32_t size) {
        auto *self = from(host);
        if (!self || (!buffer && size != 0u))
            return false;
        ++self->sends;
        if (size == 16u) {
            const auto *bytes = static_cast<const std::uint8_t *>(buffer);
            if (bytes[0] == 'W' && bytes[1] == 'V' && bytes[2] == 'B' && bytes[3] == '1' &&
                proxy_detail::loadU32Le(bytes + 4u) == kFirstParameterId) {
                const auto bits = proxy_detail::loadU64Le(bytes + 8u);
                std::memcpy(&self->masterBase, &bits, sizeof(self->masterBase));
                self->sawMasterBase = true;
            }
        }
        return true;
    }
};

std::array<std::uint8_t, proxy_detail::UiParameterQueue::kMessageSize>
editMessage(std::uint8_t kind, clap_id paramId, double value = 0.0) noexcept {
    std::array<std::uint8_t, proxy_detail::UiParameterQueue::kMessageSize> bytes{};
    bytes[0] = 'W';
    bytes[1] = 'V';
    bytes[2] = 'P';
    bytes[3] = '1';
    bytes[4] = kind;
    proxy_detail::storeU32Le(bytes.data() + 8u, paramId);
    std::uint64_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    proxy_detail::storeU64Le(bytes.data() + 16u, bits);
    return bytes;
}

clap_event_param_value_t hostValueEvent(clap_id paramId, double value) noexcept {
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0u;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = paramId;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    return event;
}

const clap_plugin_t *createWrapped(const clap_plugin_factory_t *factory,
                                   const clap_host_t *host) noexcept {
    if (!factory || !factory->create_plugin)
        return nullptr;
    const auto *inner = factory->create_plugin(factory, host, kPolySynthPluginId);
    if (!inner)
        return nullptr;
    const auto *outer = proxy::wrapPolySynthWclapPlugin(inner, host);
    if (!outer && inner->destroy)
        inner->destroy(inner);
    return outer;
}

} // namespace

int main() {
    const auto *factory = polysynthFactory();
    if (!factory)
        return 1;

    // Native CLAP contract: the shared proxy exposes the OS-native GUI path,
    // bundled resources and the original instrument extension surface.
    const auto *nativePlugin = createWrapped(factory, &kNativeHost);
    if (!nativePlugin)
        return 2;
    if (!nativePlugin->init(nativePlugin)) {
        nativePlugin->destroy(nativePlugin);
        return 3;
    }

    const auto *voiceInfo = static_cast<const clap_plugin_voice_info_t *>(
        nativePlugin->get_extension(nativePlugin, CLAP_EXT_VOICE_INFO));
    const auto *nativeGui = static_cast<const clap_plugin_gui_t *>(
        nativePlugin->get_extension(nativePlugin, CLAP_EXT_GUI));
    const auto *nativeWebview = static_cast<const clap_plugin_webview_t *>(
        nativePlugin->get_extension(nativePlugin, CLAP_EXT_WEBVIEW));
    if (!voiceInfo || !voiceInfo->get || !nativeGui || !nativeGui->is_api_supported ||
        !nativeGui->get_preferred_api || !nativeWebview || !nativeWebview->get_uri ||
        !nativeWebview->get_resource || !nativeWebview->receive) {
        nativePlugin->destroy(nativePlugin);
        return 4;
    }

    const char *nativeApi = expectedNativeApi();
    if (!nativeApi || !nativeGui->is_api_supported(nativePlugin, nativeApi, false) ||
        nativeGui->is_api_supported(nativePlugin, nativeApi, true) ||
        nativeGui->is_api_supported(nativePlugin,
                                     ::webview_gui::CLAP_WINDOW_API_WEBVIEW,
                                     false)) {
        nativePlugin->destroy(nativePlugin);
        return 5;
    }

    const char *preferredApi = nullptr;
    bool preferredFloating = true;
    if (!nativeGui->get_preferred_api(nativePlugin, &preferredApi, &preferredFloating) ||
        !preferredApi || std::strcmp(preferredApi, nativeApi) != 0 || preferredFloating) {
        nativePlugin->destroy(nativePlugin);
        return 6;
    }

    char uri[64]{};
    const auto requiredUriSize = nativeWebview->get_uri(nativePlugin, nullptr, 0u);
    if (requiredUriSize <= 0 ||
        nativeWebview->get_uri(nativePlugin,
                               uri,
                               static_cast<std::uint32_t>(sizeof(uri))) != requiredUriSize ||
        std::strcmp(uri, "/index.html") != 0) {
        nativePlugin->destroy(nativePlugin);
        return 7;
    }

    BufferStream html;
    char htmlMime[64]{};
    if (!nativeWebview->get_resource(nativePlugin,
                                     "/index.html",
                                     htmlMime,
                                     static_cast<std::uint32_t>(sizeof(htmlMime)),
                                     &html.stream) ||
        std::strcmp(htmlMime, "text/html; charset=utf-8") != 0 || html.size == 0u) {
        nativePlugin->destroy(nativePlugin);
        return 8;
    }

    BufferStream script;
    char scriptMime[64]{};
    if (!nativeWebview->get_resource(nativePlugin,
                                     "/polysynth.js",
                                     scriptMime,
                                     static_cast<std::uint32_t>(sizeof(scriptMime)),
                                     &script.stream) ||
        std::strcmp(scriptMime, "text/javascript; charset=utf-8") != 0 || script.size == 0u) {
        nativePlugin->destroy(nativePlugin);
        return 9;
    }

    if (!nativePlugin->activate(nativePlugin, 48000.0, 1u, 64u)) {
        nativePlugin->destroy(nativePlugin);
        return 10;
    }

    clap_voice_info_t info{};
    if (!voiceInfo->get(nativePlugin, &info) ||
        info.voice_count != kPolySynthDefaultVoiceCount ||
        info.voice_capacity != VoiceAllocator::kMaximumVoices ||
        (info.flags & CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES) == 0 ||
        voiceInfo->get(nativePlugin, nullptr)) {
        nativePlugin->deactivate(nativePlugin);
        nativePlugin->destroy(nativePlugin);
        return 11;
    }
    nativePlugin->deactivate(nativePlugin);
    nativePlugin->destroy(nativePlugin);

    // Host-owned WebView path: two identical instances must remain completely
    // isolated, including parameter edits, host/base synchronization and sends.
    WebviewHostState hostA{"polysynth-webview-A"};
    WebviewHostState hostB{"polysynth-webview-B"};
    const auto *pluginA = createWrapped(factory, &hostA.host);
    const auto *pluginB = createWrapped(factory, &hostB.host);
    if (!pluginA || !pluginB) {
        if (pluginA)
            pluginA->destroy(pluginA);
        if (pluginB)
            pluginB->destroy(pluginB);
        return 12;
    }
    if (!pluginA->init(pluginA) || !pluginB->init(pluginB)) {
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 13;
    }

    const auto *guiA = static_cast<const clap_plugin_gui_t *>(
        pluginA->get_extension(pluginA, CLAP_EXT_GUI));
    const auto *guiB = static_cast<const clap_plugin_gui_t *>(
        pluginB->get_extension(pluginB, CLAP_EXT_GUI));
    const auto *webviewA = static_cast<const clap_plugin_webview_t *>(
        pluginA->get_extension(pluginA, CLAP_EXT_WEBVIEW));
    const auto *webviewB = static_cast<const clap_plugin_webview_t *>(
        pluginB->get_extension(pluginB, CLAP_EXT_WEBVIEW));
    const auto *paramsA = static_cast<const clap_plugin_params_t *>(
        pluginA->get_extension(pluginA, CLAP_EXT_PARAMS));
    const auto *paramsB = static_cast<const clap_plugin_params_t *>(
        pluginB->get_extension(pluginB, CLAP_EXT_PARAMS));
    if (!guiA || !guiB || !webviewA || !webviewB || !paramsA || !paramsB ||
        !guiA->is_api_supported(pluginA, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false) ||
        !guiB->is_api_supported(pluginB, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false) ||
        !guiA->create(pluginA, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false) ||
        !guiB->create(pluginB, ::webview_gui::CLAP_WINDOW_API_WEBVIEW, false)) {
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 14;
    }

    const std::array<std::uint8_t, 4> sync{{'W', 'V', 'S', '1'}};
    if (!webviewA->receive(pluginA, sync.data(), static_cast<std::uint32_t>(sync.size())) ||
        hostA.sends != 14u || hostB.sends != 0u || !hostA.sawMasterBase ||
        hostA.masterBase != 0.0) {
        guiA->destroy(pluginA);
        guiB->destroy(pluginB);
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 15;
    }

    const auto beginGain = editMessage(1u, kFirstParameterId);
    const auto valueGain = editMessage(2u, kFirstParameterId, 6.0);
    const auto endGain = editMessage(3u, kFirstParameterId);
    if (!webviewA->receive(pluginA, beginGain.data(), beginGain.size()) ||
        !webviewA->receive(pluginA, valueGain.data(), valueGain.size()) ||
        !webviewA->receive(pluginA, endGain.data(), endGain.size()) ||
        hostA.flushRequests < 3u || hostB.flushRequests != 0u) {
        guiA->destroy(pluginA);
        guiB->destroy(pluginB);
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 16;
    }

    OutputCollector output;
    paramsA->flush(pluginA, nullptr, &output.output);
    double gainA = 0.0;
    double gainB = 0.0;
    if (output.count != 3u ||
        output.types[0] != CLAP_EVENT_PARAM_GESTURE_BEGIN ||
        output.types[1] != CLAP_EVENT_PARAM_VALUE ||
        output.types[2] != CLAP_EVENT_PARAM_GESTURE_END ||
        output.ids[0] != kFirstParameterId || output.ids[1] != kFirstParameterId ||
        output.ids[2] != kFirstParameterId ||
        !paramsA->get_value(pluginA, kFirstParameterId, &gainA) || gainA != 6.0 ||
        !paramsB->get_value(pluginB, kFirstParameterId, &gainB) || gainB != 0.0) {
        guiA->destroy(pluginA);
        guiB->destroy(pluginB);
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 17;
    }

    // A host/processor base-value update on B must be reflected by B's next UI
    // snapshot while A keeps its own independently edited value.
    auto hostGainB = hostValueEvent(kFirstParameterId, -3.0);
    SingleInput hostInputB{&hostGainB.header};
    output.reset();
    paramsB->flush(pluginB, &hostInputB.input, &output.output);
    hostA.sawMasterBase = false;
    hostB.sawMasterBase = false;
    const auto sendsABefore = hostA.sends;
    const auto sendsBBefore = hostB.sends;
    if (!webviewA->receive(pluginA, sync.data(), sync.size()) ||
        !webviewB->receive(pluginB, sync.data(), sync.size()) ||
        hostA.sends != sendsABefore + 14u || hostB.sends != sendsBBefore + 14u ||
        !hostA.sawMasterBase || !hostB.sawMasterBase ||
        hostA.masterBase != 6.0 || hostB.masterBase != -3.0) {
        guiA->destroy(pluginA);
        guiB->destroy(pluginB);
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 18;
    }

    // Saturation is fail-closed and bounded. The queue reserves enough room to
    // close every open gesture, so repeated value messages eventually reject
    // instead of blocking while the final gesture-end remains enqueueable.
    constexpr clap_id kCutoffId = kFirstParameterId + 4u;
    const auto beginCutoff = editMessage(1u, kCutoffId);
    const auto valueCutoff = editMessage(2u, kCutoffId, 7000.0);
    const auto endCutoff = editMessage(3u, kCutoffId);
    if (!webviewB->receive(pluginB, beginCutoff.data(), beginCutoff.size())) {
        guiA->destroy(pluginA);
        guiB->destroy(pluginB);
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 19;
    }

    std::uint32_t acceptedValues = 0u;
    bool rejectedAtCapacity = false;
    for (std::uint32_t attempt = 0u;
         attempt < proxy_detail::UiParameterQueue::kCapacity * 2u;
         ++attempt) {
        if (!webviewB->receive(pluginB, valueCutoff.data(), valueCutoff.size())) {
            rejectedAtCapacity = true;
            break;
        }
        ++acceptedValues;
    }
    if (!rejectedAtCapacity || acceptedValues == 0u ||
        !webviewB->receive(pluginB, endCutoff.data(), endCutoff.size())) {
        guiA->destroy(pluginA);
        guiB->destroy(pluginB);
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 20;
    }

    output.reset();
    paramsB->flush(pluginB, nullptr, &output.output);
    if (output.count != acceptedValues + 2u ||
        output.types[0] != CLAP_EVENT_PARAM_GESTURE_BEGIN ||
        output.types[output.count - 1u] != CLAP_EVENT_PARAM_GESTURE_END ||
        output.ids[0] != kCutoffId || output.ids[output.count - 1u] != kCutoffId) {
        guiA->destroy(pluginA);
        guiB->destroy(pluginB);
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 21;
    }

    // Destroying A's editor must detach its receive path without affecting B.
    guiA->destroy(pluginA);
    const auto sendsBAfterAClosed = hostB.sends;
    if (webviewA->receive(pluginA, sync.data(), sync.size()) ||
        !webviewB->receive(pluginB, sync.data(), sync.size()) ||
        hostB.sends != sendsBAfterAClosed + 14u) {
        guiB->destroy(pluginB);
        pluginA->destroy(pluginA);
        pluginB->destroy(pluginB);
        return 22;
    }

    guiB->destroy(pluginB);
    pluginA->destroy(pluginA);
    pluginB->destroy(pluginB);
    return 0;
}
