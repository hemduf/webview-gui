#include "polysynth_wclap_proxy.h"

#include <clap/ext/state.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace {

using namespace webview_gui::examples::polysynth;
using namespace webview_gui::examples::polysynth::wclap;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, nullptr};
const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    "com.webview-gui.test.polysynth-wclap-state",
    "PolySynth WCLAP state passthrough contract",
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
    clap_plugin_state_t state{};
    bool initialized = false;
    bool destroyed = false;
    std::uint8_t storedValue = 0x5au;
    std::uint32_t saveCalls = 0u;
    std::uint32_t loadCalls = 0u;

    FakeInner() noexcept {
        params.count = paramsCount;
        params.get_info = paramsGetInfo;
        params.get_value = paramsGetValue;
        params.value_to_text = paramsValueToText;
        params.text_to_value = paramsTextToValue;
        params.flush = paramsFlush;
        state.save = stateSave;
        state.load = stateLoad;

        plugin.desc = &kDescriptor;
        plugin.plugin_data = this;
        plugin.init = init;
        plugin.destroy = destroy;
        plugin.activate = nullptr;
        plugin.deactivate = nullptr;
        plugin.start_processing = nullptr;
        plugin.stop_processing = nullptr;
        plugin.reset = nullptr;
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

    static const void *CLAP_ABI getExtension(const clap_plugin_t *plugin, const char *id) {
        auto *self = from(plugin);
        if (!self || !self->initialized || !id)
            return nullptr;
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &self->params;
        if (std::strcmp(id, CLAP_EXT_STATE) == 0)
            return &self->state;
        return nullptr;
    }

    static clap_process_status CLAP_ABI process(const clap_plugin_t *, const clap_process_t *) {
        return CLAP_PROCESS_CONTINUE;
    }

    static std::uint32_t CLAP_ABI paramsCount(const clap_plugin_t *) { return 0u; }
    static bool CLAP_ABI paramsGetInfo(const clap_plugin_t *, std::uint32_t, clap_param_info_t *) {
        return false;
    }
    static bool CLAP_ABI paramsGetValue(const clap_plugin_t *, clap_id, double *) { return false; }
    static bool CLAP_ABI paramsValueToText(const clap_plugin_t *, clap_id, double, char *, std::uint32_t) {
        return false;
    }
    static bool CLAP_ABI paramsTextToValue(const clap_plugin_t *, clap_id, const char *, double *) {
        return false;
    }
    static void CLAP_ABI paramsFlush(const clap_plugin_t *,
                                     const clap_input_events_t *,
                                     const clap_output_events_t *) {}

    static bool CLAP_ABI stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
        auto *self = from(plugin);
        if (!self || !stream || !stream->write)
            return false;
        ++self->saveCalls;
        return stream->write(stream, &self->storedValue, 1u) == 1;
    }

    static bool CLAP_ABI stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
        auto *self = from(plugin);
        if (!self || !stream || !stream->read)
            return false;
        std::uint8_t value = 0u;
        if (stream->read(stream, &value, 1u) != 1)
            return false;
        self->storedValue = value;
        ++self->loadCalls;
        return true;
    }
};

struct HostState {
    clap_host_t host{};

    HostState() noexcept {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "state-contract";
        host.vendor = "webview-gui";
        host.url = "";
        host.version = "0";
        host.get_extension = getExtension;
        host.request_restart = requestRestart;
        host.request_process = requestProcess;
        host.request_callback = requestCallback;
    }

    static const void *CLAP_ABI getExtension(const clap_host_t *, const char *) { return nullptr; }
    static void CLAP_ABI requestRestart(const clap_host_t *) {}
    static void CLAP_ABI requestProcess(const clap_host_t *) {}
    static void CLAP_ABI requestCallback(const clap_host_t *) {}
};

struct ByteOutput {
    clap_ostream_t stream{};
    std::uint8_t value = 0u;
    std::uint32_t writes = 0u;

    ByteOutput() noexcept {
        stream.ctx = this;
        stream.write = write;
    }

    static std::int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                       const void *buffer,
                                       std::uint64_t size) {
        if (!stream || !stream->ctx || !buffer || size != 1u)
            return -1;
        auto &self = *static_cast<ByteOutput *>(stream->ctx);
        self.value = *static_cast<const std::uint8_t *>(buffer);
        ++self.writes;
        return 1;
    }
};

struct ByteInput {
    clap_istream_t stream{};
    std::uint8_t value = 0u;
    bool consumed = false;

    explicit ByteInput(std::uint8_t newValue) noexcept : value(newValue) {
        stream.ctx = this;
        stream.read = read;
    }

    static std::int64_t CLAP_ABI read(const clap_istream_t *stream,
                                      void *buffer,
                                      std::uint64_t size) {
        if (!stream || !stream->ctx || !buffer || size != 1u)
            return -1;
        auto &self = *static_cast<ByteInput *>(stream->ctx);
        if (self.consumed)
            return 0;
        *static_cast<std::uint8_t *>(buffer) = self.value;
        self.consumed = true;
        return 1;
    }
};

} // namespace

int main() {
    FakeInner inner;
    HostState host;

    const auto *plugin = wrapPolySynthWclapPlugin(&inner.plugin, &host.host);
    if (!plugin)
        return 1;
    if (plugin->plugin_data != inner.plugin.plugin_data || plugin->desc != inner.plugin.desc)
        return 2;

    if (plugin->get_extension(plugin, CLAP_EXT_STATE) != nullptr)
        return 3;
    const auto *preInitWebview = static_cast<const ::webview_gui::clap_plugin_webview *>(
        plugin->get_extension(plugin, ::webview_gui::CLAP_EXT_WEBVIEW));
    if (!preInitWebview || preInitWebview->get_uri(plugin, nullptr, 0u) != -1)
        return 4;

    if (!plugin->init(plugin))
        return 5;

    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!state || state != &inner.state || !state->save || !state->load)
        return 6;

    ByteOutput output;
    if (!state->save(plugin, &output.stream) || output.writes != 1u || output.value != 0x5au ||
        inner.saveCalls != 1u)
        return 7;

    ByteInput input{0xa5u};
    if (!state->load(plugin, &input.stream) || inner.loadCalls != 1u ||
        inner.storedValue != 0xa5u)
        return 8;

    plugin->destroy(plugin);
    if (!inner.destroyed)
        return 9;

    return 0;
}
