#include "webview-gui/_impl/standalone_device_bridge.h"

#include <clap/clap.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using webview_gui::detail::StandaloneDeviceBridge;

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "standalone-device-bridge test failed: %s\n", message);
    std::exit(1);
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

struct FakeHostState {
    bool exposeExtension = false;
    int32_t lastAudioApi = -1;
    clap_wrapper_standalone_audio_configuration configuration{};
    clap_wrapper_standalone_device_kind lastMidiKind = CLAP_WRAPPER_STANDALONE_MIDI_INPUT;
    uint32_t lastMidiId = 0;
    bool lastMidiEnabled = false;
    uint32_t midiSetCalls = 0;
    uint32_t refreshCalls = 0;
};

FakeHostState *state(const clap_host_t *host)
{
    return static_cast<FakeHostState *>(host ? host->host_data : nullptr);
}

template <size_t N>
void copyText(char (&destination)[N], const char *text)
{
    std::snprintf(destination, N, "%s", text ? text : "");
}

uint32_t CLAP_ABI audioApiCount(const clap_host_t *) { return 2; }

bool CLAP_ABI audioApiInfo(const clap_host_t *, uint32_t index,
                           clap_wrapper_standalone_audio_api_info *info)
{
    if (!info || index >= 2)
        return false;

    // Intentionally fail the first item. The bridge must still produce valid
    // JSON without a leading comma before the second item.
    if (index == 0)
        return false;

    *info = {};
    info->id = 7;
    copyText(info->name, "core");
    copyText(info->display_name, "Core Audio");
    info->selected = true;
    return true;
}

bool CLAP_ABI setAudioApi(const clap_host_t *host, int32_t apiId)
{
    auto *s = state(host);
    if (!s) return false;
    s->lastAudioApi = apiId;
    return true;
}

uint32_t CLAP_ABI deviceCount(const clap_host_t *, clap_wrapper_standalone_device_kind)
{
    return 1;
}

bool CLAP_ABI deviceInfo(const clap_host_t *, clap_wrapper_standalone_device_kind kind,
                         uint32_t index, clap_wrapper_standalone_device_info *info)
{
    if (!info || index != 0)
        return false;

    *info = {};
    switch (kind) {
        case CLAP_WRAPPER_STANDALONE_AUDIO_INPUT:
            info->id = 101;
            copyText(info->name, "Audio Input");
            info->input_channels = 2;
            info->is_default = true;
            info->selected = true;
            return true;
        case CLAP_WRAPPER_STANDALONE_AUDIO_OUTPUT:
            info->id = 202;
            copyText(info->name, "Audio Output");
            info->output_channels = 2;
            info->is_default = true;
            info->selected = true;
            return true;
        case CLAP_WRAPPER_STANDALONE_MIDI_INPUT:
            info->id = 0;
            copyText(info->name, "MIDI Input");
            info->selected = true;
            return true;
        case CLAP_WRAPPER_STANDALONE_MIDI_OUTPUT:
            info->id = 0;
            copyText(info->name, "MIDI Output");
            return true;
    }
    return false;
}

bool CLAP_ABI getAudioConfiguration(
    const clap_host_t *host,
    clap_wrapper_standalone_audio_configuration *configuration)
{
    auto *s = state(host);
    if (!s || !configuration) return false;
    *configuration = s->configuration;
    return true;
}

bool CLAP_ABI setAudioConfiguration(
    const clap_host_t *host,
    const clap_wrapper_standalone_audio_configuration *configuration)
{
    auto *s = state(host);
    if (!s || !configuration) return false;
    s->configuration = *configuration;
    return true;
}

uint32_t CLAP_ABI sampleRateCount(const clap_host_t *) { return 2; }

bool CLAP_ABI sampleRate(const clap_host_t *, uint32_t index, uint32_t *value)
{
    if (!value || index >= 2) return false;
    *value = index == 0 ? 44100u : 48000u;
    return true;
}

uint32_t CLAP_ABI bufferSizeCount(const clap_host_t *) { return 2; }

bool CLAP_ABI bufferSize(const clap_host_t *, uint32_t index, uint32_t *value)
{
    if (!value || index >= 2) return false;
    *value = index == 0 ? 128u : 256u;
    return true;
}

bool CLAP_ABI setMidiDeviceEnabled(const clap_host_t *host,
                                   clap_wrapper_standalone_device_kind kind,
                                   uint32_t id,
                                   bool enabled)
{
    auto *s = state(host);
    if (!s) return false;
    s->lastMidiKind = kind;
    s->lastMidiId = id;
    s->lastMidiEnabled = enabled;
    ++s->midiSetCalls;
    return true;
}

bool CLAP_ABI refreshMidiDevices(const clap_host_t *host)
{
    auto *s = state(host);
    if (!s) return false;
    ++s->refreshCalls;
    return true;
}

const clap_wrapper_host_standalone_device_control kDeviceControl{
    audioApiCount,
    audioApiInfo,
    setAudioApi,
    deviceCount,
    deviceInfo,
    getAudioConfiguration,
    setAudioConfiguration,
    sampleRateCount,
    sampleRate,
    bufferSizeCount,
    bufferSize,
    setMidiDeviceEnabled,
    refreshMidiDevices,
};

const void *CLAP_ABI getExtension(const clap_host_t *host, const char *extension)
{
    auto *s = state(host);
    if (!s || !s->exposeExtension || !extension)
        return nullptr;
    return std::strcmp(extension, CLAP_WRAPPER_EXT_STANDALONE_DEVICE_CONTROL) == 0
        ? &kDeviceControl
        : nullptr;
}

void CLAP_ABI ignoreHostRequest(const clap_host_t *) {}

clap_host_t makeHost(FakeHostState &s)
{
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.host_data = &s;
    host.name = "standalone bridge test";
    host.vendor = "webview-gui";
    host.url = "https://example.invalid";
    host.version = "1";
    host.get_extension = getExtension;
    host.request_restart = ignoreHostRequest;
    host.request_process = ignoreHostRequest;
    host.request_callback = ignoreHostRequest;
    return host;
}

void storeU32(unsigned char *bytes, uint32_t value)
{
    bytes[0] = static_cast<unsigned char>(value & 0xffu);
    bytes[1] = static_cast<unsigned char>((value >> 8u) & 0xffu);
    bytes[2] = static_cast<unsigned char>((value >> 16u) & 0xffu);
    bytes[3] = static_cast<unsigned char>((value >> 24u) & 0xffu);
}

std::string bytesToString(const std::vector<unsigned char> &bytes, size_t offset = 0)
{
    return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
}

} // namespace

int main()
{
    FakeHostState fake{};
    fake.configuration.input_device_id = 101;
    fake.configuration.output_device_id = 202;
    fake.configuration.input_enabled = true;
    fake.configuration.output_enabled = true;
    fake.configuration.plugin_has_input = true;
    fake.configuration.plugin_has_output = true;
    fake.configuration.input_channels = 2;
    fake.configuration.output_channels = 2;
    fake.configuration.sample_rate = 48000;
    fake.configuration.buffer_size = 256;

    auto host = makeHost(fake);
    StandaloneDeviceBridge bridge{&host};

    WebviewGui::Resource html;
    html.mediaType = "text/html; charset=utf-8";
    const std::string originalHtml = "<!doctype html><html><body>editor</body></html>";
    html.bytes.assign(originalHtml.begin(), originalHtml.end());

    require(bridge.initScript().empty(),
            "non-standalone host must not expose a native init script");
    bridge.injectIntoHtml(html);
    require(bytesToString(html.bytes) == originalHtml,
            "non-standalone host must not receive injected UI");

    // Re-use the same bridge after an initial miss. A miss must not be cached.
    fake.exposeExtension = true;
    const auto initScript = bridge.initScript();
    require(!initScript.empty(), "standalone native init script missing after late discovery");
    require(initScript.find("__webviewGuiStandaloneDevices") != std::string::npos,
            "standalone init-script guard missing");
    require(initScript.find("nativeReceiverInstalled") != std::string::npos,
            "native receiver retry state missing");
    require(initScript.find("probeAttempts") != std::string::npos,
            "bounded probe state missing");
    require(initScript.find("setTimeout(probe,50)") != std::string::npos,
            "receiver retry timer missing");
    require(initScript.find("DOMContentLoaded") != std::string::npos,
            "native init script must wait for the editor DOM before creating controls");

    bridge.injectIntoHtml(html);
    const auto injectedHtml = bytesToString(html.bytes);
    require(injectedHtml.find("webview-gui-standalone-devices") != std::string::npos,
            "standalone style injection missing");
    require(injectedHtml.find("<script") == std::string::npos,
            "standalone controller must not be injected into CSP-protected HTML");

    WebviewGui::Resource editorScript;
    editorScript.mediaType = "text/javascript; charset=utf-8";
    const std::string originalScript = "window.editorLoaded=true;";
    editorScript.bytes.assign(originalScript.begin(), originalScript.end());
    bridge.injectIntoHtml(editorScript);
    require(bytesToString(editorScript.bytes) == originalScript,
            "standalone host must not mutate plug-in JavaScript resources");

    std::vector<unsigned char> response;
    const auto sender = [&response](const unsigned char *data, size_t size) {
        response.assign(data, data + size);
        return true;
    };

    const unsigned char query[4]{'W', 'V', 'D', 'Q'};
    require(bridge.receive(query, sizeof(query), sender), "WVDQ was not consumed");
    require(response.size() > 4 && std::memcmp(response.data(), "WVDJ", 4) == 0,
            "WVDQ did not produce WVDJ");
    const auto json = bytesToString(response, 4);
    require(json.find("\"audioApis\":[{") != std::string::npos,
            "audio API JSON array is malformed");
    require(json.find("\"audioApis\":[,") == std::string::npos,
            "failed audio API entry produced a leading comma");
    require(json.find("Core Audio") != std::string::npos,
            "audio API snapshot missing expected device API");
    require(json.find("Audio Output") != std::string::npos,
            "audio output snapshot missing");
    require(json.find("MIDI Input") != std::string::npos,
            "MIDI input snapshot missing");

    unsigned char apiCommand[8]{'W', 'V', 'A', 'P'};
    storeU32(apiCommand + 4, 11);
    require(bridge.receive(apiCommand, sizeof(apiCommand), sender), "WVAP was not consumed");
    require(fake.lastAudioApi == 11, "WVAP did not reach host extension");

    unsigned char audioCommand[24]{'W', 'V', 'D', 'A'};
    audioCommand[4] = 1;
    audioCommand[5] = 1;
    storeU32(audioCommand + 8, 303);
    storeU32(audioCommand + 12, 404);
    storeU32(audioCommand + 16, 44100);
    storeU32(audioCommand + 20, 128);
    require(bridge.receive(audioCommand, sizeof(audioCommand), sender), "WVDA was not consumed");
    require(fake.configuration.input_device_id == 303 &&
            fake.configuration.output_device_id == 404 &&
            fake.configuration.sample_rate == 44100 &&
            fake.configuration.buffer_size == 128,
            "WVDA did not reach host extension");

    unsigned char midiCommand[12]{'W', 'V', 'D', 'M'};
    midiCommand[4] = CLAP_WRAPPER_STANDALONE_MIDI_OUTPUT;
    midiCommand[5] = 1;
    storeU32(midiCommand + 8, 3);
    require(bridge.receive(midiCommand, sizeof(midiCommand), sender), "WVDM was not consumed");
    require(fake.midiSetCalls == 1 &&
            fake.lastMidiKind == CLAP_WRAPPER_STANDALONE_MIDI_OUTPUT &&
            fake.lastMidiId == 3 && fake.lastMidiEnabled,
            "WVDM did not reach host extension");

    const unsigned char refreshCommand[4]{'W', 'V', 'D', 'R'};
    require(bridge.receive(refreshCommand, sizeof(refreshCommand), sender), "WVDR was not consumed");
    require(fake.refreshCalls == 1, "WVDR did not reach host extension");

    std::puts("standalone-device-bridge native init-script contract passed");
    return 0;
}
