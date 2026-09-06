#include "gain_plugin.h"

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct HostCapture {
    uint32_t callbackRequests = 0;
    std::vector<std::vector<uint8_t>> messages;

    void reset() {
        callbackRequests = 0;
        messages.clear();
    }
};

HostCapture gHostCapture;

bool CLAP_ABI hostWebviewSend(const clap_host_t *host, const void *buffer, uint32_t size) {
    if (!host || !host->host_data || (!buffer && size != 0))
        return false;
    auto &capture = *static_cast<HostCapture *>(host->host_data);
    const auto *bytes = static_cast<const uint8_t *>(buffer);
    capture.messages.emplace_back(bytes, bytes + size);
    return true;
}

const clap_host_webview_t kHostWebview{
    hostWebviewSend,
};

const clap_host_webview_t kBrokenHostWebview{
    nullptr,
};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *id) {
    if (id && std::strcmp(id, CLAP_EXT_WEBVIEW) == 0)
        return &kHostWebview;
    return nullptr;
}

const void *CLAP_ABI hostGetBrokenExtension(const clap_host_t *, const char *id) {
    if (id && std::strcmp(id, CLAP_EXT_WEBVIEW) == 0)
        return &kBrokenHostWebview;
    return nullptr;
}

const void *CLAP_ABI hostGetNoExtension(const clap_host_t *, const char *) {
    return nullptr;
}

void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *host) {
    if (host && host->host_data)
        ++static_cast<HostCapture *>(host->host_data)->callbackRequests;
}

const clap_host_t kHost{
    CLAP_VERSION,
    &gHostCapture,
    "webview-gui Gain WebView resource tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

const clap_host_t kHostWithoutWebview{
    CLAP_VERSION,
    nullptr,
    "webview-gui Gain non-WebView host tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetNoExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

const clap_host_t kHostWithBrokenWebview{
    CLAP_VERSION,
    nullptr,
    "webview-gui Gain incomplete WebView host tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetBrokenExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

struct MemoryOutputStream {
    explicit MemoryOutputStream(std::size_t maxWrite = std::numeric_limits<std::size_t>::max())
        : maxWrite(maxWrite), stream{this, write} {}

    static int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                  const void *buffer,
                                  uint64_t size) {
        if (!stream || !stream->ctx || (!buffer && size != 0))
            return -1;
        auto &self = *static_cast<MemoryOutputStream *>(stream->ctx);
        if (size == 0)
            return 0;

        const auto requested = static_cast<std::size_t>(std::min<uint64_t>(
            size, static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())));
        const auto amount = std::min(requested, self.maxWrite);
        if (amount == 0)
            return -1;

        const auto *begin = static_cast<const uint8_t *>(buffer);
        self.bytes.insert(self.bytes.end(), begin, begin + amount);
        return static_cast<int64_t>(amount);
    }

    std::size_t maxWrite;
    std::vector<uint8_t> bytes;
    clap_ostream_t stream;
};

struct SingleParamInput {
    SingleParamInput(clap_id paramId, double value) {
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = 0;
        event.param_id = paramId;
        event.cookie = nullptr;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    static uint32_t CLAP_ABI size(const clap_input_events_t *) { return 1; }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *list,
                                                    uint32_t index) {
        if (!list || !list->ctx || index != 0)
            return nullptr;
        return &static_cast<const SingleParamInput *>(list->ctx)->event.header;
    }

    clap_event_param_value_t event{};
    clap_input_events_t input{};
};

bool contains(const std::string &text, const char *needle) {
    return needle && text.find(needle) != std::string::npos;
}

const char *expectedNativeApi() {
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

uint32_t loadU32Le(const uint8_t *bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(bytes[i]) << (i * 8u);
    return value;
}

uint64_t loadU64Le(const uint8_t *bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8u);
    return value;
}

bool matchesUiParameterMessage(const std::vector<uint8_t> &message,
                               uint8_t parameter,
                               double expected) {
    if (message.size() != 16 ||
        message[0] != 0x57 || message[1] != 0x56 ||
        message[2] != 0x55 || message[3] != 0x31 ||
        message[4] != parameter || message[5] != 0 || message[6] != 0 || message[7] != 0)
        return false;
    const uint64_t bits = loadU64Le(message.data() + 8);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value) && std::fabs(value - expected) < 1.0e-9;
}

bool matchesUiMeterMessage(const std::vector<uint8_t> &message,
                           float expectedLeft,
                           float expectedRight) {
    if (message.size() != 16 ||
        message[0] != 0x57 || message[1] != 0x56 ||
        message[2] != 0x4d || message[3] != 0x31)
        return false;

    const uint32_t leftBits = loadU32Le(message.data() + 4);
    const uint32_t rightBits = loadU32Le(message.data() + 8);
    const uint32_t sequence = loadU32Le(message.data() + 12);
    float left = 0.0f;
    float right = 0.0f;
    std::memcpy(&left, &leftBits, sizeof(left));
    std::memcpy(&right, &rightBits, sizeof(right));
    return std::isfinite(left) && std::isfinite(right) &&
           std::fabs(left - expectedLeft) < 1.0e-6f &&
           std::fabs(right - expectedRight) < 1.0e-6f &&
           sequence != 0u && (sequence & 1u) == 0u;
}

} // namespace

int main() {
    using namespace webview_gui::examples::gain;

    constexpr std::array<uint8_t, 4> kSyncRequest{{0x57, 0x56, 0x51, 0x31}};
    gHostCapture.reset();
    const auto *factory = gainFactory();
    if (!factory)
        return 1;

    const auto *plugin = factory->create_plugin(factory, &kHost, kGainPluginId);
    if (!plugin)
        return 2;
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return 3;
    }

    const auto *webview = static_cast<const clap_plugin_webview_t *>(
        plugin->get_extension(plugin, CLAP_EXT_WEBVIEW));
    if (!webview || !webview->get_uri || !webview->get_resource || !webview->receive) {
        std::cerr << "Gain must expose the complete clap.webview/3 plug-in surface\n";
        plugin->destroy(plugin);
        return 4;
    }

    constexpr const char *expectedUri = "/index.html";
    constexpr int32_t expectedUriSize = 12;

    if (webview->get_uri(plugin, nullptr, 0) != expectedUriSize) {
        std::cerr << "get_uri capacity query did not return the full NUL-inclusive length\n";
        plugin->destroy(plugin);
        return 5;
    }

    char shortUri[5] = {'x', 'x', 'x', 'x', 'x'};
    if (webview->get_uri(plugin, shortUri, sizeof(shortUri)) != expectedUriSize ||
        shortUri[sizeof(shortUri) - 1] != '\0' || std::strncmp(shortUri, "/ind", 4) != 0) {
        std::cerr << "get_uri did not truncate safely while reporting the full length\n";
        plugin->destroy(plugin);
        return 6;
    }

    char uri[64] = {};
    if (webview->get_uri(plugin, uri, sizeof(uri)) != expectedUriSize ||
        std::strcmp(uri, expectedUri) != 0) {
        std::cerr << "Gain WebView start URI is not the expected relative resource path\n";
        plugin->destroy(plugin);
        return 7;
    }

    MemoryOutputStream htmlOutput(7);
    char mime[64] = {};
    if (!webview->get_resource(plugin, expectedUri, mime, sizeof(mime), &htmlOutput.stream) ||
        std::strcmp(mime, "text/html; charset=utf-8") != 0 || htmlOutput.bytes.empty()) {
        std::cerr << "Gain WebView resource did not survive short CLAP stream writes\n";
        plugin->destroy(plugin);
        return 8;
    }

    const std::string html(htmlOutput.bytes.begin(), htmlOutput.bytes.end());
    if (!contains(html, "id=\"root\"") ||
        !contains(html, "script-src 'self'") ||
        !contains(html, "type=\"module\"") ||
        contains(html, "http://") || contains(html, "https://") || contains(html, "//cdn.")) {
        std::cerr << "bundled Gain editor is not a self-contained Vite entry point\n";
        plugin->destroy(plugin);
        return 9;
    }

    const auto scriptMarker = html.find("<script type=\"module\" crossorigin src=\"");
    const auto fallbackMarker = html.find("<script type=\"module\" src=\"");
    const auto marker = scriptMarker != std::string::npos ? scriptMarker : fallbackMarker;
    if (marker == std::string::npos) {
        std::cerr << "Vite entry does not reference its module asset\n";
        plugin->destroy(plugin);
        return 90;
    }
    const auto src = html.find("src=\"", marker);
    const auto srcEnd = src == std::string::npos ? std::string::npos : html.find('\"', src + 5u);
    if (src == std::string::npos || srcEnd == std::string::npos) {
        plugin->destroy(plugin);
        return 91;
    }
    std::string scriptPath = html.substr(src + 5u, srcEnd - (src + 5u));
    if (!scriptPath.empty() && scriptPath[0] == '.')
        scriptPath.erase(0u, 1u);
    if (scriptPath.empty() || scriptPath[0] != '/') {
        plugin->destroy(plugin);
        return 92;
    }

    MemoryOutputStream scriptOutput(5);
    char scriptMime[64] = {};
    if (!webview->get_resource(plugin, scriptPath.c_str(), scriptMime, sizeof(scriptMime),
                               &scriptOutput.stream) ||
        std::strcmp(scriptMime, "application/javascript; charset=utf-8") != 0 ||
        scriptOutput.bytes.empty()) {
        std::cerr << "bundled Vite JavaScript asset was not served correctly\n";
        plugin->destroy(plugin);
        return 93;
    }
    const std::string script(scriptOutput.bytes.begin(), scriptOutput.bytes.end());
    if (contains(script, "http://") || contains(script, "https://") ||
        contains(script, "XMLHttpRequest")) {
        std::cerr << "Gain frontend introduced a remote runtime dependency\n";
        plugin->destroy(plugin);
        return 94;
    }

    MemoryOutputStream unknownOutput;
    char unknownMime[64] = {};
    if (webview->get_resource(plugin, "/missing.js", unknownMime, sizeof(unknownMime),
                              &unknownOutput.stream) ||
        !unknownOutput.bytes.empty()) {
        std::cerr << "unknown WebView resource was accepted or streamed partial data\n";
        plugin->destroy(plugin);
        return 11;
    }

    MemoryOutputStream tinyMimeOutput;
    char tinyMime[4] = {};
    if (webview->get_resource(plugin, expectedUri, tinyMime, sizeof(tinyMime),
                              &tinyMimeOutput.stream) ||
        !tinyMimeOutput.bytes.empty() || tinyMime[0] != '\0') {
        std::cerr << "insufficient MIME capacity must fail before mutating outputs or streaming data\n";
        plugin->destroy(plugin);
        return 12;
    }

    if (webview->get_resource(plugin, expectedUri, mime, sizeof(mime), nullptr)) {
        std::cerr << "resource request without a CLAP output stream did not fail closed\n";
        plugin->destroy(plugin);
        return 13;
    }

    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!gui || !params) {
        std::cerr << "Gain must expose clap.gui and clap.params for editor synchronization\n";
        plugin->destroy(plugin);
        return 14;
    }

    if (!gui->is_api_supported(plugin, CLAP_WINDOW_API_WEBVIEW, false) ||
        gui->is_api_supported(plugin, CLAP_WINDOW_API_WEBVIEW, true)) {
        std::cerr << "Gain clap.gui did not negotiate embedded WebView correctly\n";
        plugin->destroy(plugin);
        return 15;
    }

    const char *preferredApi = nullptr;
    bool isFloating = true;
    if (!gui->get_preferred_api(plugin, &preferredApi, &isFloating) ||
        !preferredApi || std::strcmp(preferredApi, CLAP_WINDOW_API_WEBVIEW) != 0 ||
        isFloating) {
        std::cerr << "Gain clap.gui did not prefer the embedded WebView API\n";
        plugin->destroy(plugin);
        return 16;
    }

    if (!gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false)) {
        std::cerr << "Gain clap.gui could not create the embedded WebView GUI\n";
        plugin->destroy(plugin);
        return 17;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!gui->get_size(plugin, &width, &height) || width != 480 || height != 430) {
        std::cerr << "Gain clap.gui did not expose the expected initial logical size after create()\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 18;
    }

    clap_window_t webviewWindow{};
    webviewWindow.api = CLAP_WINDOW_API_WEBVIEW;
    webviewWindow.ptr = nullptr;
    if (!gui->set_parent(plugin, &webviewWindow) || !gui->show(plugin)) {
        std::cerr << "Gain host-owned WebView parent/show lifecycle failed\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 19;
    }

    if (gHostCapture.callbackRequests != 0 || !gHostCapture.messages.empty()) {
        std::cerr << "Gain editor synchronization must not use host main-thread callbacks\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 95;
    }

    if (!webview->receive(plugin, kSyncRequest.data(), kSyncRequest.size()) ||
        gHostCapture.messages.size() != 2 ||
        !matchesUiParameterMessage(gHostCapture.messages[0], 1, 0.0) ||
        !matchesUiParameterMessage(gHostCapture.messages[1], 2, 0.0)) {
        std::cerr << "Gain did not publish the initial parameter snapshot on a WebView sync request\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 96;
    }

    SingleParamInput gainChange{kGainParamId, -12.0};
    SingleParamInput bypassChange{kBypassParamId, 1.0};
    params->flush(plugin, &gainChange.input, nullptr);
    params->flush(plugin, &bypassChange.input, nullptr);
    if (gHostCapture.callbackRequests != 0 || gHostCapture.messages.size() != 2) {
        std::cerr << "Gain parameter changes performed host/UI work outside WebView polling\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 97;
    }

    if (!webview->receive(plugin, kSyncRequest.data(), kSyncRequest.size()) ||
        gHostCapture.messages.size() != 4 ||
        !matchesUiParameterMessage(gHostCapture.messages[2], 1, -12.0) ||
        !matchesUiParameterMessage(gHostCapture.messages[3], 2, 1.0)) {
        std::cerr << "Gain did not reconcile host parameter changes into the WebView\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 98;
    }

    SingleParamInput gainIntermediate{kGainParamId, -6.0};
    SingleParamInput gainLatest{kGainParamId, -3.0};
    params->flush(plugin, &gainIntermediate.input, nullptr);
    params->flush(plugin, &gainLatest.input, nullptr);
    if (gHostCapture.callbackRequests != 0 || gHostCapture.messages.size() != 4) {
        std::cerr << "Gain repeated parameter changes were not coalesced before WebView polling\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 99;
    }
    if (!webview->receive(plugin, kSyncRequest.data(), kSyncRequest.size()) ||
        gHostCapture.messages.size() != 6 ||
        !matchesUiParameterMessage(gHostCapture.messages[4], 1, -3.0) ||
        !matchesUiParameterMessage(gHostCapture.messages[5], 2, 1.0)) {
        std::cerr << "Gain editor synchronization did not publish the latest coalesced snapshot\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 100;
    }

    if (!gui->hide(plugin)) {
        std::cerr << "Gain host-owned WebView hide lifecycle failed\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 20;
    }

    SingleParamInput hiddenGainChange{kGainParamId, -9.0};
    params->flush(plugin, &hiddenGainChange.input, nullptr);
    if (gHostCapture.callbackRequests != 0 || gHostCapture.messages.size() != 6) {
        std::cerr << "Gain scheduled WebView synchronization while the editor was hidden\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 101;
    }

    if (!gui->show(plugin) || gHostCapture.callbackRequests != 0) {
        std::cerr << "Gain host-owned WebView could not reopen without a host callback side channel\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 102;
    }
    if (!webview->receive(plugin, kSyncRequest.data(), kSyncRequest.size()) ||
        gHostCapture.messages.size() != 8 ||
        !matchesUiParameterMessage(gHostCapture.messages[6], 1, -9.0) ||
        !matchesUiParameterMessage(gHostCapture.messages[7], 2, 1.0)) {
        std::cerr << "Gain did not reconcile hidden parameter changes when the editor reopened\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 103;
    }

    gHostCapture.reset();
    if (!plugin->activate(plugin, 48000.0, 1, 64) || !plugin->start_processing(plugin)) {
        std::cerr << "Gain could not enter processing for WebView meter qualification\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 105;
    }

    std::array<float, 4> inputLeft{{0.25f, -0.50f, 0.125f, 0.0f}};
    std::array<float, 4> inputRight{{-0.75f, 0.25f, 0.50f, 0.0f}};
    std::array<float, 4> outputLeft{};
    std::array<float, 4> outputRight{};
    float *inputChannels[2]{inputLeft.data(), inputRight.data()};
    float *outputChannels[2]{outputLeft.data(), outputRight.data()};
    clap_audio_buffer_t audioInput{};
    audioInput.data32 = inputChannels;
    audioInput.channel_count = 2;
    clap_audio_buffer_t audioOutput{};
    audioOutput.data32 = outputChannels;
    audioOutput.channel_count = 2;
    clap_process_t meterProcess{};
    meterProcess.frames_count = 4;
    meterProcess.audio_inputs = &audioInput;
    meterProcess.audio_outputs = &audioOutput;
    meterProcess.audio_inputs_count = 1;
    meterProcess.audio_outputs_count = 1;

    if (plugin->process(plugin, &meterProcess) != CLAP_PROCESS_CONTINUE) {
        std::cerr << "Gain processing failed while publishing the first meter snapshot\n";
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 106;
    }

    inputLeft = {{0.0625f, -0.125f, 0.03125f, 0.0f}};
    inputRight = {{-0.25f, 0.125f, 0.0625f, 0.0f}};
    if (plugin->process(plugin, &meterProcess) != CLAP_PROCESS_CONTINUE ||
        gHostCapture.callbackRequests != 0 || !gHostCapture.messages.empty()) {
        std::cerr << "Gain audio processing performed host/UI work instead of coalescing the meter\n";
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 107;
    }

    if (!webview->receive(plugin, kSyncRequest.data(), kSyncRequest.size()) ||
        gHostCapture.messages.size() != 3 ||
        !matchesUiParameterMessage(gHostCapture.messages[0], 1, -9.0) ||
        !matchesUiParameterMessage(gHostCapture.messages[1], 2, 1.0) ||
        !matchesUiMeterMessage(gHostCapture.messages[2], 0.125f, 0.25f)) {
        std::cerr << "Gain did not deliver the latest coalesced stereo meter on the UI poll\n";
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 108;
    }

    if (!webview->receive(plugin, kSyncRequest.data(), kSyncRequest.size()) ||
        gHostCapture.messages.size() != 5 ||
        !matchesUiParameterMessage(gHostCapture.messages[3], 1, -9.0) ||
        !matchesUiParameterMessage(gHostCapture.messages[4], 2, 1.0)) {
        std::cerr << "Gain resent an unchanged meter snapshot instead of coalescing UI delivery\n";
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 109;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);

    if (!gui->can_resize(plugin) || !gui->set_size(plugin, 640, 360) ||
        !gui->get_size(plugin, &width, &height) || width != 640 || height != 360) {
        std::cerr << "Gain clap.gui logical resize contract failed\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 21;
    }

    gui->destroy(plugin);

    if (!gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false)) {
        std::cerr << "Gain clap.gui could not be recreated after destroy()\n";
        plugin->destroy(plugin);
        return 22;
    }
    if (!gui->get_size(plugin, &width, &height) || width != 480 || height != 430) {
        std::cerr << "Gain clap.gui recreate did not restore the initial logical size\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 23;
    }
    gui->destroy(plugin);
    plugin->destroy(plugin);

    const char *nativeApi = expectedNativeApi();
    if (!nativeApi)
        return 24;

    const auto *nonWebviewPlugin =
        factory->create_plugin(factory, &kHostWithoutWebview, kGainPluginId);
    if (!nonWebviewPlugin)
        return 25;
    if (!nonWebviewPlugin->init(nonWebviewPlugin)) {
        nonWebviewPlugin->destroy(nonWebviewPlugin);
        return 26;
    }

    const auto *nonWebviewGui = static_cast<const clap_plugin_gui_t *>(
        nonWebviewPlugin->get_extension(nonWebviewPlugin, CLAP_EXT_GUI));
    const char *unsupportedPreferredApi = nullptr;
    bool unsupportedFloating = true;
    if (!nonWebviewGui ||
        nonWebviewGui->is_api_supported(nonWebviewPlugin, CLAP_WINDOW_API_WEBVIEW, false) ||
        !nonWebviewGui->is_api_supported(nonWebviewPlugin, nativeApi, false) ||
        !nonWebviewGui->get_preferred_api(nonWebviewPlugin,
                                          &unsupportedPreferredApi,
                                          &unsupportedFloating) ||
        !unsupportedPreferredApi || std::strcmp(unsupportedPreferredApi, nativeApi) != 0 ||
        unsupportedFloating) {
        std::cerr << "Gain failed native GUI fallback without host clap.webview/3 support\n";
        nonWebviewPlugin->destroy(nonWebviewPlugin);
        return 27;
    }
    nonWebviewPlugin->destroy(nonWebviewPlugin);

    const auto *brokenWebviewPlugin =
        factory->create_plugin(factory, &kHostWithBrokenWebview, kGainPluginId);
    if (!brokenWebviewPlugin)
        return 28;
    if (!brokenWebviewPlugin->init(brokenWebviewPlugin)) {
        brokenWebviewPlugin->destroy(brokenWebviewPlugin);
        return 29;
    }

    const auto *brokenWebviewGui = static_cast<const clap_plugin_gui_t *>(
        brokenWebviewPlugin->get_extension(brokenWebviewPlugin, CLAP_EXT_GUI));
    const char *brokenPreferredApi = nullptr;
    bool brokenFloating = true;
    if (!brokenWebviewGui ||
        brokenWebviewGui->is_api_supported(brokenWebviewPlugin,
                                           CLAP_WINDOW_API_WEBVIEW,
                                           false) ||
        !brokenWebviewGui->is_api_supported(brokenWebviewPlugin, nativeApi, false) ||
        !brokenWebviewGui->get_preferred_api(brokenWebviewPlugin,
                                             &brokenPreferredApi,
                                             &brokenFloating) ||
        !brokenPreferredApi || std::strcmp(brokenPreferredApi, nativeApi) != 0 ||
        brokenFloating) {
        std::cerr << "Gain failed native GUI fallback with an incomplete host WebView path\n";
        brokenWebviewPlugin->destroy(brokenWebviewPlugin);
        return 30;
    }

    brokenWebviewPlugin->destroy(brokenWebviewPlugin);
    return 0;
}