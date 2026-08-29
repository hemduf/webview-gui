#include "gain_plugin.h"

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>

#include <algorithm>
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

} // namespace

int main() {
    using namespace webview_gui::examples::gain;

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
    constexpr int32_t expectedUriSize = 12; // strlen + trailing NUL

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
    if (!contains(html, "id=\"gain\"") ||
        !contains(html, "id=\"bypass\"") ||
        !contains(html, "id=\"gain-value\"") ||
        !contains(html, "id=\"meter-left\"") ||
        !contains(html, "id=\"meter-right\"")) {
        std::cerr << "bundled Gain editor is missing its required control/meter surface\n";
        plugin->destroy(plugin);
        return 9;
    }

    if (contains(html, "http://") || contains(html, "https://") || contains(html, "//cdn.")) {
        std::cerr << "bundled Gain editor must not require remote runtime resources\n";
        plugin->destroy(plugin);
        return 10;
    }

    if (!contains(html, "script-src 'self'") ||
        !contains(html, "<script src=\"gain.js\" defer></script>")) {
        std::cerr << "bundled Gain editor does not load its same-origin parameter transport script\n";
        plugin->destroy(plugin);
        return 90;
    }

    MemoryOutputStream scriptOutput(5);
    char scriptMime[64] = {};
    if (!webview->get_resource(plugin, "/gain.js", scriptMime, sizeof(scriptMime),
                               &scriptOutput.stream) ||
        std::strcmp(scriptMime, "text/javascript; charset=utf-8") != 0 ||
        scriptOutput.bytes.empty()) {
        std::cerr << "bundled Gain parameter transport script was not served correctly\n";
        plugin->destroy(plugin);
        return 91;
    }

    const std::string script(scriptOutput.bytes.begin(), scriptOutput.bytes.end());
    if (!contains(script, "new ArrayBuffer(16)") ||
        !contains(script, "bytes.set([0x57, 0x56, 0x47, 0x31])") ||
        !contains(script, "setFloat64(8, value, true)") ||
        !contains(script, "window.parent.postMessage(encode(kind, parameter, value), \"*\")") ||
        !contains(script, "gain.addEventListener(\"input\"") ||
        !contains(script, "gain.addEventListener(\"pointercancel\"") ||
        !contains(script, "bypass.addEventListener(\"change\"")) {
        std::cerr << "Gain editor script does not implement the WVG1 binary control protocol\n";
        plugin->destroy(plugin);
        return 92;
    }

    // RED for processor/host -> editor reconciliation. The WebView must consume
    // the fixed-size WVU1 snapshots emitted by the plug-in on the main thread.
    if (!contains(script, "window.addEventListener(\"message\"") ||
        !contains(script, "getFloat64(8, true)") ||
        !contains(script, "gain.value =") ||
        !contains(script, "bypass.checked =") ||
        !contains(script, "0x55")) {
        std::cerr << "Gain editor script does not consume the WVU1 parameter sync protocol\n";
        plugin->destroy(plugin);
        return 94;
    }

    if (contains(script, "JSON.stringify") || contains(script, "fetch(") ||
        contains(script, "XMLHttpRequest")) {
        std::cerr << "Gain editor control transport introduced JSON or network I/O\n";
        plugin->destroy(plugin);
        return 93;
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
    if (!gui->get_size(plugin, &width, &height) || width != 480 || height != 320) {
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

    if (gHostCapture.callbackRequests != 1 || !gHostCapture.messages.empty()) {
        std::cerr << "Gain must schedule initial editor sync without sending from gui.show()\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 95;
    }

    plugin->on_main_thread(plugin);
    if (gHostCapture.messages.size() != 2 ||
        !matchesUiParameterMessage(gHostCapture.messages[0], 1, 0.0) ||
        !matchesUiParameterMessage(gHostCapture.messages[1], 2, 0.0)) {
        std::cerr << "Gain did not publish the initial parameter snapshot on the main thread\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 96;
    }

    SingleParamInput gainChange{kGainParamId, -12.0};
    SingleParamInput bypassChange{kBypassParamId, 1.0};
    params->flush(plugin, &gainChange.input, nullptr);
    params->flush(plugin, &bypassChange.input, nullptr);
    if (gHostCapture.callbackRequests != 2 || gHostCapture.messages.size() != 2) {
        std::cerr << "Gain host parameter changes were not coalesced onto the main thread\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 97;
    }

    plugin->on_main_thread(plugin);
    if (gHostCapture.messages.size() != 4 ||
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
    if (gHostCapture.callbackRequests != 3) {
        std::cerr << "Gain editor synchronization did not coalesce repeated parameter changes\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 99;
    }
    plugin->on_main_thread(plugin);
    if (gHostCapture.messages.size() != 6 ||
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
    if (gHostCapture.callbackRequests != 3) {
        std::cerr << "Gain scheduled WebView synchronization while the editor was hidden\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 101;
    }

    if (!gui->show(plugin) || gHostCapture.callbackRequests != 4) {
        std::cerr << "Gain did not schedule the latest parameter snapshot when the editor reopened\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 102;
    }
    plugin->on_main_thread(plugin);
    if (gHostCapture.messages.size() != 8 ||
        !matchesUiParameterMessage(gHostCapture.messages[6], 1, -9.0) ||
        !matchesUiParameterMessage(gHostCapture.messages[7], 2, 1.0)) {
        std::cerr << "Gain did not reconcile hidden parameter changes when the editor reopened\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 103;
    }

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
    if (!gui->get_size(plugin, &width, &height) || width != 480 || height != 320) {
        std::cerr << "Gain clap.gui recreate did not restore the initial logical size\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 23;
    }
    gui->destroy(plugin);
    plugin->destroy(plugin);

    const auto *nonWebviewPlugin =
        factory->create_plugin(factory, &kHostWithoutWebview, kGainPluginId);
    if (!nonWebviewPlugin)
        return 24;
    if (!nonWebviewPlugin->init(nonWebviewPlugin)) {
        nonWebviewPlugin->destroy(nonWebviewPlugin);
        return 25;
    }

    const auto *nonWebviewGui = static_cast<const clap_plugin_gui_t *>(
        nonWebviewPlugin->get_extension(nonWebviewPlugin, CLAP_EXT_GUI));
    const char *unsupportedPreferredApi = nullptr;
    bool unsupportedFloating = false;
    if (!nonWebviewGui ||
        nonWebviewGui->is_api_supported(nonWebviewPlugin, CLAP_WINDOW_API_WEBVIEW, false) ||
        nonWebviewGui->get_preferred_api(nonWebviewPlugin,
                                         &unsupportedPreferredApi,
                                         &unsupportedFloating)) {
        std::cerr << "Gain advertised host-owned WebView GUI without host clap.webview/3 support\n";
        nonWebviewPlugin->destroy(nonWebviewPlugin);
        return 26;
    }
    nonWebviewPlugin->destroy(nonWebviewPlugin);

    const auto *brokenWebviewPlugin =
        factory->create_plugin(factory, &kHostWithBrokenWebview, kGainPluginId);
    if (!brokenWebviewPlugin)
        return 27;
    if (!brokenWebviewPlugin->init(brokenWebviewPlugin)) {
        brokenWebviewPlugin->destroy(brokenWebviewPlugin);
        return 28;
    }

    const auto *brokenWebviewGui = static_cast<const clap_plugin_gui_t *>(
        brokenWebviewPlugin->get_extension(brokenWebviewPlugin, CLAP_EXT_GUI));
    const char *brokenPreferredApi = nullptr;
    bool brokenFloating = false;
    if (!brokenWebviewGui ||
        brokenWebviewGui->is_api_supported(brokenWebviewPlugin,
                                           CLAP_WINDOW_API_WEBVIEW,
                                           false) ||
        brokenWebviewGui->get_preferred_api(brokenWebviewPlugin,
                                            &brokenPreferredApi,
                                            &brokenFloating)) {
        std::cerr << "Gain advertised host-owned WebView GUI with a missing host send callback\n";
        brokenWebviewPlugin->destroy(brokenWebviewPlugin);
        return 29;
    }

    brokenWebviewPlugin->destroy(brokenWebviewPlugin);
    return 0;
}
