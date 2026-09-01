#pragma once

#include "../polysynth_plugin.h"
#include "../polysynth_parameters.h"
#include "webview-gui/clap-webview-gui.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

namespace webview_gui::examples::polysynth::wclap {
namespace detail {

inline constexpr char kEditorUri[] = "/index.html";
inline constexpr char kEditorMime[] = "text/html; charset=utf-8";
inline constexpr char kEditorScriptUri[] = "/polysynth.js";
inline constexpr char kEditorScriptMime[] = "text/javascript; charset=utf-8";
inline constexpr std::uint32_t kEditorWidth = 760u;
inline constexpr std::uint32_t kEditorHeight = 580u;

inline constexpr char kEditorHtml[] = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; img-src data:">
<title>webview-gui PolySynth</title>
<style>
:root { color-scheme: dark; font-family: system-ui, sans-serif; background:#151515; color:#f1f1f1; }
* { box-sizing:border-box; }
body { margin:0; min-height:100vh; background:#151515; }
main { padding:18px; display:grid; gap:16px; }
header { display:flex; gap:20px; align-items:center; justify-content:space-between; }
h1 { font-size:18px; margin:0; }
.telemetry { display:flex; gap:14px; align-items:center; font-size:12px; opacity:.9; }
.grid { display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:12px; }
section { border:1px solid #343434; border-radius:8px; padding:12px; background:#1c1c1c; }
h2 { margin:0 0 10px; font-size:12px; text-transform:uppercase; letter-spacing:.08em; opacity:.75; }
.control { display:grid; grid-template-columns:1fr auto; gap:5px 10px; margin:9px 0; align-items:center; }
.control input,.control select { grid-column:1 / -1; width:100%; }
.value { font-variant-numeric:tabular-nums; opacity:.8; }
.mod { min-width:4.5em; text-align:right; color:#9bd2ff; font-variant-numeric:tabular-nums; }
meter { width:90px; height:10px; }
small { opacity:.65; }
@media (max-width:680px) { .grid { grid-template-columns:1fr; } }
</style>
</head>
<body>
<main>
<header>
  <div><h1>PolySynth</h1><small>WCLAP / CLAP WebView reference editor</small></div>
  <div class="telemetry">
    <span>Voices <strong id="voices">0</strong>/16</span>
    <span>L <meter id="meter-l" min="0" max="1" value="0"></meter></span>
    <span>R <meter id="meter-r" min="0" max="1" value="0"></meter></span>
  </div>
</header>
<div class="grid">
<section><h2>Oscillator</h2>
  <label class="control">Waveform <span class="value" data-value="1001">Sine</span>
    <select data-param="1001"><option value="0">Sine</option><option value="1">Saw</option><option value="2">Square</option></select></label>
  <label class="control">Coarse <span class="value" data-value="1002">0</span>
    <input data-param="1002" type="range" min="-48" max="48" step="1" value="0"></label>
  <label class="control">Fine <span><span class="value" data-value="1003">0.0</span> <span class="mod" data-mod="1003"></span></span>
    <input data-param="1003" type="range" min="-100" max="100" step="0.1" value="0"></label>
</section>
<section><h2>Filter</h2>
  <label class="control">Cutoff <span><span class="value" data-value="1004">6000</span> <span class="mod" data-mod="1004"></span></span>
    <input data-param="1004" type="range" min="20" max="20000" step="1" value="6000"></label>
  <label class="control">Resonance <span><span class="value" data-value="1005">0.00</span> <span class="mod" data-mod="1005"></span></span>
    <input data-param="1005" type="range" min="0" max="0.99" step="0.01" value="0"></label>
  <label class="control">Env amount <span><span class="value" data-value="1010">0.00</span> <span class="mod" data-mod="1010"></span></span>
    <input data-param="1010" type="range" min="-1" max="1" step="0.01" value="0"></label>
</section>
<section><h2>Amp Envelope</h2>
  <label class="control">Attack <span class="value" data-value="1006">0.01</span>
    <input data-param="1006" type="range" min="0" max="10" step="0.01" value="0.01"></label>
  <label class="control">Decay <span class="value" data-value="1007">0.10</span>
    <input data-param="1007" type="range" min="0" max="10" step="0.01" value="0.1"></label>
  <label class="control">Sustain <span class="value" data-value="1008">0.80</span>
    <input data-param="1008" type="range" min="0" max="1" step="0.01" value="0.8"></label>
  <label class="control">Release <span class="value" data-value="1009">0.25</span>
    <input data-param="1009" type="range" min="0.001" max="10" step="0.01" value="0.25"></label>
</section>
<section><h2>Output</h2>
  <label class="control">Master gain <span class="value" data-value="1000">0.0</span>
    <input data-param="1000" type="range" min="-60" max="12" step="0.1" value="0"></label>
  <label class="control">Pan <span><span class="value" data-value="1011">0.00</span> <span class="mod" data-mod="1011"></span></span>
    <input data-param="1011" type="range" min="-1" max="1" step="0.01" value="0"></label>
  <label class="control">Amp level <span><span class="value" data-value="1012">1.00</span> <span class="mod" data-mod="1012"></span></span>
    <input data-param="1012" type="range" min="0" max="1" step="0.01" value="1"></label>
</section>
<section><h2>Voice Inspector</h2>
  <div class="control">Last modulation <span id="last-mod">—</span></div>
  <div class="control">Last expression <span id="last-expression">—</span></div>
  <small>Base parameter values stay separate from per-note modulation and note-expression telemetry.</small>
</section>
</div>
</main>
<script src="polysynth.js" defer></script>
</body>
</html>
)html";

inline constexpr char kEditorScript[] = R"js((() => {
"use strict";
const EDIT_SIZE = 24;
const openGestures = new Set();
const controls = new Map();
for (const element of document.querySelectorAll("[data-param]"))
  controls.set(Number(element.dataset.param), element);

const valueLabels = new Map();
for (const element of document.querySelectorAll("[data-value]"))
  valueLabels.set(Number(element.dataset.value), element);

const modLabels = new Map();
for (const element of document.querySelectorAll("[data-mod]"))
  modLabels.set(Number(element.dataset.mod), element);

const voices = document.getElementById("voices");
const meterL = document.getElementById("meter-l");
const meterR = document.getElementById("meter-r");
const lastMod = document.getElementById("last-mod");
const lastExpression = document.getElementById("last-expression");
const expressionNames = ["volume", "pan", "tuning", "vibrato", "expression", "brightness", "pressure"];

function edit(kind, paramId, value = 0) {
  const buffer = new ArrayBuffer(EDIT_SIZE);
  const bytes = new Uint8Array(buffer);
  bytes.set([0x57,0x56,0x50,0x31]); // WVP1
  bytes[4] = kind;
  const view = new DataView(buffer);
  view.setUint32(8, paramId, true);
  view.setFloat64(16, value, true);
  window.parent.postMessage(buffer, "*");
}

function begin(paramId) {
  if (openGestures.has(paramId)) return;
  openGestures.add(paramId);
  edit(1, paramId);
}
function end(paramId) {
  if (!openGestures.has(paramId)) return;
  openGestures.delete(paramId);
  edit(3, paramId);
}
function setValue(paramId, value) {
  if (!Number.isFinite(value)) return;
  begin(paramId);
  edit(2, paramId, value);
}
function requestSync() {
  const buffer = new Uint8Array([0x57,0x56,0x53,0x31]).buffer; // WVS1
  window.parent.postMessage(buffer, "*");
}
function displayValue(id, value) {
  const label = valueLabels.get(id);
  if (!label) return;
  if (id === 1001) label.textContent = ["Sine","Saw","Square"][Math.max(0,Math.min(2,Math.trunc(value)))] ?? "?";
  else if (id === 1002 || id === 1004) label.textContent = String(Math.round(value));
  else if (id === 1000 || id === 1003) label.textContent = value.toFixed(1);
  else label.textContent = value.toFixed(2);
}
function applyBase(id, value) {
  if (!Number.isFinite(value)) return;
  const control = controls.get(id);
  if (control && !openGestures.has(id)) control.value = String(value);
  displayValue(id, value);
}
function clearModsExcept(id) {
  for (const [paramId,label] of modLabels) if (paramId !== id) label.textContent = "";
}

window.addEventListener("message", event => {
  if (!(event.data instanceof ArrayBuffer)) return;
  const bytes = new Uint8Array(event.data);
  const view = new DataView(event.data);
  if (bytes.length === 16 && bytes[0]===0x57 && bytes[1]===0x56 && bytes[2]===0x42 && bytes[3]===0x31) {
    applyBase(view.getUint32(4,true), view.getFloat64(8,true));
    return;
  }
  if (bytes.length === 32 && bytes[0]===0x57 && bytes[1]===0x56 && bytes[2]===0x54 && bytes[3]===0x31) {
    voices.textContent = String(view.getUint32(4,true));
    meterL.value = String(Math.min(1,Math.max(0,view.getFloat32(8,true))));
    meterR.value = String(Math.min(1,Math.max(0,view.getFloat32(12,true))));
    const modId = view.getUint32(16,true);
    const modAmount = view.getFloat32(20,true);
    clearModsExcept(modId);
    const mod = modLabels.get(modId);
    if (mod && Number.isFinite(modAmount)) mod.textContent = `${modAmount >= 0 ? "+" : ""}${modAmount.toFixed(2)}`;
    lastMod.textContent = modId === 0xffffffff ? "—" : `${modId}: ${modAmount.toFixed(3)}`;
    const expressionId = view.getUint32(24,true);
    const expressionValue = view.getFloat32(28,true);
    lastExpression.textContent = expressionId === 0xffffffff ? "—" : `${expressionNames[expressionId] ?? expressionId}: ${expressionValue.toFixed(3)}`;
  }
});

for (const [id,control] of controls) {
  if (control.tagName === "SELECT") {
    control.addEventListener("change", () => {
      begin(id); setValue(id, Number(control.value)); displayValue(id, Number(control.value)); end(id);
    });
    continue;
  }
  control.addEventListener("pointerdown", () => begin(id));
  control.addEventListener("input", () => {
    const value = Number(control.value);
    setValue(id, value); displayValue(id, value);
  });
  control.addEventListener("change", () => end(id));
  control.addEventListener("pointerup", () => end(id));
  control.addEventListener("pointercancel", () => end(id));
  control.addEventListener("blur", () => end(id));
}

window.addEventListener("pagehide", () => {
  for (const id of [...openGestures]) end(id);
});
requestSync();
setInterval(requestSync, 50);
})());
)js";

inline bool copyExactText(char *destination, std::size_t capacity, const char *text) noexcept {
    if (!destination || !text)
        return false;
    const auto length = std::strlen(text) + 1u;
    if (capacity < length)
        return false;
    std::memcpy(destination, text, length);
    return true;
}

inline bool writeAll(const clap_ostream_t *stream,
                     const std::uint8_t *data,
                     std::size_t size) noexcept {
    if (!stream || !stream->write || (!data && size != 0u))
        return false;
    std::size_t offset = 0u;
    while (offset < size) {
        const auto written = stream->write(stream, data + offset, size - offset);
        if (written <= 0 || static_cast<std::uint64_t>(written) > size - offset)
            return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

inline std::uint32_t loadU32Le(const std::uint8_t *source) noexcept {
    std::uint32_t value = 0u;
    for (unsigned index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8u);
    return value;
}

inline std::uint64_t loadU64Le(const std::uint8_t *source) noexcept {
    std::uint64_t value = 0u;
    for (unsigned index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(source[index]) << (index * 8u);
    return value;
}

inline void storeU32Le(std::uint8_t *destination, std::uint32_t value) noexcept {
    for (unsigned index = 0; index < 4; ++index)
        destination[index] = static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu);
}

inline void storeU64Le(std::uint8_t *destination, std::uint64_t value) noexcept {
    for (unsigned index = 0; index < 8; ++index)
        destination[index] = static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu);
}

inline std::uint32_t floatBits(float value) noexcept {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float floatFromBits(std::uint32_t bits) noexcept {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

class UiParameterQueue {
public:
    static constexpr std::uint32_t kCapacity = 64u;
    static constexpr std::uint32_t kMessageSize = 24u;

    void init(const clap_host_t *host) noexcept {
        host_ = host;
        hostParams_ = nullptr;
        hostParamsResolved_ = false;
        writeIndex_.store(0u, std::memory_order_relaxed);
        readIndex_.store(0u, std::memory_order_relaxed);
        active_.store(false, std::memory_order_relaxed);
        openGestures_.fill(false);
    }

    void setActive(bool active) noexcept {
        active_.store(active, std::memory_order_release);
        if (!active && hasPending())
            schedulePending();
    }

    [[nodiscard]] bool isActive() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool receive(const void *buffer, std::uint32_t size) noexcept {
        if (!buffer || size != kMessageSize)
            return false;
        Command command{};
        if (!decode(static_cast<const std::uint8_t *>(buffer), command) || !canSchedule())
            return false;
        if (!pushPreservingGestureClosure(command))
            return false;
        schedulePending();
        return true;
    }

    void closeOpenGestures() noexcept {
        bool queued = false;
        for (std::size_t index = 0; index < openGestures_.size(); ++index) {
            if (!openGestures_[index])
                continue;
            Command command{};
            command.kind = CommandKind::GestureEnd;
            command.paramId = kFirstParameterId + static_cast<clap_id>(index);
            queued = pushPreservingGestureClosure(command) || queued;
        }
        if (queued)
            schedulePending();
    }

    std::uint32_t drain(const clap_output_events_t *out,
                        std::array<clap_event_param_value_t, kCapacity> &values) noexcept {
        std::uint32_t valueCount = 0u;
        if (!out || !out->try_push) {
            reschedulePendingIfInactive();
            return 0u;
        }

        Command command{};
        while (peek(command)) {
            if (command.kind == CommandKind::Value && valueCount >= values.size())
                break;
            if (!emit(out, command)) {
                reschedulePendingIfInactive();
                break;
            }
            if (command.kind == CommandKind::Value)
                initialiseValueEvent(values[valueCount++], command.paramId, command.value);
            pop();
        }
        return valueCount;
    }

private:
    enum class CommandKind : std::uint8_t {
        GestureBegin = 1,
        Value = 2,
        GestureEnd = 3,
    };

    struct Command {
        CommandKind kind = CommandKind::Value;
        clap_id paramId = CLAP_INVALID_ID;
        double value = 0.0;
    };

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "PolySynth WCLAP queue indices must remain lock-free");

    static bool decode(const std::uint8_t *bytes, Command &command) noexcept {
        if (bytes[0] != 'W' || bytes[1] != 'V' || bytes[2] != 'P' || bytes[3] != '1' ||
            bytes[5] != 0u || bytes[6] != 0u || bytes[7] != 0u ||
            bytes[12] != 0u || bytes[13] != 0u || bytes[14] != 0u || bytes[15] != 0u)
            return false;

        switch (bytes[4]) {
            case 1u: command.kind = CommandKind::GestureBegin; break;
            case 2u: command.kind = CommandKind::Value; break;
            case 3u: command.kind = CommandKind::GestureEnd; break;
            default: return false;
        }

        command.paramId = loadU32Le(bytes + 8u);
        const auto *spec = parameterSpecForId(command.paramId);
        if (!spec)
            return false;

        const auto valueBits = loadU64Le(bytes + 16u);
        std::memcpy(&command.value, &valueBits, sizeof(command.value));
        if (command.kind != CommandKind::Value)
            return valueBits == 0u;
        if (!std::isfinite(command.value) || command.value < spec->minValue ||
            command.value > spec->maxValue)
            return false;
        if ((spec->flags & CLAP_PARAM_IS_STEPPED) != 0u &&
            command.value != std::trunc(command.value))
            return false;
        return true;
    }

    [[nodiscard]] std::size_t gestureIndex(clap_id paramId) const noexcept {
        if (paramId < kFirstParameterId)
            return openGestures_.size();
        const auto index = static_cast<std::size_t>(paramId - kFirstParameterId);
        return index < openGestures_.size() ? index : openGestures_.size();
    }

    [[nodiscard]] std::uint32_t openGestureCount() const noexcept {
        std::uint32_t count = 0u;
        for (bool open : openGestures_)
            count += open ? 1u : 0u;
        return count;
    }

    bool pushPreservingGestureClosure(const Command &command) noexcept {
        const auto index = gestureIndex(command.paramId);
        if (index >= openGestures_.size())
            return false;

        auto reservedAfter = openGestureCount();
        if (command.kind == CommandKind::GestureBegin) {
            if (openGestures_[index])
                return false;
            ++reservedAfter;
        } else if (command.kind == CommandKind::GestureEnd) {
            if (!openGestures_[index])
                return false;
            --reservedAfter;
        }

        if (freeSlots() < reservedAfter + 1u || !push(command))
            return false;
        if (command.kind == CommandKind::GestureBegin)
            openGestures_[index] = true;
        else if (command.kind == CommandKind::GestureEnd)
            openGestures_[index] = false;
        return true;
    }

    bool push(const Command &command) noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto next = increment(write);
        if (next == readIndex_.load(std::memory_order_acquire))
            return false;
        commands_[write] = command;
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    bool peek(Command &command) const noexcept {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        if (read == writeIndex_.load(std::memory_order_acquire))
            return false;
        command = commands_[read];
        return true;
    }

    void pop() noexcept {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        readIndex_.store(increment(read), std::memory_order_release);
    }

    [[nodiscard]] bool hasPending() const noexcept {
        return readIndex_.load(std::memory_order_acquire) !=
               writeIndex_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint32_t freeSlots() const noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        const auto used = write >= read ? write - read : kCapacity - (read - write);
        return (kCapacity - 1u) - used;
    }

    static constexpr std::uint32_t increment(std::uint32_t value) noexcept {
        return (value + 1u) % kCapacity;
    }

    [[nodiscard]] bool resolveHostParams() const noexcept {
        if (!hostParamsResolved_) {
            hostParamsResolved_ = true;
            if (host_ && host_->get_extension) {
                hostParams_ = static_cast<const clap_host_params_t *>(
                    host_->get_extension(host_, CLAP_EXT_PARAMS));
            }
        }
        return hostParams_ && hostParams_->request_flush;
    }

    [[nodiscard]] bool canSchedule() const noexcept {
        if (isActive())
            return host_ && host_->request_process;
        return resolveHostParams() || (host_ && host_->request_process);
    }

    void schedulePending() const noexcept {
        if (isActive()) {
            if (host_ && host_->request_process)
                host_->request_process(host_);
            return;
        }
        if (resolveHostParams()) {
            hostParams_->request_flush(host_);
            return;
        }
        if (host_ && host_->request_process)
            host_->request_process(host_);
    }

    void reschedulePendingIfInactive() const noexcept {
        if (!isActive() && hasPending())
            schedulePending();
    }

    static void initialiseHeader(clap_event_header_t &header,
                                 std::uint16_t type,
                                 std::uint32_t size) noexcept {
        header.size = size;
        header.time = 0u;
        header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        header.type = type;
        header.flags = CLAP_EVENT_IS_LIVE;
    }

    static void initialiseValueEvent(clap_event_param_value_t &event,
                                     clap_id paramId,
                                     double value) noexcept {
        event = {};
        initialiseHeader(event.header, CLAP_EVENT_PARAM_VALUE, sizeof(event));
        event.param_id = paramId;
        event.cookie = nullptr;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
    }

    static bool emit(const clap_output_events_t *out, const Command &command) noexcept {
        if (command.kind == CommandKind::Value) {
            clap_event_param_value_t event{};
            initialiseValueEvent(event, command.paramId, command.value);
            return out->try_push(out, &event.header);
        }
        clap_event_param_gesture_t event{};
        initialiseHeader(event.header,
                         command.kind == CommandKind::GestureBegin
                             ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                             : CLAP_EVENT_PARAM_GESTURE_END,
                         sizeof(event));
        event.param_id = command.paramId;
        return out->try_push(out, &event.header);
    }

    const clap_host_t *host_ = nullptr;
    mutable const clap_host_params_t *hostParams_ = nullptr;
    mutable bool hostParamsResolved_ = false;
    std::array<Command, kCapacity> commands_{};
    std::array<bool, kParameterCount> openGestures_{};
    std::atomic<std::uint32_t> writeIndex_{0u};
    std::atomic<std::uint32_t> readIndex_{0u};
    std::atomic<bool> active_{false};
};

struct MergedInputEvents {
    clap_input_events_t input{};
    const clap_event_param_value_t *uiValues = nullptr;
    std::uint32_t uiCount = 0u;
    const clap_input_events_t *hostInput = nullptr;
    std::uint32_t hostCount = 0u;
    bool valid = false;

    MergedInputEvents() noexcept {
        input.ctx = this;
        input.size = size;
        input.get = get;
    }

    bool prepare(const clap_event_param_value_t *values,
                 std::uint32_t valueCount,
                 const clap_input_events_t *host) noexcept {
        uiValues = values;
        uiCount = valueCount;
        hostInput = host;
        hostCount = 0u;
        valid = true;
        if (hostInput) {
            if (!hostInput->size || !hostInput->get)
                valid = false;
            else
                hostCount = hostInput->size(hostInput);
        }
        if (uiCount > std::numeric_limits<std::uint32_t>::max() - hostCount)
            valid = false;
        return valid;
    }

    static std::uint32_t CLAP_ABI size(const clap_input_events_t *events) noexcept {
        if (!events || !events->ctx)
            return 0u;
        const auto &self = *static_cast<const MergedInputEvents *>(events->ctx);
        return self.valid ? self.uiCount + self.hostCount : 0u;
    }

    static const clap_event_header_t *CLAP_ABI get(const clap_input_events_t *events,
                                                    std::uint32_t index) noexcept {
        if (!events || !events->ctx)
            return nullptr;
        const auto &self = *static_cast<const MergedInputEvents *>(events->ctx);
        if (!self.valid || index >= self.uiCount + self.hostCount)
            return nullptr;
        if (index < self.uiCount)
            return &self.uiValues[index].header;
        return self.hostInput->get(self.hostInput, index - self.uiCount);
    }
};

struct ProxyState {
    explicit ProxyState(const clap_host_t *newHost) noexcept : host(newHost), gui(nullptr, newHost) {
        uiQueue.init(newHost);
    }

    const clap_host_t *host = nullptr;
    ::webview_gui::ClapWebviewGui gui;
    UiParameterQueue uiQueue{};
    bool guiCreated = false;

    std::uint32_t activeVoicesRt = 0u;
    std::uint32_t fullNoteOnCreditsRt = 0u;
    std::uint32_t lastModParamIdRt = CLAP_INVALID_ID;
    float lastModAmountRt = 0.0f;
    std::uint32_t lastExpressionIdRt = CLAP_INVALID_ID;
    float lastExpressionValueRt = 0.0f;

    std::atomic<std::uint32_t> activeVoices{0u};
    std::atomic<std::uint32_t> peakLeftBits{0u};
    std::atomic<std::uint32_t> peakRightBits{0u};
    std::atomic<std::uint32_t> lastModParamId{CLAP_INVALID_ID};
    std::atomic<std::uint32_t> lastModAmountBits{0u};
    std::atomic<std::uint32_t> lastExpressionId{CLAP_INVALID_ID};
    std::atomic<std::uint32_t> lastExpressionValueBits{0u};

    void resetTelemetry() noexcept {
        activeVoicesRt = 0u;
        fullNoteOnCreditsRt = 0u;
        lastModParamIdRt = CLAP_INVALID_ID;
        lastModAmountRt = 0.0f;
        lastExpressionIdRt = CLAP_INVALID_ID;
        lastExpressionValueRt = 0.0f;
        activeVoices.store(0u, std::memory_order_release);
        peakLeftBits.store(0u, std::memory_order_release);
        peakRightBits.store(0u, std::memory_order_release);
        lastModParamId.store(CLAP_INVALID_ID, std::memory_order_release);
        lastModAmountBits.store(0u, std::memory_order_release);
        lastExpressionId.store(CLAP_INVALID_ID, std::memory_order_release);
        lastExpressionValueBits.store(0u, std::memory_order_release);
    }

    void observeInput(const clap_input_events_t *events) noexcept {
        if (!events || !events->size || !events->get)
            return;
        const auto count = events->size(events);
        for (std::uint32_t index = 0u; index < count; ++index) {
            const auto *header = events->get(events, index);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
                header->size < sizeof(clap_event_header_t))
                continue;
            if (header->type == CLAP_EVENT_NOTE_ON && header->size >= sizeof(clap_event_note_t)) {
                if (activeVoicesRt < kPolySynthDefaultVoiceCount)
                    ++activeVoicesRt;
                else
                    ++fullNoteOnCreditsRt;
                continue;
            }
            if (header->type == CLAP_EVENT_PARAM_MOD &&
                header->size >= sizeof(clap_event_param_mod_t)) {
                const auto &event = *reinterpret_cast<const clap_event_param_mod_t *>(header);
                if (parameterSpecForId(event.param_id) && std::isfinite(event.amount)) {
                    lastModParamIdRt = event.param_id;
                    lastModAmountRt = static_cast<float>(event.amount);
                }
                continue;
            }
            if (header->type == CLAP_EVENT_NOTE_EXPRESSION &&
                header->size >= sizeof(clap_event_note_expression_t)) {
                const auto &event = *reinterpret_cast<const clap_event_note_expression_t *>(header);
                if (std::isfinite(event.value)) {
                    lastExpressionIdRt = event.expression_id;
                    lastExpressionValueRt = static_cast<float>(event.value);
                }
            }
        }
    }

    void observeOutput(const clap_event_header_t *header) noexcept {
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
            header->type != CLAP_EVENT_NOTE_END || header->size < sizeof(clap_event_note_t))
            return;
        if (fullNoteOnCreditsRt != 0u)
            --fullNoteOnCreditsRt;
        else if (activeVoicesRt != 0u)
            --activeVoicesRt;
    }

    void publishProcessTelemetry(const clap_process_t &process) noexcept {
        float leftPeak = 0.0f;
        float rightPeak = 0.0f;
        if (process.audio_outputs && process.audio_outputs_count == 1u) {
            const auto &output = process.audio_outputs[0];
            if (output.data32 && output.channel_count >= 2u && output.data32[0] && output.data32[1]) {
                for (std::uint32_t frame = 0u; frame < process.frames_count; ++frame) {
                    leftPeak = std::max(leftPeak, std::fabs(output.data32[0][frame]));
                    rightPeak = std::max(rightPeak, std::fabs(output.data32[1][frame]));
                }
            }
        }
        activeVoices.store(activeVoicesRt, std::memory_order_release);
        peakLeftBits.store(floatBits(leftPeak), std::memory_order_release);
        peakRightBits.store(floatBits(rightPeak), std::memory_order_release);
        lastModParamId.store(lastModParamIdRt, std::memory_order_release);
        lastModAmountBits.store(floatBits(lastModAmountRt), std::memory_order_release);
        lastExpressionId.store(lastExpressionIdRt, std::memory_order_release);
        lastExpressionValueBits.store(floatBits(lastExpressionValueRt), std::memory_order_release);
    }
};

struct TelemetryOutputEvents {
    clap_output_events_t output{};
    ProxyState *state = nullptr;
    const clap_output_events_t *downstream = nullptr;

    TelemetryOutputEvents(ProxyState *newState,
                          const clap_output_events_t *newDownstream) noexcept
        : state(newState), downstream(newDownstream) {
        output.ctx = this;
        output.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t *events,
                                 const clap_event_header_t *event) noexcept {
        if (!events || !events->ctx)
            return false;
        auto &self = *static_cast<TelemetryOutputEvents *>(events->ctx);
        if (!self.downstream || !self.downstream->try_push ||
            !self.downstream->try_push(self.downstream, event))
            return false;
        if (self.state)
            self.state->observeOutput(event);
        return true;
    }
};

struct PolySynthWclapPluginProxy {
    clap_plugin_t plugin{};
    const clap_plugin_t *innerPlugin = nullptr;
    const clap_plugin_params_t *innerParams = nullptr;
    ProxyState *state = nullptr;
    bool initialized = false;
    bool active = false;

    PolySynthWclapPluginProxy(const clap_plugin_t *inner,
                              ProxyState *newState) noexcept
        : plugin(*inner), innerPlugin(inner), state(newState) {
        plugin.init = proxyInit;
        plugin.destroy = proxyDestroy;
        plugin.activate = inner->activate ? proxyActivate : nullptr;
        plugin.deactivate = inner->deactivate ? proxyDeactivate : nullptr;
        plugin.start_processing = inner->start_processing ? proxyStartProcessing : nullptr;
        plugin.stop_processing = inner->stop_processing ? proxyStopProcessing : nullptr;
        plugin.reset = inner->reset ? proxyReset : nullptr;
        plugin.process = inner->process ? proxyProcess : nullptr;
        plugin.get_extension = proxyGetExtension;
        plugin.on_main_thread = inner->on_main_thread ? proxyOnMainThread : nullptr;
    }

    static PolySynthWclapPluginProxy *from(const clap_plugin_t *outer) noexcept {
        return reinterpret_cast<PolySynthWclapPluginProxy *>(
            const_cast<clap_plugin_t *>(outer));
    }

    static bool CLAP_ABI proxyInit(const clap_plugin_t *outer) {
        auto *self = from(outer);
        if (!self || self->initialized || !self->innerPlugin || !self->innerPlugin->init)
            return false;
        if (!self->innerPlugin->init(self->innerPlugin))
            return false;
        self->innerParams = self->innerPlugin->get_extension
                                ? static_cast<const clap_plugin_params_t *>(
                                      self->innerPlugin->get_extension(self->innerPlugin,
                                                                       CLAP_EXT_PARAMS))
                                : nullptr;
        if (!self->innerParams || !self->innerParams->count || !self->innerParams->get_info ||
            !self->innerParams->get_value || !self->innerParams->value_to_text ||
            !self->innerParams->text_to_value || !self->innerParams->flush)
            return false;

        self->initialized = true;
        self->state->uiQueue.init(self->state->host);
        self->state->gui.init(&self->plugin, self->state->host);
        if (!self->state->gui.setSize(kEditorWidth, kEditorHeight))
            return false;
        return true;
    }

    static void CLAP_ABI proxyDestroy(const clap_plugin_t *outer) {
        auto *self = from(outer);
        if (!self)
            return;
        auto *stateToDelete = self->state;
        const auto *innerToDestroy = self->innerPlugin;
        self->state = nullptr;
        self->innerPlugin = nullptr;
        self->innerParams = nullptr;
        self->initialized = false;
        self->active = false;
        if (stateToDelete) {
            if (stateToDelete->guiCreated)
                stateToDelete->gui.destroy();
            stateToDelete->guiCreated = false;
        }
        if (innerToDestroy && innerToDestroy->destroy)
            innerToDestroy->destroy(innerToDestroy);
        delete stateToDelete;
        delete self;
    }

    static bool CLAP_ABI proxyActivate(const clap_plugin_t *outer,
                                       double sampleRate,
                                       std::uint32_t minFrames,
                                       std::uint32_t maxFrames) {
        auto *self = from(outer);
        if (!self || !self->initialized || self->active || !self->innerPlugin ||
            !self->innerPlugin->activate)
            return false;
        if (!self->innerPlugin->activate(self->innerPlugin,
                                         sampleRate,
                                         minFrames,
                                         maxFrames))
            return false;
        self->active = true;
        self->state->uiQueue.setActive(true);
        self->state->resetTelemetry();
        return true;
    }

    static void CLAP_ABI proxyDeactivate(const clap_plugin_t *outer) {
        auto *self = from(outer);
        if (!self || !self->innerPlugin || !self->innerPlugin->deactivate)
            return;
        self->state->uiQueue.setActive(false);
        self->innerPlugin->deactivate(self->innerPlugin);
        self->active = false;
        self->state->resetTelemetry();
    }

    static bool CLAP_ABI proxyStartProcessing(const clap_plugin_t *outer) {
        auto *self = from(outer);
        return self && self->innerPlugin && self->innerPlugin->start_processing &&
               self->innerPlugin->start_processing(self->innerPlugin);
    }

    static void CLAP_ABI proxyStopProcessing(const clap_plugin_t *outer) {
        auto *self = from(outer);
        if (self && self->innerPlugin && self->innerPlugin->stop_processing)
            self->innerPlugin->stop_processing(self->innerPlugin);
    }

    static void CLAP_ABI proxyReset(const clap_plugin_t *outer) {
        auto *self = from(outer);
        if (!self)
            return;
        if (self->innerPlugin && self->innerPlugin->reset)
            self->innerPlugin->reset(self->innerPlugin);
        self->state->resetTelemetry();
    }

    static clap_process_status CLAP_ABI proxyProcess(const clap_plugin_t *outer,
                                                      const clap_process_t *process) {
        auto *self = from(outer);
        if (!self || !self->initialized || !self->active || !process ||
            !self->innerPlugin || !self->innerPlugin->process)
            return CLAP_PROCESS_ERROR;

        std::array<clap_event_param_value_t, UiParameterQueue::kCapacity> uiValues{};
        const auto uiCount = self->state->uiQueue.drain(process->out_events, uiValues);

        clap_process_t innerProcess = *process;
        MergedInputEvents mergedInput;
        if (uiCount != 0u) {
            if (!mergedInput.prepare(uiValues.data(), uiCount, process->in_events))
                return CLAP_PROCESS_ERROR;
            innerProcess.in_events = &mergedInput.input;
        }

        self->state->observeInput(process->in_events);
        TelemetryOutputEvents telemetryOutput{self->state, process->out_events};
        innerProcess.out_events = &telemetryOutput.output;
        const auto status = self->innerPlugin->process(self->innerPlugin, &innerProcess);
        if (status != CLAP_PROCESS_ERROR)
            self->state->publishProcessTelemetry(*process);
        return status;
    }

    static void CLAP_ABI proxyOnMainThread(const clap_plugin_t *outer) {
        auto *self = from(outer);
        if (self && self->innerPlugin && self->innerPlugin->on_main_thread)
            self->innerPlugin->on_main_thread(self->innerPlugin);
    }

    static const void *CLAP_ABI proxyGetExtension(const clap_plugin_t *outer,
                                                   const char *id) {
        auto *self = from(outer);
        if (!self || !id)
            return nullptr;

        // Historical WCLAP bridges query only clap.webview/3 before init. Return
        // a stable fail-closed table for that one compatibility probe. Every
        // other pre-init lookup preserves the strict inner plug-in behaviour.
        if (!self->initialized) {
            if (std::strcmp(id, ::webview_gui::CLAP_EXT_WEBVIEW) == 0)
                return &webviewProxy;
            return self->innerPlugin && self->innerPlugin->get_extension
                       ? self->innerPlugin->get_extension(self->innerPlugin, id)
                       : nullptr;
        }

        if (std::strcmp(id, CLAP_EXT_GUI) == 0)
            return &guiProxy;
        if (std::strcmp(id, ::webview_gui::CLAP_EXT_WEBVIEW) == 0)
            return &webviewProxy;
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &paramsProxy;
        return self->innerPlugin && self->innerPlugin->get_extension
                   ? self->innerPlugin->get_extension(self->innerPlugin, id)
                   : nullptr;
    }

    static std::uint32_t CLAP_ABI paramsCount(const clap_plugin_t *outer) {
        auto *self = from(outer);
        return self && self->innerParams ? self->innerParams->count(self->innerPlugin) : 0u;
    }

    static bool CLAP_ABI paramsGetInfo(const clap_plugin_t *outer,
                                       std::uint32_t index,
                                       clap_param_info_t *info) {
        auto *self = from(outer);
        return self && self->innerParams &&
               self->innerParams->get_info(self->innerPlugin, index, info);
    }

    static bool CLAP_ABI paramsGetValue(const clap_plugin_t *outer,
                                        clap_id paramId,
                                        double *value) {
        auto *self = from(outer);
        return self && self->innerParams &&
               self->innerParams->get_value(self->innerPlugin, paramId, value);
    }

    static bool CLAP_ABI paramsValueToText(const clap_plugin_t *outer,
                                           clap_id paramId,
                                           double value,
                                           char *display,
                                           std::uint32_t size) {
        auto *self = from(outer);
        return self && self->innerParams &&
               self->innerParams->value_to_text(self->innerPlugin,
                                                paramId,
                                                value,
                                                display,
                                                size);
    }

    static bool CLAP_ABI paramsTextToValue(const clap_plugin_t *outer,
                                           clap_id paramId,
                                           const char *display,
                                           double *value) {
        auto *self = from(outer);
        return self && self->innerParams &&
               self->innerParams->text_to_value(self->innerPlugin,
                                                paramId,
                                                display,
                                                value);
    }

    static void CLAP_ABI paramsFlush(const clap_plugin_t *outer,
                                     const clap_input_events_t *in,
                                     const clap_output_events_t *out) {
        auto *self = from(outer);
        if (!self || !self->innerParams)
            return;
        if (self->state->uiQueue.isActive()) {
            self->innerParams->flush(self->innerPlugin, in, out);
            return;
        }

        std::array<clap_event_param_value_t, UiParameterQueue::kCapacity> uiValues{};
        const auto uiCount = self->state->uiQueue.drain(out, uiValues);
        if (uiCount == 0u) {
            self->innerParams->flush(self->innerPlugin, in, out);
            return;
        }
        MergedInputEvents mergedInput;
        if (!mergedInput.prepare(uiValues.data(), uiCount, in))
            return;
        self->innerParams->flush(self->innerPlugin, &mergedInput.input, out);
    }

    static bool CLAP_ABI guiIsApiSupported(const clap_plugin_t *outer,
                                            const char *api,
                                            bool isFloating) {
        auto *self = from(outer);
        return self && self->initialized && self->state->gui.isApiSupported(api, isFloating);
    }

    static bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t *outer,
                                            const char **api,
                                            bool *isFloating) {
        auto *self = from(outer);
        return self && self->initialized && self->state->gui.getPreferredApi(api, isFloating);
    }

    static bool CLAP_ABI guiCreate(const clap_plugin_t *outer,
                                   const char *api,
                                   bool isFloating) {
        auto *self = from(outer);
        if (!self || !self->initialized || self->state->guiCreated ||
            !self->state->gui.create(api, isFloating))
            return false;
        self->state->guiCreated = true;
        return true;
    }

    static void CLAP_ABI guiDestroy(const clap_plugin_t *outer) {
        auto *self = from(outer);
        if (!self || !self->state->guiCreated)
            return;
        self->state->uiQueue.closeOpenGestures();
        self->state->gui.destroy();
        self->state->guiCreated = false;
    }

    static bool CLAP_ABI guiSetScale(const clap_plugin_t *, double) { return false; }

    static bool CLAP_ABI guiGetSize(const clap_plugin_t *outer,
                                    std::uint32_t *width,
                                    std::uint32_t *height) {
        auto *self = from(outer);
        return self && self->state->guiCreated && self->state->gui.getSize(width, height);
    }

    static bool CLAP_ABI guiCanResize(const clap_plugin_t *outer) {
        auto *self = from(outer);
        return self && self->state->guiCreated && self->state->gui.canResize();
    }

    static bool CLAP_ABI guiGetResizeHints(const clap_plugin_t *outer,
                                           clap_gui_resize_hints_t *hints) {
        auto *self = from(outer);
        return self && self->state->guiCreated && self->state->gui.getResizeHints(hints);
    }

    static bool CLAP_ABI guiAdjustSize(const clap_plugin_t *outer,
                                       std::uint32_t *width,
                                       std::uint32_t *height) {
        auto *self = from(outer);
        return self && self->state->guiCreated && self->state->gui.adjustSize(width, height);
    }

    static bool CLAP_ABI guiSetSize(const clap_plugin_t *outer,
                                    std::uint32_t width,
                                    std::uint32_t height) {
        auto *self = from(outer);
        return self && self->state->guiCreated && self->state->gui.setSize(width, height);
    }

    static bool CLAP_ABI guiSetParent(const clap_plugin_t *outer,
                                      const clap_window_t *window) {
        auto *self = from(outer);
        return self && self->state->guiCreated && self->state->gui.setParent(window);
    }

    static bool CLAP_ABI guiSetTransient(const clap_plugin_t *outer,
                                         const clap_window_t *window) {
        auto *self = from(outer);
        return self && self->state->guiCreated && self->state->gui.setTransient(window);
    }

    static void CLAP_ABI guiSuggestTitle(const clap_plugin_t *outer,
                                         const char *title) {
        auto *self = from(outer);
        if (self && self->state->guiCreated)
            self->state->gui.suggestTitle(title);
    }

    static bool CLAP_ABI guiShow(const clap_plugin_t *outer) {
        auto *self = from(outer);
        return self && self->state->guiCreated && self->state->gui.show();
    }

    static bool CLAP_ABI guiHide(const clap_plugin_t *outer) {
        auto *self = from(outer);
        return self && self->state->guiCreated && self->state->gui.hide();
    }

    static int32_t CLAP_ABI webviewGetUri(const clap_plugin_t *outer,
                                          char *uri,
                                          std::uint32_t uriCapacity) {
        auto *self = from(outer);
        if (!self || !self->initialized)
            return -1;
        constexpr auto required = sizeof(kEditorUri);
        if (uriCapacity == 0u)
            return static_cast<int32_t>(required);
        if (!uri)
            return -1;
        const auto copyLength = std::min<std::size_t>(required - 1u, uriCapacity - 1u);
        if (copyLength != 0u)
            std::memcpy(uri, kEditorUri, copyLength);
        uri[copyLength] = '\0';
        return static_cast<int32_t>(required);
    }

    static bool CLAP_ABI webviewGetResource(const clap_plugin_t *outer,
                                             const char *path,
                                             char *mime,
                                             std::uint32_t mimeCapacity,
                                             const clap_ostream_t *stream) {
        auto *self = from(outer);
        if (!self || !self->initialized || !path || !stream || !stream->write)
            return false;
        const char *resourceMime = nullptr;
        const std::uint8_t *resourceData = nullptr;
        std::size_t resourceSize = 0u;
        if (std::strcmp(path, kEditorUri) == 0) {
            resourceMime = kEditorMime;
            resourceData = reinterpret_cast<const std::uint8_t *>(kEditorHtml);
            resourceSize = sizeof(kEditorHtml) - 1u;
        } else if (std::strcmp(path, kEditorScriptUri) == 0) {
            resourceMime = kEditorScriptMime;
            resourceData = reinterpret_cast<const std::uint8_t *>(kEditorScript);
            resourceSize = sizeof(kEditorScript) - 1u;
        } else {
            return false;
        }
        return copyExactText(mime, mimeCapacity, resourceMime) &&
               writeAll(stream, resourceData, resourceSize);
    }

    static bool CLAP_ABI webviewReceive(const clap_plugin_t *outer,
                                        const void *buffer,
                                        std::uint32_t size) {
        auto *self = from(outer);
        if (!self || !self->initialized || !self->state->guiCreated ||
            !self->state->gui.isOnGuiThread() || !buffer)
            return false;
        if (size == 4u) {
            const auto *bytes = static_cast<const std::uint8_t *>(buffer);
            if (bytes[0] == 'W' && bytes[1] == 'V' && bytes[2] == 'S' && bytes[3] == '1')
                return self->sendUiSnapshot();
            return false;
        }
        return self->state->uiQueue.receive(buffer, size);
    }

    bool sendUiSnapshot() noexcept {
        if (!innerParams || !state || !state->guiCreated)
            return false;
        for (const auto &spec : kParameterSpecs) {
            double value = 0.0;
            if (!innerParams->get_value(innerPlugin, spec.id, &value) || !std::isfinite(value))
                return false;
            std::array<std::uint8_t, 16> message{};
            message[0] = 'W';
            message[1] = 'V';
            message[2] = 'B';
            message[3] = '1';
            storeU32Le(message.data() + 4u, spec.id);
            std::uint64_t bits = 0u;
            std::memcpy(&bits, &value, sizeof(bits));
            storeU64Le(message.data() + 8u, bits);
            if (!state->gui.send(message.data(), message.size()))
                return false;
        }

        std::array<std::uint8_t, 32> telemetry{};
        telemetry[0] = 'W';
        telemetry[1] = 'V';
        telemetry[2] = 'T';
        telemetry[3] = '1';
        storeU32Le(telemetry.data() + 4u,
                   state->activeVoices.load(std::memory_order_acquire));
        storeU32Le(telemetry.data() + 8u,
                   state->peakLeftBits.load(std::memory_order_acquire));
        storeU32Le(telemetry.data() + 12u,
                   state->peakRightBits.load(std::memory_order_acquire));
        storeU32Le(telemetry.data() + 16u,
                   state->lastModParamId.load(std::memory_order_acquire));
        storeU32Le(telemetry.data() + 20u,
                   state->lastModAmountBits.load(std::memory_order_acquire));
        storeU32Le(telemetry.data() + 24u,
                   state->lastExpressionId.load(std::memory_order_acquire));
        storeU32Le(telemetry.data() + 28u,
                   state->lastExpressionValueBits.load(std::memory_order_acquire));
        return state->gui.send(telemetry.data(), telemetry.size());
    }

    inline static const clap_plugin_params_t paramsProxy{
        paramsCount,
        paramsGetInfo,
        paramsGetValue,
        paramsValueToText,
        paramsTextToValue,
        paramsFlush,
    };

    inline static const clap_plugin_gui_t guiProxy{
        guiIsApiSupported,
        guiGetPreferredApi,
        guiCreate,
        guiDestroy,
        guiSetScale,
        guiGetSize,
        guiCanResize,
        guiGetResizeHints,
        guiAdjustSize,
        guiSetSize,
        guiSetParent,
        guiSetTransient,
        guiSuggestTitle,
        guiShow,
        guiHide,
    };

    inline static const ::webview_gui::clap_plugin_webview webviewProxy{
        webviewGetUri,
        webviewGetResource,
        webviewReceive,
    };
};

static_assert(std::is_standard_layout_v<PolySynthWclapPluginProxy>,
              "PolySynth WCLAP proxy must stay pointer-interconvertible with clap_plugin_t");
static_assert(offsetof(PolySynthWclapPluginProxy, plugin) == 0u,
              "PolySynth WCLAP proxy requires clap_plugin_t as its first member");

} // namespace detail

inline const clap_plugin_t *wrapPolySynthWclapPlugin(const clap_plugin_t *inner,
                                                      const clap_host_t *host) noexcept {
    if (!inner || !host || !inner->init || !inner->destroy || !inner->get_extension ||
        !inner->process)
        return nullptr;
    auto *state = new (std::nothrow) detail::ProxyState(host);
    if (!state)
        return nullptr;
    auto *proxy = new (std::nothrow) detail::PolySynthWclapPluginProxy(inner, state);
    if (!proxy) {
        delete state;
        return nullptr;
    }
    return &proxy->plugin;
}

} // namespace webview_gui::examples::polysynth::wclap
