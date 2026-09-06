#include "../../examples/polysynth/wclap/polysynth_wclap_proxy.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

using namespace webview_gui::examples::polysynth;
using namespace webview_gui::examples::polysynth::wclap::detail;

struct InputList {
    clap_input_events_t api{};
    std::array<const clap_event_header_t *, 16> events{};
    std::array<clap_event_note_t, 4> notes{};
    std::array<clap_event_param_mod_t, 12> mods{};
    std::uint32_t eventCount = 0u;
    std::uint32_t noteCount = 0u;
    std::uint32_t modCount = 0u;

    InputList() noexcept {
        api.ctx = this;
        api.size = size;
        api.get = get;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *list) noexcept {
        return static_cast<const InputList *>(list->ctx)->eventCount;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *list,
                                                    std::uint32_t index) noexcept {
        const auto *self = static_cast<const InputList *>(list->ctx);
        return index < self->eventCount ? self->events[index] : nullptr;
    }

    void addNoteOn(std::uint32_t time,
                   std::int32_t noteId,
                   std::int16_t port,
                   std::int16_t channel,
                   std::int16_t key) noexcept {
        auto &event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = noteId;
        event.port_index = port;
        event.channel = channel;
        event.key = key;
        event.velocity = 1.0;
        events[eventCount++] = &event.header;
    }

    void addMod(std::uint32_t time,
                clap_id paramId,
                double amount,
                std::int32_t noteId = -1,
                std::int16_t port = -1,
                std::int16_t channel = -1,
                std::int16_t key = -1) noexcept {
        auto &event = mods[modCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_MOD;
        event.param_id = paramId;
        event.note_id = noteId;
        event.port_index = port;
        event.channel = channel;
        event.key = key;
        event.amount = amount;
        events[eventCount++] = &event.header;
    }
};

struct OutputSink {
    clap_output_events_t api{};
    std::uint32_t count = 0u;

    OutputSink() noexcept {
        api.ctx = this;
        api.try_push = push;
    }

    static bool CLAP_ABI push(const clap_output_events_t *list,
                              const clap_event_header_t *event) noexcept {
        if (!list || !list->ctx || !event)
            return false;
        ++static_cast<OutputSink *>(list->ctx)->count;
        return true;
    }
};

std::uint32_t gProcessModCount = 0u;
std::uint32_t gFlushModCount = 0u;
clap_event_param_mod_t gLastFlushMod{};
bool gEmitNoteEnd = false;

void observeInnerEvents(const clap_input_events_t *events, std::uint32_t &modCount) noexcept {
    if (!events || !events->size || !events->get)
        return;
    const auto count = events->size(events);
    for (std::uint32_t index = 0u; index < count; ++index) {
        const auto *header = events->get(events, index);
        if (header && header->space_id == CLAP_CORE_EVENT_SPACE_ID &&
            header->type == CLAP_EVENT_PARAM_MOD &&
            header->size >= sizeof(clap_event_param_mod_t)) {
            ++modCount;
            gLastFlushMod = *reinterpret_cast<const clap_event_param_mod_t *>(header);
        }
    }
}

clap_process_status CLAP_ABI innerProcess(const clap_plugin_t *,
                                          const clap_process_t *process) noexcept {
    if (!process)
        return CLAP_PROCESS_ERROR;
    observeInnerEvents(process->in_events, gProcessModCount);
    if (gEmitNoteEnd && process->out_events && process->out_events->try_push) {
        clap_event_note_t noteEnd{};
        noteEnd.header.size = sizeof(noteEnd);
        noteEnd.header.time = 7u;
        noteEnd.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        noteEnd.header.type = CLAP_EVENT_NOTE_END;
        noteEnd.note_id = 42;
        noteEnd.port_index = 0;
        noteEnd.channel = 1;
        noteEnd.key = 60;
        if (!process->out_events->try_push(process->out_events, &noteEnd.header))
            return CLAP_PROCESS_ERROR;
    }
    return CLAP_PROCESS_CONTINUE;
}

std::uint32_t CLAP_ABI paramsCount(const clap_plugin_t *) noexcept { return 0u; }
bool CLAP_ABI paramsGetInfo(const clap_plugin_t *, std::uint32_t, clap_param_info_t *) noexcept { return false; }
bool CLAP_ABI paramsGetValue(const clap_plugin_t *, clap_id, double *) noexcept { return false; }
bool CLAP_ABI paramsValueToText(const clap_plugin_t *, clap_id, double, char *, std::uint32_t) noexcept { return false; }
bool CLAP_ABI paramsTextToValue(const clap_plugin_t *, clap_id, const char *, double *) noexcept { return false; }
void CLAP_ABI innerFlush(const clap_plugin_t *,
                         const clap_input_events_t *in,
                         const clap_output_events_t *) noexcept {
    observeInnerEvents(in, gFlushModCount);
}

const clap_plugin_params_t kInnerParams{
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    innerFlush,
};

bool sameAddress(const ModulationTelemetryRecord &record,
                 std::int32_t noteId,
                 std::int32_t port,
                 std::int32_t channel,
                 std::int32_t key) noexcept {
    return record.noteId == noteId && record.portIndex == port &&
           record.channel == channel && record.key == key;
}

} // namespace

int main() {
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;

    clap_plugin_t inner{};
    inner.process = innerProcess;

    ProxyState state(&host);
    PolySynthWclapPluginProxy proxy(&inner, &state);
    proxy.initialized = true;
    proxy.active = true;
    proxy.innerParams = &kInnerParams;
    state.uiQueue.setActive(true);
    state.resetTelemetry();

    OutputSink sink;
    InputList processInput;
    processInput.addNoteOn(0u, 42, 0, 1, 60);
    processInput.addNoteOn(0u, 43, 0, 2, 64);
    processInput.addMod(2u, 1003u, 0.10, 42, 0, 1, 60); // Fine Tune
    processInput.addMod(3u, 1004u, 0.25, 42, 0, 1, 60); // Cutoff
    processInput.addMod(4u, 1011u, -0.50, 43, 0, 2, 64); // Pan
    processInput.addMod(5u, 1001u, 0.75, 42, 0, 1, 60); // Waveform: not modulatable
    processInput.addMod(6u, 1000u, 0.20, 42, 0, 1, 60); // Master: global-only, targeted invalid
    processInput.addMod(7u, 1004u, 0.30, 42, 0, 16, 60); // invalid MIDI channel
    processInput.addMod(8u, 1004u, 0.35, 42, 0, 1, 128); // invalid MIDI key
    processInput.addMod(9u, 1000u, 0.15); // valid global Master modulation

    clap_process_t process{};
    process.frames_count = 16u;
    process.in_events = &processInput.api;
    process.out_events = &sink.api;

    if (PolySynthWclapPluginProxy::proxyProcess(&proxy.plugin, &process) != CLAP_PROCESS_CONTINUE) {
        std::cerr << "proxy process rejected valid modulation input\n";
        return 1;
    }
    if (gProcessModCount != 8u) {
        std::cerr << "proxy changed the raw host event stream delivered to the inner plug-in\n";
        return 2;
    }

    std::array<ModulationTelemetryRecord, ModulationTelemetryQueue::kMaximumPending> records{};
    auto count = state.modulationTelemetry.copyPending(records);
    if (count != 6u) { // 2 note-ons + 3 poly mods + 1 valid global mod
        std::cerr << "telemetry did not filter/retain the expected process events: " << count << '\n';
        return 3;
    }
    if (records[0].kind != ModulationTelemetryKind::NoteOn ||
        !sameAddress(records[0], 42, 0, 1, 60) ||
        records[1].kind != ModulationTelemetryKind::NoteOn ||
        !sameAddress(records[1], 43, 0, 2, 64))
        return 4;
    if (records[2].paramId != 1003u || records[2].sampleOffset != 2u ||
        !sameAddress(records[2], 42, 0, 1, 60) ||
        records[3].paramId != 1004u || records[3].sampleOffset != 3u ||
        !sameAddress(records[3], 42, 0, 1, 60) ||
        records[4].paramId != 1011u || records[4].sampleOffset != 4u ||
        !sameAddress(records[4], 43, 0, 2, 64) ||
        records[5].paramId != 1000u || !sameAddress(records[5], -1, -1, -1, -1)) {
        std::cerr << "process telemetry lost parameter/address/sample metadata\n";
        return 5;
    }
    if (state.lastModParamId.load(std::memory_order_acquire) != 1000u ||
        std::fabs(floatFromBits(state.lastModAmountBits.load(std::memory_order_acquire)) - 0.15f) > 1.0e-6f ||
        state.activeVoices.load(std::memory_order_acquire) != 2u) {
        std::cerr << "legacy historical telemetry did not remain coherent\n";
        return 6;
    }
    state.modulationTelemetry.consume(count);

    // params.flush() is the second legal host modulation delivery route. Its
    // unavailable sample timestamp is normalized to zero while full addressing
    // and the DSP/base-state separation remain owned by the inner plug-in.
    InputList flushInput;
    flushInput.addMod(99u, 1004u, 0.40, 43, 0, 2, 64);
    PolySynthWclapPluginProxy::paramsFlush(&proxy.plugin, &flushInput.api, &sink.api);
    if (gFlushModCount != 1u || gLastFlushMod.param_id != 1004u ||
        gLastFlushMod.note_id != 43 || gLastFlushMod.port_index != 0 ||
        gLastFlushMod.channel != 2 || gLastFlushMod.key != 64) {
        std::cerr << "params.flush modulation did not reach the inner DSP unchanged\n";
        return 7;
    }
    count = state.modulationTelemetry.copyPending(records);
    if (count != 1u || records[0].kind != ModulationTelemetryKind::Modulation ||
        records[0].paramId != 1004u || records[0].sampleOffset != 0u ||
        !sameAddress(records[0], 43, 0, 2, 64) ||
        std::fabs(records[0].amount - 0.40f) > 1.0e-6f) {
        std::cerr << "params.flush modulation was not published with full telemetry address\n";
        return 8;
    }
    state.modulationTelemetry.consume(count);

    // A NOTE_END emitted by the DSP must invalidate the current voice generation
    // even if it is merely telemetry/history from the WebView's perspective.
    gEmitNoteEnd = true;
    clap_process_t endProcess{};
    endProcess.frames_count = 16u;
    endProcess.out_events = &sink.api;
    if (PolySynthWclapPluginProxy::proxyProcess(&proxy.plugin, &endProcess) != CLAP_PROCESS_CONTINUE)
        return 9;
    count = state.modulationTelemetry.copyPending(records);
    if (count != 1u || records[0].kind != ModulationTelemetryKind::NoteEnd ||
        records[0].sampleOffset != 7u || !sameAddress(records[0], 42, 0, 1, 60) ||
        state.activeVoices.load(std::memory_order_acquire) != 1u) {
        std::cerr << "NOTE_END lifecycle telemetry did not clear one active generation\n";
        return 10;
    }

    return 0;
}
