#include "gain_plugin.h"
#include "gain_persistent_state.h"
#include "gain_preset_state.h"
#include "gain_webview_parameter_bridge.h"
#include "webview-gui/clap-webview-gui.h"

#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace webview_gui::examples::gain {
namespace {

using GainBase = clap::helpers::Plugin<
    clap::helpers::MisbehaviourHandler::Terminate,
    clap::helpers::CheckingLevel::Minimal>;

static_assert(std::atomic<float>::is_always_lock_free,
              "Gain parameter snapshots must be lock-free on supported targets");
static_assert(std::atomic<bool>::is_always_lock_free,
              "Bypass parameter snapshots must be lock-free on supported targets");
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "Gain state handoff revision must be lock-free on supported targets");
static_assert(sizeof(float) == sizeof(uint32_t), "Gain meter requires a 32-bit float");
static_assert(std::numeric_limits<float>::is_iec559,
              "Gain meter requires IEEE-754 single precision");
static_assert(sizeof(double) == sizeof(uint64_t), "Gain state requires a 64-bit double");
static_assert(std::numeric_limits<double>::is_iec559,
              "Gain state requires IEEE-754 double precision");

const char *const kFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    kGainPluginId,
    "webview-gui Gain",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "",
    "",
    "0.1.0",
    "Stereo Gain reference effect",
    kFeatures,
};

constexpr std::array<uint8_t, 8> kStateMagic{{'W', 'V', 'G', 'G', 'A', 'I', 'N', 0}};
constexpr uint32_t kStateVersion = 1;
constexpr std::size_t kStateSize = 24;
constexpr const char kEditorUri[] = "/index.html";
constexpr const char kEditorMime[] = "text/html; charset=utf-8";
constexpr const char kEditorScriptUri[] = "/gain.js";
constexpr const char kEditorScriptMime[] = "text/javascript; charset=utf-8";
constexpr uint32_t kEditorWidth = 480;
constexpr uint32_t kEditorHeight = 320;
constexpr std::size_t kUiParameterMessageSize = 16;
constexpr std::size_t kUiMeterMessageSize = 16;
constexpr uint8_t kUiGainParameter = 1;
constexpr uint8_t kUiBypassParameter = 2;
constexpr const char kEditorHtml[] = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; img-src data:">
<title>webview-gui Gain</title>
<style>
:root { color-scheme: dark; font-family: system-ui, sans-serif; background: #171717; color: #f2f2f2; }
body { margin: 0; min-height: 100vh; display: grid; place-items: center; }
main { width: min(30rem, calc(100vw - 2rem)); display: grid; gap: 1.25rem; padding: 1.5rem; box-sizing: border-box; }
header { display: flex; align-items: baseline; justify-content: space-between; gap: 1rem; }
h1 { margin: 0; font-size: 1.1rem; font-weight: 650; }
label { display: grid; gap: .5rem; }
input[type="range"] { width: 100%; }
.row { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
.meters { display: grid; grid-template-columns: 1fr 1fr; gap: .75rem; }
meter { width: 100%; }
small { opacity: .7; }
</style>
</head>
<body>
<main>
<header><h1>Gain</h1><output id="gain-value" for="gain">0.00 dB</output></header>
<label>Gain<input id="gain" type="range" min="-60" max="12" step="0.1" value="0"></label>
<label class="row"><span>Bypass</span><input id="bypass" type="checkbox"></label>
<section class="meters" aria-label="Stereo output meter">
<label>L<meter id="meter-left" min="0" max="1" value="0"></meter></label>
<label>R<meter id="meter-right" min="0" max="1" value="0"></meter></label>
</section>
<small>Edits are sent as live CLAP parameter gestures through the WebView bridge.</small>
</main>
<script src="gain.js" defer></script>
</body>
</html>
)html";

constexpr const char kEditorScript[] = R"js((() => {
"use strict";

const KIND_BEGIN = 1;
const KIND_VALUE = 2;
const KIND_END = 3;
const PARAM_GAIN = 1;
const PARAM_BYPASS = 2;
const syncRequest = new ArrayBuffer(4);
const bytes = new Uint8Array(syncRequest);
bytes.set([0x57, 0x56, 0x51, 0x31]);

function encode(kind, parameter, value = 0) {
    const buffer = new ArrayBuffer(16);
    const bytes = new Uint8Array(buffer);
    bytes.set([0x57, 0x56, 0x47, 0x31]);
    bytes[4] = kind;
    bytes[5] = parameter;
    new DataView(buffer).setFloat64(8, value, true);
    return buffer;
}

function send(kind, parameter, value = 0) {
    window.parent.postMessage(encode(kind, parameter, value), "*");
}

function requestSync() {
    window.parent.postMessage(syncRequest, "*");
}

const gain = document.getElementById("gain");
const gainValue = document.getElementById("gain-value");
const bypass = document.getElementById("bypass");
const meterLeft = document.getElementById("meter-left");
const meterRight = document.getElementById("meter-right");
let gainGestureOpen = false;

function beginGain() {
    if (gainGestureOpen)
        return;
    send(KIND_BEGIN, PARAM_GAIN);
    gainGestureOpen = true;
}

function endGain() {
    if (!gainGestureOpen)
        return;
    send(KIND_END, PARAM_GAIN);
    gainGestureOpen = false;
}

function applyUiSync(data) {
    if (!(data instanceof ArrayBuffer) || data.byteLength !== 16)
        return;
    const bytes = new Uint8Array(data);
    if (bytes[0] !== 0x57 || bytes[1] !== 0x56)
        return;

    if (bytes[2] === 0x4d && bytes[3] === 0x31) {
        const view = new DataView(data);
        const left = view.getFloat32(4, true);
        const right = view.getFloat32(8, true);
        if (!Number.isFinite(left) || !Number.isFinite(right) || left < 0 || right < 0)
            return;
        meterLeft.value = String(Math.min(left, 1));
        meterRight.value = String(Math.min(right, 1));
        return;
    }

    if (bytes[2] !== 0x55 || bytes[3] !== 0x31 ||
        bytes[5] !== 0 || bytes[6] !== 0 || bytes[7] !== 0)
        return;
    const value = new DataView(data).getFloat64(8, true);
    if (!Number.isFinite(value))
        return;
    if (bytes[4] === PARAM_GAIN && !gainGestureOpen) {
        gain.value = String(value);
        gainValue.textContent = `${value.toFixed(2)} dB`;
    } else if (bytes[4] === PARAM_BYPASS) {
        bypass.checked = value !== 0;
    }
}

window.addEventListener("message", event => applyUiSync(event.data));
requestSync();
setInterval(requestSync, 33);

gain.addEventListener("pointerdown", beginGain);
gain.addEventListener("input", () => {
    beginGain();
    const value = Number(gain.value);
    if (!Number.isFinite(value))
        return;
    send(KIND_VALUE, PARAM_GAIN, value);
    gainValue.textContent = `${value.toFixed(2)} dB`;
});
gain.addEventListener("change", endGain);
gain.addEventListener("pointerup", endGain);
gain.addEventListener("pointercancel", endGain);
gain.addEventListener("blur", endGain);

bypass.addEventListener("change", () => {
    send(KIND_BEGIN, PARAM_BYPASS);
    send(KIND_VALUE, PARAM_BYPASS, bypass.checked ? 1 : 0);
    send(KIND_END, PARAM_BYPASS);
});
})());
)js";

bool copyText(char *destination, std::size_t capacity, const char *text) noexcept {
    if (!destination || capacity == 0 || !text)
        return false;
    const int written = std::snprintf(destination, capacity, "%s", text);
    return written >= 0 && static_cast<std::size_t>(written) < capacity;
}

bool copyExactText(char *destination, std::size_t capacity, const char *text) noexcept {
    if (!destination || !text)
        return false;
    const auto length = std::strlen(text) + 1u;
    if (capacity < length)
        return false;
    std::memcpy(destination, text, length);
    return true;
}

void storeU32Le(uint8_t *destination, uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        destination[i] = static_cast<uint8_t>((value >> (i * 8u)) & 0xffu);
}

void storeU64Le(uint8_t *destination, uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        destination[i] = static_cast<uint8_t>((value >> (i * 8u)) & 0xffu);
}

std::array<uint8_t, kUiParameterMessageSize> encodeUiParameterMessage(uint8_t parameter,
                                                                      double value) noexcept {
    std::array<uint8_t, kUiParameterMessageSize> message{};
    message[0] = 0x57;
    message[1] = 0x56;
    message[2] = 0x55;
    message[3] = 0x31;
    message[4] = parameter;
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    storeU64Le(message.data() + 8, bits);
    return message;
}

std::array<uint8_t, kUiMeterMessageSize> encodeUiMeterMessage(
    const GainMeterSnapshot &snapshot) noexcept {
    std::array<uint8_t, kUiMeterMessageSize> message{};
    message[0] = 0x57;
    message[1] = 0x56;
    message[2] = 0x4d;
    message[3] = 0x31;

    uint32_t leftBits = 0;
    uint32_t rightBits = 0;
    std::memcpy(&leftBits, &snapshot.leftPeak, sizeof(leftBits));
    std::memcpy(&rightBits, &snapshot.rightPeak, sizeof(rightBits));
    storeU32Le(message.data() + 4, leftBits);
    storeU32Le(message.data() + 8, rightBits);
    storeU32Le(message.data() + 12, snapshot.sequence);
    return message;
}

uint32_t loadU32Le(const uint8_t *source) noexcept {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(source[i]) << (i * 8u);
    return value;
}

uint64_t loadU64Le(const uint8_t *source) noexcept {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(source[i]) << (i * 8u);
    return value;
}

bool writeAll(const clap_ostream_t *stream, const uint8_t *data, std::size_t size) noexcept {
    if (!stream || !stream->write || (!data && size != 0))
        return false;

    std::size_t offset = 0;
    while (offset < size) {
        const auto written = stream->write(stream, data + offset, size - offset);
        if (written <= 0 || static_cast<uint64_t>(written) > size - offset)
            return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool readAll(const clap_istream_t *stream, uint8_t *data, std::size_t size) noexcept {
    if (!stream || !stream->read || (!data && size != 0))
        return false;

    std::size_t offset = 0;
    while (offset < size) {
        const auto read = stream->read(stream, data + offset, size - offset);
        if (read <= 0 || static_cast<uint64_t>(read) > size - offset)
            return false;
        offset += static_cast<std::size_t>(read);
    }
    return true;
}

std::array<uint8_t, kStateSize> encodeState(double gainDb, bool bypassed) noexcept {
    std::array<uint8_t, kStateSize> bytes{};
    std::copy(kStateMagic.begin(), kStateMagic.end(), bytes.begin());
    storeU32Le(bytes.data() + 8, kStateVersion);

    uint64_t gainBits = 0;
    std::memcpy(&gainBits, &gainDb, sizeof(gainBits));
    storeU64Le(bytes.data() + 12, gainBits);
    bytes[20] = bypassed ? 1u : 0u;
    return bytes;
}

bool decodeState(const std::array<uint8_t, kStateSize> &bytes,
                 double &gainDb,
                 bool &bypassed) noexcept {
    if (!std::equal(kStateMagic.begin(), kStateMagic.end(), bytes.begin()) ||
        loadU32Le(bytes.data() + 8) != kStateVersion ||
        (bytes[20] != 0u && bytes[20] != 1u) ||
        bytes[21] != 0u || bytes[22] != 0u || bytes[23] != 0u)
        return false;

    const uint64_t gainBits = loadU64Le(bytes.data() + 12);
    std::memcpy(&gainDb, &gainBits, sizeof(gainDb));
    if (!std::isfinite(gainDb) || gainDb < GainProcessor::kMinimumGainDb ||
        gainDb > GainProcessor::kMaximumGainDb)
        return false;

    bypassed = bytes[20] != 0u;
    return true;
}

class GainPlugin final : public GainBase {
public:
    explicit GainPlugin(const clap_host_t *host)
        : GainBase(&kDescriptor, host), host_(host), gui_(clapPlugin(), host) {
        syncParameterSnapshotsFromProcessor();
    }

    ~GainPlugin() override = default;

protected:
    bool init() noexcept override {
        // Keep clap_plugin.init() free of host extension discovery. WCLAP bridges
        // may cross the WASM/native boundary from host.get_extension() and the
        // native host is allowed to inspect plug-in extensions in that callback,
        // which would re-enter strict clap-helpers before init has completed.
        gui_.init();
        guiParameterBridge_.init(host_);
        return true;
    }

    bool activate(double, uint32_t, uint32_t) noexcept override {
        guiParameterBridge_.setActive(true);
        return true;
    }

    void deactivate() noexcept override {
        guiParameterBridge_.setActive(false);
    }

    clap_process_status process(const clap_process_t *processData) noexcept override {
        applyPendingLoadedState();
        if (!processData)
            return CLAP_PROCESS_ERROR;
        guiParameterBridge_.drain(processData->out_events, processor_);
        if (!processor_.process(*processData))
            return CLAP_PROCESS_ERROR;
        syncParameterSnapshotsPreservingConcurrentStateLoad();
        return CLAP_PROCESS_CONTINUE;
    }

    bool enableDraftExtensions() const noexcept override { return true; }
    bool implementsWebview() const noexcept override { return true; }

    int32_t webviewGetUri(char *uri, uint32_t uriCapacity) const noexcept override {
        constexpr auto uriLength = sizeof(kEditorUri);
        static_assert(uriLength <= static_cast<std::size_t>(std::numeric_limits<int32_t>::max()),
                      "Gain WebView URI length must fit the CLAP return type");

        if (uriCapacity == 0)
            return static_cast<int32_t>(uriLength);
        if (!uri)
            return -1;

        const auto copyLength = std::min<std::size_t>(uriLength - 1u, uriCapacity - 1u);
        if (copyLength != 0)
            std::memcpy(uri, kEditorUri, copyLength);
        uri[copyLength] = '\0';
        return static_cast<int32_t>(uriLength);
    }

    bool webviewGetResource(const char *path,
                            char *mime,
                            uint32_t mimeCapacity,
                            const clap_ostream_t *dataStream) override {
        if (!path || !dataStream || !dataStream->write)
            return false;

        const char *resourceMime = nullptr;
        const uint8_t *resourceData = nullptr;
        std::size_t resourceSize = 0;
        if (std::strcmp(path, kEditorUri) == 0) {
            resourceMime = kEditorMime;
            resourceData = reinterpret_cast<const uint8_t *>(kEditorHtml);
            resourceSize = sizeof(kEditorHtml) - 1u;
        } else if (std::strcmp(path, kEditorScriptUri) == 0) {
            resourceMime = kEditorScriptMime;
            resourceData = reinterpret_cast<const uint8_t *>(kEditorScript);
            resourceSize = sizeof(kEditorScript) - 1u;
        } else {
            return false;
        }

        if (!copyExactText(mime, mimeCapacity, resourceMime))
            return false;
        return writeAll(dataStream, resourceData, resourceSize);
    }

    bool webviewReceive(const void *buffer, uint32_t size) const noexcept override {
        if (guiCreated_ && buffer && size == 4) {
            const auto *bytes = static_cast<const uint8_t *>(buffer);
            if (bytes[0] == 0x57 && bytes[1] == 0x56 &&
                bytes[2] == 0x51 && bytes[3] == 0x31)
                return sendUiSnapshotToWebview();
        }
        return guiParameterBridge_.receive(guiCreated_, buffer, size);
    }

    bool implementsGui() const noexcept override { return true; }

    bool guiIsApiSupported(const char *api, bool isFloating) noexcept override {
        return gui_.isApiSupported(api, isFloating);
    }

    bool guiGetPreferredApi(const char **api, bool *isFloating) noexcept override {
        return gui_.getPreferredApi(api, isFloating);
    }

    bool guiCreate(const char *api, bool isFloating) noexcept override {
        if (guiCreated_)
            return false;

        const bool hostOwnedWebview =
            api && std::strcmp(api, ::webview_gui::CLAP_WINDOW_API_WEBVIEW) == 0;
        if (!gui_.create(api, isFloating))
            return false;
        if (!gui_.setSize(kEditorWidth, kEditorHeight)) {
            gui_.destroy();
            return false;
        }

        hasSentMeterSequence_ = false;
        guiCreated_ = true;
        hostOwnedWebviewGui_ = hostOwnedWebview;
        return true;
    }

    void guiDestroy() noexcept override {
        guiParameterBridge_.closeOpenGestures();
        gui_.destroy();
        hasSentMeterSequence_ = false;
        guiCreated_ = false;
        hostOwnedWebviewGui_ = false;
    }

    bool guiSetScale(double) noexcept override {
        return false;
    }

    bool guiShow() noexcept override {
        if (!guiCreated_)
            return false;
        return hostOwnedWebviewGui_ ? true : gui_.show();
    }

    bool guiHide() noexcept override {
        if (!guiCreated_)
            return false;
        return hostOwnedWebviewGui_ ? true : gui_.hide();
    }

    bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override {
        return guiCreated_ && gui_.getSize(width, height);
    }

    bool guiCanResize() const noexcept override {
        return guiCreated_ && gui_.canResize();
    }

    bool guiGetResizeHints(clap_gui_resize_hints_t *hints) noexcept override {
        return guiCreated_ && gui_.getResizeHints(hints);
    }

    bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept override {
        return guiCreated_ && gui_.adjustSize(width, height);
    }

    bool guiSetSize(uint32_t width, uint32_t height) noexcept override {
        return guiCreated_ && gui_.setSize(width, height);
    }

    void guiSuggestTitle(const char *title) noexcept override {
        if (guiCreated_ && !hostOwnedWebviewGui_)
            gui_.suggestTitle(title);
    }

    bool guiSetParent(const clap_window *window) noexcept override {
        if (!guiCreated_)
            return false;
        if (hostOwnedWebviewGui_) {
            return window && window->api &&
                   std::strcmp(window->api, ::webview_gui::CLAP_WINDOW_API_WEBVIEW) == 0 &&
                   window->ptr == nullptr;
        }
        return gui_.setParent(window);
    }

    bool guiSetTransient(const clap_window *window) noexcept override {
        if (!guiCreated_ || hostOwnedWebviewGui_)
            return false;
        return gui_.setTransient(window);
    }

    bool implementsState() const noexcept override { return true; }

    bool stateSave(const clap_ostream_t *stream) noexcept override {
        const auto snapshot = readEffectiveGainParameterSnapshot();
        const auto bytes = encodeState(static_cast<double>(snapshot.gainDb),
                                       snapshot.bypassed);
        return writeAll(stream, bytes.data(), bytes.size());
    }

    bool stateLoad(const clap_istream_t *stream) noexcept override {
        std::array<uint8_t, kStateSize> bytes{};
        if (!readAll(stream, bytes.data(), bytes.size()))
            return false;

        uint8_t trailing = 0;
        const auto trailingRead = stream->read(stream, &trailing, 1);
        if (trailingRead != 0)
            return false;

        double gainDb = 0.0;
        bool bypassed = false;
        if (!decodeState(bytes, gainDb, bypassed))
            return false;

        const GainParameterSnapshot loaded{static_cast<float>(gainDb), bypassed};
        return commitPersistentParameterSnapshot(loaded, true);
    }

    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool) const noexcept override { return 1; }

    bool audioPortsInfo(uint32_t index,
                        bool isInput,
                        clap_audio_port_info_t *info) const noexcept override {
        if (index != 0 || !info)
            return false;
        *info = {};
        info->id = isInput ? kGainInputPortId : kGainOutputPortId;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = isInput ? kGainOutputPortId : kGainInputPortId;
        return copyText(info->name, sizeof(info->name), isInput ? "Stereo In" : "Stereo Out");
    }

    bool implementsParams() const noexcept override { return true; }
    uint32_t paramsCount() const noexcept override { return 2; }

    bool paramsInfo(uint32_t paramIndex, clap_param_info_t *info) const noexcept override {
        if (!info || paramIndex >= 2)
            return false;
        *info = {};
        if (paramIndex == 0) {
            info->id = kGainParamId;
            info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS;
            info->min_value = GainProcessor::kMinimumGainDb;
            info->max_value = GainProcessor::kMaximumGainDb;
            info->default_value = 0.0;
            return copyText(info->name, sizeof(info->name), "Gain");
        }
        info->id = kBypassParamId;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS |
                      CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS;
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return copyText(info->name, sizeof(info->name), "Bypass");
    }

    bool paramsValue(clap_id paramId, double *value) noexcept override {
        if (!value)
            return false;
        if (paramId == kGainParamId) {
            *value = static_cast<double>(gainDbSnapshot_.load(std::memory_order_relaxed));
            return true;
        }
        if (paramId == kBypassParamId) {
            *value = bypassSnapshot_.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            return true;
        }
        return false;
    }

    bool paramsValueToText(clap_id paramId,
                           double value,
                           char *display,
                           uint32_t size) noexcept override {
        if (!display || size == 0 || !std::isfinite(value))
            return false;
        int written = -1;
        if (paramId == kGainParamId &&
            value >= GainProcessor::kMinimumGainDb && value <= GainProcessor::kMaximumGainDb) {
            written = std::snprintf(display, size, "%.2f dB", value);
        } else if (paramId == kBypassParamId && value >= 0.0 && value <= 1.0) {
            written = std::snprintf(display, size, "%s",
                                    static_cast<int32_t>(value) != 0 ? "On" : "Off");
        }
        return written >= 0 && static_cast<uint32_t>(written) < size;
    }

    bool paramsTextToValue(clap_id paramId,
                           const char *display,
                           double *value) noexcept override {
        if (!display || !value)
            return false;
        if (paramId == kBypassParamId) {
            if (std::strcmp(display, "On") == 0 || std::strcmp(display, "1") == 0) {
                *value = 1.0;
                return true;
            }
            if (std::strcmp(display, "Off") == 0 || std::strcmp(display, "0") == 0) {
                *value = 0.0;
                return true;
            }
            return false;
        }
        if (paramId != kGainParamId)
            return false;
        char *end = nullptr;
        const double parsed = std::strtod(display, &end);
        if (end == display || !std::isfinite(parsed) ||
            parsed < GainProcessor::kMinimumGainDb || parsed > GainProcessor::kMaximumGainDb)
            return false;
        while (*end == ' ' || *end == '\t')
            ++end;
        if (end[0] == 'd' && end[1] == 'B')
            end += 2;
        while (*end == ' ' || *end == '\t')
            ++end;
        if (*end != '\0')
            return false;
        *value = parsed;
        return true;
    }

    void paramsFlush(const clap_input_events_t *in,
                     const clap_output_events_t *out) noexcept override {
        applyPendingLoadedState();
        if (in && in->size && in->get) {
            const uint32_t count = in->size(in);
            for (uint32_t index = 0; index < count; ++index) {
                const auto *header = in->get(in, index);
                if (!header || header->size < sizeof(clap_event_header_t))
                    continue;
                if (header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
                    header->type != CLAP_EVENT_PARAM_VALUE ||
                    header->size < sizeof(clap_event_param_value_t))
                    continue;
                const auto &event = *reinterpret_cast<const clap_event_param_value_t *>(header);
                (void)processor_.applyParameterValue(event);
            }
        }
        guiParameterBridge_.drain(out, processor_);
        syncParameterSnapshotsPreservingConcurrentStateLoad();
    }

    int32_t getParamIndexForParamId(clap_id paramId) const noexcept override {
        if (paramId == kGainParamId)
            return 0;
        if (paramId == kBypassParamId)
            return 1;
        return -1;
    }

private:
    [[nodiscard]] GainParameterSnapshot readEffectiveGainParameterSnapshot() const noexcept {
        return {gainDbSnapshot_.load(std::memory_order_relaxed),
                bypassSnapshot_.load(std::memory_order_relaxed)};
    }

    bool commitPersistentParameterSnapshot(const GainParameterSnapshot &snapshot,
                                           bool notifyHostParams) noexcept {
        const bool parameterValuesChanged =
            gainDbSnapshot_.load(std::memory_order_relaxed) != snapshot.gainDb ||
            bypassSnapshot_.load(std::memory_order_relaxed) != snapshot.bypassed;

        pendingLoadedGainDb_.store(snapshot.gainDb, std::memory_order_relaxed);
        pendingLoadedBypass_.store(snapshot.bypassed, std::memory_order_relaxed);
        loadedStateRevision_.fetch_add(1u, std::memory_order_release);
        gainDbSnapshot_.store(snapshot.gainDb, std::memory_order_relaxed);
        bypassSnapshot_.store(snapshot.bypassed, std::memory_order_relaxed);

        if (notifyHostParams && parameterValuesChanged) {
            const auto *hostParams = resolveHostParams();
            if (hostParams && hostParams->rescan)
                hostParams->rescan(host_, CLAP_PARAM_RESCAN_VALUES);
        }
        return true;
    }

    [[nodiscard]] presets::PresetDocument captureGainPresetDocument(
        presets::PresetMetadata metadata) const {
        return captureGainPreset(readEffectiveGainParameterSnapshot(), metadata);
    }

    presets::PresetStateAdapterError applyGainPresetDocument(
        const presets::PresetDocument &document) noexcept {
        const auto mapped = makeGainPresetCandidate(document);
        if (!mapped.ok())
            return mapped.error;
        (void)commitPersistentParameterSnapshot(*mapped.candidate, false);
        return presets::PresetStateAdapterError::None;
    }

    const clap_host_params_t *resolveHostParams() noexcept {
        if (!hostParamsResolved_) {
            hostParamsResolved_ = true;
            if (host_ && host_->get_extension) {
                hostParams_ = static_cast<const clap_host_params_t *>(
                    host_->get_extension(host_, CLAP_EXT_PARAMS));
            }
        }
        return hostParams_;
    }

    bool sendParameterSnapshotToWebview() const noexcept {
        const auto gainMessage = encodeUiParameterMessage(
            kUiGainParameter,
            static_cast<double>(gainDbSnapshot_.load(std::memory_order_relaxed)));
        const auto bypassMessage = encodeUiParameterMessage(
            kUiBypassParameter,
            bypassSnapshot_.load(std::memory_order_relaxed) ? 1.0 : 0.0);
        const bool gainSent = gui_.send(gainMessage.data(), gainMessage.size());
        const bool bypassSent = gui_.send(bypassMessage.data(), bypassMessage.size());
        return gainSent && bypassSent;
    }

    bool sendMeterSnapshotToWebview() const noexcept {
        GainMeterSnapshot snapshot{};
        if (!processor_.tryReadMeter(snapshot))
            return true;
        if (hasSentMeterSequence_ && snapshot.sequence == lastMeterSequenceSent_)
            return true;

        const auto message = encodeUiMeterMessage(snapshot);
        if (!gui_.send(message.data(), message.size()))
            return false;

        lastMeterSequenceSent_ = snapshot.sequence;
        hasSentMeterSequence_ = true;
        return true;
    }

    bool sendUiSnapshotToWebview() const noexcept {
        return sendParameterSnapshotToWebview() && sendMeterSnapshotToWebview();
    }

    void applyPendingLoadedState() noexcept {
        const auto revision = loadedStateRevision_.load(std::memory_order_acquire);
        if (revision == appliedLoadedStateRevision_)
            return;

        const auto gain = pendingLoadedGainDb_.load(std::memory_order_relaxed);
        const auto bypassed = pendingLoadedBypass_.load(std::memory_order_relaxed);
        (void)processor_.processor().setGainDb(static_cast<double>(gain));
        processor_.processor().setBypassed(bypassed);
        appliedLoadedStateRevision_ = revision;
    }

    void syncParameterSnapshotsFromProcessor() noexcept {
        gainDbSnapshot_.store(static_cast<float>(processor_.processor().gainDb()),
                              std::memory_order_relaxed);
        bypassSnapshot_.store(processor_.processor().bypassed(), std::memory_order_relaxed);
    }

    void restoreSnapshotsFromPendingState() noexcept {
        gainDbSnapshot_.store(pendingLoadedGainDb_.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
        bypassSnapshot_.store(pendingLoadedBypass_.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
    }

    void syncParameterSnapshotsPreservingConcurrentStateLoad() noexcept {
        const auto revisionBefore = loadedStateRevision_.load(std::memory_order_acquire);
        if (revisionBefore != appliedLoadedStateRevision_) {
            restoreSnapshotsFromPendingState();
            return;
        }

        syncParameterSnapshotsFromProcessor();

        const auto revisionAfter = loadedStateRevision_.load(std::memory_order_acquire);
        if (revisionAfter != revisionBefore)
            restoreSnapshotsFromPendingState();
    }

    const clap_host_t *host_ = nullptr;
    const clap_host_params_t *hostParams_ = nullptr;
    bool hostParamsResolved_ = false;
    GainEventProcessor processor_{};
    mutable ::webview_gui::ClapWebviewGui gui_;
    mutable GainWebviewParameterBridge guiParameterBridge_{};
    mutable uint32_t lastMeterSequenceSent_ = 0u;
    mutable bool hasSentMeterSequence_ = false;
    bool guiCreated_ = false;
    bool hostOwnedWebviewGui_ = false;
    std::atomic<float> gainDbSnapshot_{0.0f};
    std::atomic<bool> bypassSnapshot_{false};

    std::atomic<float> pendingLoadedGainDb_{0.0f};
    std::atomic<bool> pendingLoadedBypass_{false};
    std::atomic<uint32_t> loadedStateRevision_{0};
    uint32_t appliedLoadedStateRevision_ = 0;
};

uint32_t CLAP_ABI factoryGetPluginCount(const clap_plugin_factory_t *) { return 1; }

const clap_plugin_descriptor_t *CLAP_ABI factoryGetPluginDescriptor(
    const clap_plugin_factory_t *, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t *CLAP_ABI factoryCreatePlugin(
    const clap_plugin_factory_t *, const clap_host_t *host, const char *pluginId) {
    if (!host || !pluginId || !clap_version_is_compatible(host->clap_version) ||
        std::strcmp(pluginId, kGainPluginId) != 0)
        return nullptr;
    auto *instance = new (std::nothrow) GainPlugin(host);
    return instance ? instance->clapPlugin() : nullptr;
}

const clap_plugin_factory_t kFactory{
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

} // namespace

const clap_plugin_factory_t *gainFactory() noexcept { return &kFactory; }

} // namespace webview_gui::examples::gain