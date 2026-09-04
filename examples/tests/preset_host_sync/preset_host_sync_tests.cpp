#include "gain_plugin.h"
#include "polysynth_plugin.h"
#include "polysynth_parameters.h"
#include "polysynth_wclap_proxy.h"

#include <webview-gui/clap-webview-gui.h>

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/preset-load.h>
#include <clap/factory/preset-discovery.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace gain = webview_gui::examples::gain;
namespace poly = webview_gui::examples::polysynth;
namespace poly_wclap = webview_gui::examples::polysynth::wclap;

struct HostState {
    clap_host_t host{};
    clap_host_params_t params{};
    clap_host_preset_load_t presetLoad{};
    webview_gui::clap_host_webview webview{};

    bool exposeParams = true;
    bool exposePresetLoad = true;
    bool exposeWebview = false;
    std::uint32_t rescanCount = 0u;
    clap_param_rescan_flags rescanFlags = 0u;
    std::uint32_t loadedCount = 0u;
    std::uint32_t errorCount = 0u;
    std::uint32_t flushRequests = 0u;
    std::uint32_t processRequests = 0u;
    std::uint32_t gainBaseMessages = 0u;
    std::uint32_t polyBaseMessages = 0u;
    std::uint32_t telemetryMessages = 0u;
    std::vector<std::string> order;

    HostState() noexcept {
        params.rescan = paramsRescan;
        params.clear = paramsClear;
        params.request_flush = paramsRequestFlush;
        presetLoad.on_error = presetOnError;
        presetLoad.loaded = presetLoaded;
        webview.send = webviewSend;

        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "webview-gui preset host sync contract";
        host.vendor = "webview-gui";
        host.url = "https://github.com/hemduf/webview-gui";
        host.version = "0.1.0";
        host.get_extension = getExtension;
        host.request_restart = requestRestart;
        host.request_process = requestProcess;
        host.request_callback = requestCallback;
    }

    void resetNotifications() {
        rescanCount = 0u;
        rescanFlags = 0u;
        loadedCount = 0u;
        errorCount = 0u;
        flushRequests = 0u;
        processRequests = 0u;
        gainBaseMessages = 0u;
        polyBaseMessages = 0u;
        telemetryMessages = 0u;
        order.clear();
    }

    static HostState *from(const clap_host_t *host) noexcept {
        return host ? static_cast<HostState *>(host->host_data) : nullptr;
    }

    static const void *CLAP_ABI getExtension(const clap_host_t *host, const char *id) {
        auto *self = from(host);
        if (!self || !id)
            return nullptr;
        if (self->exposeParams && std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &self->params;
        if (self->exposePresetLoad &&
            (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0 ||
             std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0))
            return &self->presetLoad;
        if (self->exposeWebview && std::strcmp(id, webview_gui::CLAP_EXT_WEBVIEW) == 0)
            return &self->webview;
        return nullptr;
    }

    static void CLAP_ABI requestRestart(const clap_host_t *) {}
    static void CLAP_ABI requestCallback(const clap_host_t *) {}

    static void CLAP_ABI requestProcess(const clap_host_t *host) {
        if (auto *self = from(host))
            ++self->processRequests;
    }

    static void CLAP_ABI paramsRescan(const clap_host_t *host,
                                      clap_param_rescan_flags flags) {
        if (auto *self = from(host)) {
            ++self->rescanCount;
            self->rescanFlags |= flags;
            self->order.emplace_back("rescan");
        }
    }

    static void CLAP_ABI paramsClear(const clap_host_t *, clap_id, clap_param_clear_flags) {}

    static void CLAP_ABI paramsRequestFlush(const clap_host_t *host) {
        if (auto *self = from(host))
            ++self->flushRequests;
    }

    static void CLAP_ABI presetOnError(const clap_host_t *host,
                                       std::uint32_t,
                                       const char *,
                                       const char *,
                                       std::int32_t,
                                       const char *) {
        if (auto *self = from(host)) {
            ++self->errorCount;
            self->order.emplace_back("error");
        }
    }

    static void CLAP_ABI presetLoaded(const clap_host_t *host,
                                      std::uint32_t,
                                      const char *,
                                      const char *) {
        if (auto *self = from(host)) {
            ++self->loadedCount;
            self->order.emplace_back("loaded");
        }
    }

    static bool CLAP_ABI webviewSend(const clap_host_t *host,
                                     const void *buffer,
                                     std::uint32_t size) {
        auto *self = from(host);
        if (!self || (!buffer && size != 0u))
            return false;
        const auto *bytes = static_cast<const std::uint8_t *>(buffer);
        if (size == 16u && bytes && bytes[0] == 'W' && bytes[1] == 'V') {
            if (bytes[2] == 'U' && bytes[3] == '1')
                ++self->gainBaseMessages;
            if (bytes[2] == 'B' && bytes[3] == '1')
                ++self->polyBaseMessages;
        }
        if (size == 32u && bytes && bytes[0] == 'W' && bytes[1] == 'V' &&
            bytes[2] == 'T' && bytes[3] == '1')
            ++self->telemetryMessages;
        self->order.emplace_back("ui");
        return true;
    }
};

const clap_plugin_preset_load_t *presetLoadExtension(const clap_plugin_t *plugin) {
    if (!plugin || !plugin->get_extension)
        return nullptr;
    auto *extension = static_cast<const clap_plugin_preset_load_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD));
    if (!extension)
        extension = static_cast<const clap_plugin_preset_load_t *>(
            plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD_COMPAT));
    return extension;
}

bool near(double actual, double expected, double epsilon = 1.0e-5) noexcept {
    return std::fabs(actual - expected) <= epsilon;
}

bool readValue(const clap_plugin_t *plugin,
               const clap_plugin_params_t *params,
               clap_id id,
               double expected) {
    double value = 0.0;
    return params && params->get_value && params->get_value(plugin, id, &value) &&
           near(value, expected);
}

constexpr clap_id polyId(poly::ParameterSlot slot) noexcept {
    return poly::kFirstParameterId + static_cast<clap_id>(slot);
}

bool hasPolyPadGeneration(const clap_plugin_t *plugin,
                          const clap_plugin_params_t *params) {
    return readValue(plugin, params, polyId(poly::ParameterSlot::MasterGain), -6.0) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::Waveform), 1.0) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::CoarseTuning), 0.0) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::FineTuning), 0.0) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::FilterCutoff), 3500.0) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::FilterResonance), 0.15) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::AmpAttack), 1.5) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::AmpDecay), 1.0) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::AmpSustain), 0.8) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::AmpRelease), 2.5) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::FilterEnvelopeAmount), 0.2) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::Pan), 0.0) &&
           readValue(plugin, params, polyId(poly::ParameterSlot::AmpLevel), 0.85);
}

struct EmptyInput {
    clap_input_events_t input{};
    EmptyInput() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }
    static std::uint32_t CLAP_ABI size(const clap_input_events_t *) noexcept { return 0u; }
    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *,
                                                   std::uint32_t) noexcept {
        return nullptr;
    }
};

struct OutputCounter {
    clap_output_events_t output{};
    std::uint32_t count = 0u;
    std::uint32_t automationCount = 0u;

    OutputCounter() noexcept {
        output.ctx = this;
        output.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t *events,
                                 const clap_event_header_t *header) noexcept {
        if (!events || !events->ctx || !header)
            return false;
        auto &self = *static_cast<OutputCounter *>(events->ctx);
        ++self.count;
        if (header->type == CLAP_EVENT_PARAM_VALUE ||
            header->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
            header->type == CLAP_EVENT_PARAM_GESTURE_END)
            ++self.automationCount;
        return true;
    }
};

bool verifyGainPresetHostAndGuiSync() {
    HostState host;
    host.exposeWebview = true;
    const auto *factory = gain::gainFactory();
    const auto *plugin = factory ? factory->create_plugin(factory, &host.host, gain::kGainPluginId) : nullptr;
    if (!plugin || !plugin->init(plugin)) {
        if (plugin)
            plugin->destroy(plugin);
        return false;
    }

    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *preset = presetLoadExtension(plugin);
    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto *webview = static_cast<const webview_gui::clap_plugin_webview *>(
        plugin->get_extension(plugin, webview_gui::CLAP_EXT_WEBVIEW));
    if (!params || !preset || !preset->from_location || !gui || !webview || !webview->receive ||
        !gui->is_api_supported(plugin, webview_gui::CLAP_WINDOW_API_WEBVIEW, false) ||
        !gui->create(plugin, webview_gui::CLAP_WINDOW_API_WEBVIEW, false)) {
        plugin->destroy(plugin);
        return false;
    }

    host.resetNotifications();
    const bool loaded = preset->from_location(plugin,
                                              CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                              nullptr,
                                              "gain:trim-minus-6db");
    const std::array<std::uint8_t, 4> sync{{'W', 'V', 'Q', '1'}};
    const bool guiSynced = loaded && webview->receive(plugin, sync.data(), sync.size());
    const bool ok = guiSynced &&
                    readValue(plugin, params, gain::kGainParamId, -6.0) &&
                    readValue(plugin, params, gain::kBypassParamId, 0.0) &&
                    host.rescanCount == 1u &&
                    (host.rescanFlags & CLAP_PARAM_RESCAN_VALUES) != 0u &&
                    host.loadedCount == 1u && host.errorCount == 0u &&
                    host.order.size() >= 4u && host.order[0] == "rescan" &&
                    host.order[1] == "loaded" &&
                    host.gainBaseMessages == 2u &&
                    host.flushRequests == 0u && host.processRequests == 0u;

    gui->destroy(plugin);
    plugin->destroy(plugin);
    return ok;
}

bool verifyGainMissingHostParamsAndFailure() {
    HostState host;
    host.exposeParams = false;
    const auto *factory = gain::gainFactory();
    const auto *plugin = factory ? factory->create_plugin(factory, &host.host, gain::kGainPluginId) : nullptr;
    if (!plugin || !plugin->init(plugin)) {
        if (plugin)
            plugin->destroy(plugin);
        return false;
    }
    const auto *preset = presetLoadExtension(plugin);
    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!preset || !params) {
        plugin->destroy(plugin);
        return false;
    }

    host.resetNotifications();
    if (!preset->from_location(plugin,
                               CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                               nullptr,
                               "gain:boost-plus-6db") ||
        !readValue(plugin, params, gain::kGainParamId, 6.0) ||
        host.rescanCount != 0u || host.loadedCount != 1u || host.errorCount != 0u) {
        plugin->destroy(plugin);
        return false;
    }

    host.resetNotifications();
    const bool rejected = !preset->from_location(plugin,
                                                  CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                                  nullptr,
                                                  "gain:not-a-preset") &&
                          host.rescanCount == 0u && host.loadedCount == 0u &&
                          host.errorCount == 1u;
    plugin->destroy(plugin);
    return rejected;
}

bool verifyPolyPresetInactiveAndActiveSync() {
    HostState host;
    const auto *factory = poly::polysynthFactory();
    const auto *plugin = factory ? factory->create_plugin(factory, &host.host, poly::kPolySynthPluginId) : nullptr;
    if (!plugin || !plugin->init(plugin)) {
        if (plugin)
            plugin->destroy(plugin);
        return false;
    }

    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *preset = presetLoadExtension(plugin);
    if (!params || !preset || !preset->from_location) {
        plugin->destroy(plugin);
        return false;
    }

    host.resetNotifications();
    if (!preset->from_location(plugin,
                               CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                               nullptr,
                               "polysynth:pad") ||
        !hasPolyPadGeneration(plugin, params) ||
        host.rescanCount != 1u ||
        (host.rescanFlags & CLAP_PARAM_RESCAN_VALUES) == 0u ||
        host.loadedCount != 1u || host.errorCount != 0u ||
        host.order.size() < 2u || host.order[0] != "rescan" || host.order[1] != "loaded") {
        plugin->destroy(plugin);
        return false;
    }

    if (!plugin->activate(plugin, 48000.0, 1u, 64u) ||
        !plugin->start_processing(plugin)) {
        plugin->destroy(plugin);
        return false;
    }

    host.resetNotifications();
    if (!preset->from_location(plugin,
                               CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                               nullptr,
                               "polysynth:pad") ||
        !hasPolyPadGeneration(plugin, params) || host.rescanCount != 1u ||
        (host.rescanFlags & CLAP_PARAM_RESCAN_VALUES) == 0u ||
        host.loadedCount != 1u || host.order.size() < 2u ||
        host.order[0] != "rescan" || host.order[1] != "loaded") {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return false;
    }

    // Load a different generation while active so the process handoff is exercised.
    host.resetNotifications();
    if (!preset->from_location(plugin,
                               CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                               nullptr,
                               "polysynth:bass") ||
        host.rescanCount != 1u ||
        (host.rescanFlags & CLAP_PARAM_RESCAN_VALUES) == 0u ||
        host.loadedCount != 1u ||
        host.order.size() < 2u || host.order[0] != "rescan" || host.order[1] != "loaded") {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return false;
    }

    EmptyInput empty;
    OutputCounter output;
    std::array<float, 8> left{};
    std::array<float, 8> right{};
    float *channels[] = {left.data(), right.data()};
    clap_audio_buffer_t audio{};
    audio.data32 = channels;
    audio.channel_count = 2u;
    clap_process_t process{};
    process.frames_count = 8u;
    process.in_events = &empty.input;
    process.out_events = &output.output;
    process.audio_outputs = &audio;
    process.audio_outputs_count = 1u;

    const auto status = plugin->process(plugin, &process);
    const bool ok = status != CLAP_PROCESS_ERROR && output.automationCount == 0u &&
                    readValue(plugin, params, polyId(poly::ParameterSlot::MasterGain), -3.0) &&
                    readValue(plugin, params, polyId(poly::ParameterSlot::Waveform), 2.0) &&
                    readValue(plugin, params, polyId(poly::ParameterSlot::CoarseTuning), -12.0) &&
                    readValue(plugin, params, polyId(poly::ParameterSlot::FilterCutoff), 220.0) &&
                    readValue(plugin, params, polyId(poly::ParameterSlot::AmpLevel), 1.0);

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return ok;
}

bool verifyPolyPresetGuiSyncThroughProxy() {
    HostState host;
    host.exposeWebview = true;
    const auto *factory = poly::polysynthFactory();
    const auto *inner = factory ? factory->create_plugin(factory, &host.host, poly::kPolySynthPluginId) : nullptr;
    const auto *plugin = inner ? poly_wclap::wrapPolySynthWclapPlugin(inner, &host.host) : nullptr;
    if (!plugin || !plugin->init(plugin)) {
        if (plugin)
            plugin->destroy(plugin);
        else if (inner)
            inner->destroy(inner);
        return false;
    }

    const auto *gui = static_cast<const clap_plugin_gui_t *>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    const auto *webview = static_cast<const webview_gui::clap_plugin_webview *>(
        plugin->get_extension(plugin, webview_gui::CLAP_EXT_WEBVIEW));
    const auto *preset = presetLoadExtension(plugin);
    if (!gui || !webview || !webview->receive || !preset || !preset->from_location ||
        !gui->is_api_supported(plugin, webview_gui::CLAP_WINDOW_API_WEBVIEW, false) ||
        !gui->create(plugin, webview_gui::CLAP_WINDOW_API_WEBVIEW, false)) {
        plugin->destroy(plugin);
        return false;
    }

    host.resetNotifications();
    const bool loaded = preset->from_location(plugin,
                                              CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                              nullptr,
                                              "polysynth:pad");
    const std::array<std::uint8_t, 4> sync{{'W', 'V', 'S', '1'}};
    const bool guiSynced = loaded && webview->receive(plugin, sync.data(), sync.size());
    const bool ok = guiSynced && host.rescanCount == 1u && host.loadedCount == 1u &&
                    host.order.size() >= 16u && host.order[0] == "rescan" &&
                    host.order[1] == "loaded" &&
                    host.polyBaseMessages == poly::kParameterCount &&
                    host.telemetryMessages == 1u &&
                    host.flushRequests == 0u && host.processRequests == 0u;

    gui->destroy(plugin);
    plugin->destroy(plugin);
    return ok;
}

} // namespace

int main() {
    if (!verifyGainPresetHostAndGuiSync())
        return 1;
    if (!verifyGainMissingHostParamsAndFailure())
        return 2;
    if (!verifyPolyPresetInactiveAndActiveSync())
        return 3;
    if (!verifyPolyPresetGuiSyncThroughProxy())
        return 4;
    return 0;
}
