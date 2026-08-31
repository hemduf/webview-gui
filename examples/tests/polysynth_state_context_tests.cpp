#include "polysynth_plugin.h"
#include "polysynth_parameters.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state-context.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

using webview_gui::examples::polysynth::ParameterSlot;

constexpr clap_id kFineTuneId =
    webview_gui::examples::polysynth::kFirstParameterId +
    static_cast<clap_id>(ParameterSlot::FineTuning);

constexpr std::array<std::uint32_t, 3> kContexts{{
    CLAP_STATE_CONTEXT_FOR_PRESET,
    CLAP_STATE_CONTEXT_FOR_DUPLICATE,
    CLAP_STATE_CONTEXT_FOR_PROJECT,
}};

const void *CLAP_ABI hostGetExtension(const clap_host_t *, const char *) { return nullptr; }
void CLAP_ABI hostRequestRestart(const clap_host_t *) {}
void CLAP_ABI hostRequestProcess(const clap_host_t *) {}
void CLAP_ABI hostRequestCallback(const clap_host_t *) {}

const clap_host_t kHost{
    CLAP_VERSION,
    nullptr,
    "webview-gui PolySynth state-context tests",
    "webview-gui",
    "https://github.com/hemduf/webview-gui",
    "0.1.0",
    hostGetExtension,
    hostRequestRestart,
    hostRequestProcess,
    hostRequestCallback,
};

struct FlushInputEvents {
    explicit FlushInputEvents(double value) noexcept {
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = kFineTuneId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        return events && events->ctx ? 1u : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx || index != 0)
            return nullptr;
        return &static_cast<const FlushInputEvents *>(events->ctx)->event.header;
    }

    clap_event_param_value_t event{};
    clap_input_events_t input{};
};

struct MemoryOutputStream {
    MemoryOutputStream() noexcept {
        stream.ctx = this;
        stream.write = write;
    }

    static std::int64_t CLAP_ABI write(const clap_ostream_t *stream,
                                       const void *source,
                                       std::uint64_t size) noexcept {
        if (!stream || !stream->ctx || (!source && size != 0))
            return -1;
        auto &self = *static_cast<MemoryOutputStream *>(stream->ctx);
        const auto remaining = self.bytes.size() - self.used;
        if (size > remaining)
            return -1;
        if (size != 0)
            std::memcpy(self.bytes.data() + self.used, source, static_cast<std::size_t>(size));
        self.used += static_cast<std::size_t>(size);
        return static_cast<std::int64_t>(size);
    }

    std::array<std::uint8_t, 128> bytes{};
    std::size_t used = 0;
    clap_ostream_t stream{};
};

struct MemoryInputStream {
    MemoryInputStream(const std::uint8_t *source, std::size_t size) noexcept
        : bytes(source), sizeBytes(size) {
        stream.ctx = this;
        stream.read = read;
    }

    static std::int64_t CLAP_ABI read(const clap_istream_t *stream,
                                      void *destination,
                                      std::uint64_t size) noexcept {
        if (!stream || !stream->ctx || (!destination && size != 0))
            return -1;
        auto &self = *static_cast<MemoryInputStream *>(stream->ctx);
        if (self.offset >= self.sizeBytes)
            return 0;
        const auto count = std::min<std::uint64_t>(
            size, static_cast<std::uint64_t>(self.sizeBytes - self.offset));
        if (count != 0)
            std::memcpy(destination, self.bytes + self.offset, static_cast<std::size_t>(count));
        self.offset += static_cast<std::size_t>(count);
        return static_cast<std::int64_t>(count);
    }

    const std::uint8_t *bytes = nullptr;
    std::size_t sizeBytes = 0;
    std::size_t offset = 0;
    clap_istream_t stream{};
};

bool setFineTune(const clap_plugin_t *plugin,
                 const clap_plugin_params_t *params,
                 double value) noexcept {
    FlushInputEvents input(value);
    params->flush(plugin, &input.input, nullptr);
    double observed = 0.0;
    return params->get_value(plugin, kFineTuneId, &observed) &&
           std::fabs(observed - value) <= 1.0e-6;
}

bool hasFineTune(const clap_plugin_t *plugin,
                 const clap_plugin_params_t *params,
                 double expected) noexcept {
    double value = 0.0;
    return params->get_value(plugin, kFineTuneId, &value) &&
           std::fabs(value - expected) <= 1.0e-6;
}

bool sameBytes(const MemoryOutputStream &left, const MemoryOutputStream &right) noexcept {
    return left.used == right.used &&
           std::equal(left.bytes.begin(), left.bytes.begin() + left.used, right.bytes.begin());
}

int fail(const char *message, int code) {
    std::cerr << message << '\n';
    return code;
}

} // namespace

int main() {
    using namespace webview_gui::examples::polysynth;

    const auto *factory = polysynthFactory();
    if (!factory)
        return fail("missing PolySynth factory", 1);

    const auto *plugin = factory->create_plugin(factory, &kHost, kPolySynthPluginId);
    if (!plugin)
        return fail("could not create PolySynth", 2);
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return fail("could not initialize PolySynth", 3);
    }

    const auto *params = static_cast<const clap_plugin_params_t *>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto *state = static_cast<const clap_plugin_state_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const auto *stateContext = static_cast<const clap_plugin_state_context_t *>(
        plugin->get_extension(plugin, CLAP_EXT_STATE_CONTEXT));
    if (!params || !params->get_value || !params->flush ||
        !state || !state->save || !state->load ||
        !stateContext || !stateContext->save || !stateContext->load) {
        plugin->destroy(plugin);
        return fail("PolySynth does not expose complete state + state-context interfaces", 4);
    }

    if (!setFineTune(plugin, params, 37.5)) {
        plugin->destroy(plugin);
        return fail("could not establish Fine Tune state", 5);
    }

    MemoryOutputStream regularSaved;
    if (!state->save(plugin, &regularSaved.stream) || regularSaved.used == 0) {
        plugin->destroy(plugin);
        return fail("regular state save failed", 6);
    }

    for (const auto saveContext : kContexts) {
        MemoryOutputStream contextualSaved;
        if (!stateContext->save(plugin, &contextualSaved.stream, saveContext) ||
            !sameBytes(regularSaved, contextualSaved)) {
            plugin->destroy(plugin);
            return fail("state-context save is not equivalent to regular state", 7);
        }

        // A context save must remain loadable through the plain state extension.
        if (!setFineTune(plugin, params, -25.0)) {
            plugin->destroy(plugin);
            return fail("could not mutate Fine Tune before regular cross-load", 8);
        }
        MemoryInputStream regularCrossLoad(contextualSaved.bytes.data(), contextualSaved.used);
        if (!state->load(plugin, &regularCrossLoad.stream) ||
            !hasFineTune(plugin, params, 37.5)) {
            plugin->destroy(plugin);
            return fail("regular state could not load state-context payload", 9);
        }

        // A context payload must be loadable under every other defined context.
        for (const auto loadContext : kContexts) {
            if (!setFineTune(plugin, params, -12.5)) {
                plugin->destroy(plugin);
                return fail("could not mutate Fine Tune before context cross-load", 10);
            }
            MemoryInputStream contextualCrossLoad(
                contextualSaved.bytes.data(), contextualSaved.used);
            if (!stateContext->load(plugin, &contextualCrossLoad.stream, loadContext) ||
                !hasFineTune(plugin, params, 37.5)) {
                plugin->destroy(plugin);
                return fail("state-context payload was not cross-context compatible", 11);
            }
        }
    }

    // Plain state payloads must also be accepted by every state-context load path.
    for (const auto loadContext : kContexts) {
        if (!setFineTune(plugin, params, -50.0)) {
            plugin->destroy(plugin);
            return fail("could not mutate Fine Tune before plain-to-context load", 12);
        }
        MemoryInputStream plainToContext(regularSaved.bytes.data(), regularSaved.used);
        if (!stateContext->load(plugin, &plainToContext.stream, loadContext) ||
            !hasFineTune(plugin, params, 37.5)) {
            plugin->destroy(plugin);
            return fail("state-context could not load regular state payload", 13);
        }
    }

    for (const auto context : kContexts) {
        if (stateContext->save(plugin, nullptr, context) ||
            stateContext->load(plugin, nullptr, context)) {
            plugin->destroy(plugin);
            return fail("state-context accepted a null stream", 14);
        }
    }

    plugin->destroy(plugin);
    return 0;
}
