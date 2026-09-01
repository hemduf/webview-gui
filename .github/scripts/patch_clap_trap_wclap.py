#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


WEBVIEW_HELPERS = r'''
static constexpr const char kWclapWebviewExtensionId[] = "clap.webview/3";
static constexpr const char kWclapWebviewApi[] = "webview";

struct WclapPluginWebviewSmoke {
    int32_t (CLAP_ABI *get_uri)(const clap_plugin_t *, char *, uint32_t);
    bool (CLAP_ABI *get_resource)(const clap_plugin_t *,
                                  const char *,
                                  char *,
                                  uint32_t,
                                  const clap_ostream_t *);
    bool (CLAP_ABI *receive)(const clap_plugin_t *, const void *, uint32_t);
};

struct WclapHostWebviewSmoke {
    bool (CLAP_ABI *send)(const clap_host_t *, const void *, uint32_t);
};

static uint32_t wclapWebviewSendCalls = 0;

static bool CLAP_ABI wclapHostWebviewSend(const clap_host_t *,
                                          const void *buffer,
                                          uint32_t size) {
    if (!buffer && size != 0)
        return false;
    ++wclapWebviewSendCalls;
    return true;
}

static const WclapHostWebviewSmoke wclapHostWebview{wclapHostWebviewSend};

static const void *wclapHostExtension(const char *id) {
    if (id && std::strcmp(id, kWclapWebviewExtensionId) == 0)
        return &wclapHostWebview;
    return nullptr;
}

struct WclapResourceSink {
    clap_ostream_t stream{};
    uint64_t size = 0;

    WclapResourceSink() {
        stream.ctx = this;
        stream.write = write;
    }

    static int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                  const void *buffer,
                                  uint64_t length) {
        if (!stream || !stream->ctx || (!buffer && length != 0))
            return -1;
        auto &self = *static_cast<WclapResourceSink *>(stream->ctx);
        static constexpr uint64_t kMaxSmokeResourceBytes = 16u * 1024u * 1024u;
        if (length > kMaxSmokeResourceBytes - self.size)
            return -1;
        self.size += length;
        return static_cast<int64_t>(length);
    }
};

static bool runWclapWebviewSmoke(const clap_plugin_t *plugin) {
    if (!plugin || !plugin->get_extension) {
        fprintf(stderr, "  ✗ WCLAP WebView smoke: missing plug-in extension entrypoint\n");
        return false;
    }

    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto *webview = static_cast<const WclapPluginWebviewSmoke *>(
        plugin->get_extension(plugin, kWclapWebviewExtensionId));
    if (!gui || !webview || !gui->is_api_supported || !gui->create ||
        !gui->destroy || !gui->get_size ||
        !webview->get_uri || !webview->get_resource || !webview->receive) {
        fprintf(stderr, "  ✗ WCLAP WebView smoke: incomplete clap.gui/clap.webview/3 path\n");
        return false;
    }

    if (!gui->is_api_supported(plugin, kWclapWebviewApi, false)) {
        fprintf(stderr, "  ✗ WCLAP WebView smoke: webview API not supported\n");
        return false;
    }
    if (!gui->create(plugin, kWclapWebviewApi, false)) {
        fprintf(stderr, "  ✗ WCLAP WebView smoke: gui.create(webview) failed\n");
        return false;
    }

    auto failCreated = [&](const char *message) {
        fprintf(stderr, "  ✗ WCLAP WebView smoke: %s\n", message);
        gui->destroy(plugin);
        return false;
    };

    uint32_t width = 0;
    uint32_t height = 0;
    if (!gui->get_size(plugin, &width, &height) || width == 0 || height == 0)
        return failCreated("GUI size did not cross the real WCLAP bridge");

    char uri[2048]{};
    const auto uriLength = webview->get_uri(plugin, uri, sizeof(uri));
    if (uriLength <= 0 || static_cast<uint32_t>(uriLength) > sizeof(uri) || uri[0] == '\0')
        return failCreated("clap.webview/3 get_uri failed");
    if (uri[0] != '/')
        return failCreated("expected bundled absolute-path WebView URI");

    char mime[128]{};
    WclapResourceSink resource;
    if (!webview->get_resource(plugin, uri, mime, sizeof(mime), &resource.stream) ||
        resource.size == 0 || mime[0] == '\0')
        return failCreated("bundled WebView resource could not be loaded through bridge");

    const uint8_t syncMessage[] = {__SYNC_BYTES__};
    const auto sendsBefore = wclapWebviewSendCalls;
    if (!webview->receive(plugin, syncMessage, sizeof(syncMessage)))
        return failCreated("WebView message did not reach module");
    if (wclapWebviewSendCalls <= sendsBefore)
        return failCreated("module reply did not reach native clap_host_webview.send");

    gui->destroy(plugin);
    printf("  ✓ WCLAP WebView bridge smoke (create/size/resource/message round-trip)\n");
    return true;
}
'''


def replace_required(
    text: str,
    needle: str,
    replacement: str,
    description: str,
    count: int | None = None,
) -> tuple[str, int]:
    occurrences = text.count(needle)
    if occurrences == 0:
        raise SystemExit(
            f"pinned clap-trap {description} changed; revalidate WCLAP host patch"
        )
    if count is None:
        return text.replace(needle, replacement), occurrences
    return text.replace(needle, replacement, count), occurrences


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--sync-magic", required=True)
    parser.add_argument("--instrument", action="store_true")
    args = parser.parse_args()

    if len(args.sync_magic) != 4 or not args.sync_magic.isascii():
        raise SystemExit("--sync-magic must be exactly four ASCII characters")

    path = Path(args.source)
    text = path.read_text()

    text, load_count = replace_required(
        text,
        "PluginLoader::load(opts.pluginPath)",
        "PluginLoader::create(opts.pluginPath)",
        "loader pattern",
    )
    text, entry_count = replace_required(
        text,
        "if (!loader->entry()) {",
        "if (!loader->isWasm() && !loader->entry()) {",
        "entry guard",
    )

    helper_anchor = "using namespace clap_trap;\n"
    sync_bytes = ", ".join(f"0x{ord(ch):02x}" for ch in args.sync_magic)
    helpers = WEBVIEW_HELPERS.replace("__SYNC_BYTES__", sync_bytes)
    text, _ = replace_required(
        text,
        helper_anchor,
        helper_anchor + "\n" + helpers + "\n",
        "namespace anchor",
        1,
    )

    host_needle = "    int failures = 0;\n    TestHost host;\n"
    host_replacement = (
        "    int failures = 0;\n"
        "    TestHost host;\n"
        "    host.setExtensionCallback([](const char *id) -> const void * {\n"
        "        return wclapHostExtension(id);\n"
        "    });\n"
    )
    text, _ = replace_required(
        text,
        host_needle,
        host_replacement,
        "validate TestHost setup",
        1,
    )

    init_needle = (
        '        printf("  ✓ init()\\n");\n\n'
        '        if (!plugin->activate(plugin, opts.sampleRate, opts.bufferSize, opts.bufferSize)) {'
    )
    init_replacement = (
        '        printf("  ✓ init()\\n");\n\n'
        '        if (!runWclapWebviewSmoke(plugin)) {\n'
        '            plugin->destroy(plugin);\n'
        '            failures++;\n'
        '            continue;\n'
        '        }\n\n'
        '        if (!plugin->activate(plugin, opts.sampleRate, opts.bufferSize, opts.bufferSize)) {'
    )
    text, _ = replace_required(
        text,
        init_needle,
        init_replacement,
        "validate init/activate transition",
        1,
    )

    io_patch = "none"
    if args.instrument:
        validate_io_needle = (
            "        process.audio_inputs = buffers.inputBuffer();\n"
            "        process.audio_outputs = buffers.outputBuffer();\n"
            "        process.audio_inputs_count = 1;\n"
            "        process.audio_outputs_count = 1;"
        )
        validate_io_replacement = (
            "        const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(\n"
            "            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));\n"
            "        const uint32_t audioInputCount =\n"
            "            audioPorts && audioPorts->count ? audioPorts->count(plugin, true) : 0u;\n"
            "        process.audio_inputs = audioInputCount == 0u ? nullptr : buffers.inputBuffer();\n"
            "        process.audio_outputs = buffers.outputBuffer();\n"
            "        process.audio_inputs_count = audioInputCount;\n"
            "        process.audio_outputs_count = 1;"
        )
        text, io_count = replace_required(
            text,
            validate_io_needle,
            validate_io_replacement,
            "validate audio topology",
            1,
        )
        io_patch = f"instrument audio topology ({io_count} match)"

    path.write_text(text)
    print(
        f"patched clap-trap: {load_count} loader call(s), {entry_count} entry guard(s), "
        f"real WebView bridge smoke ({args.sync_magic}), {io_patch}"
    )


if __name__ == "__main__":
    main()
