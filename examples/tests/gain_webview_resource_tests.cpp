#include "gain_plugin.h"

#include <clap/clap.h>
#include <clap/ext/draft/webview.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

bool CLAP_ABI hostWebviewSend(const clap_host_t *, const void *, uint32_t) {
    return true;
}

const clap_host_webview_t kHostWebview{
    hostWebviewSend,
};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *id) {
    if (id && std::strcmp(id, CLAP_EXT_WEBVIEW) == 0)
        return &kHostWebview;
    return nullptr;
}
void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui Gain WebView resource tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
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

bool contains(const std::string &text, const char *needle) {
    return needle && text.find(needle) != std::string::npos;
}

} // namespace

int main() {
    using namespace webview_gui::examples::gain;

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

    // The Gain editor uses the shared ClapWebviewGui adapter. Headless CI tests
    // only the host-owned CLAP WebView path here; Cocoa/HWND/X11 attachment is
    // qualified by the framework's dedicated platform lifecycle tests.
    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    if (!gui) {
        std::cerr << "Gain must expose clap.gui through ClapWebviewGui\n";
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

    if (gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, true) ||
        !gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false)) {
        std::cerr << "Gain clap.gui create() did not enforce embedded WebView semantics\n";
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

    if (!gui->can_resize(plugin) || !gui->set_size(plugin, 640, 360) ||
        !gui->get_size(plugin, &width, &height) || width != 640 || height != 360) {
        std::cerr << "Gain clap.gui logical resize contract failed\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 19;
    }

    gui->destroy(plugin);
    if (!gui->create(plugin, CLAP_WINDOW_API_WEBVIEW, false)) {
        std::cerr << "Gain clap.gui could not be recreated after destroy()\n";
        plugin->destroy(plugin);
        return 20;
    }
    if (!gui->get_size(plugin, &width, &height) || width != 480 || height != 320) {
        std::cerr << "Gain clap.gui recreate did not restore the initial logical size\n";
        gui->destroy(plugin);
        plugin->destroy(plugin);
        return 21;
    }
    gui->destroy(plugin);

    plugin->destroy(plugin);
    return 0;
}
