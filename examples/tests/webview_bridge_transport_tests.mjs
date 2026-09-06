import assert from "node:assert/strict";

import * as gain from "../gain/ui/src/bridge.js";
import * as polysynth from "../polysynth/ui/src/bridge.js";
import { applyModulationTelemetry, emptyModulation, modulationKey } from "../polysynth/ui/src/modulation_state.js";

function withWindow(mockWindow, callback) {
  const previous = globalThis.window;
  globalThis.window = mockWindow;
  try {
    callback();
  } finally {
    if (previous === undefined) delete globalThis.window;
    else globalThis.window = previous;
  }
}

function topLevelWindow() {
  const calls = [];
  const self = {
    postMessage(data, targetOrigin) {
      calls.push({ data, targetOrigin, target: "self" });
    },
  };
  self.parent = self;
  return { window: self, calls };
}

function hostedWindow() {
  const calls = [];
  const parent = {
    postMessage(data, targetOrigin) {
      calls.push({ data, targetOrigin, target: "parent" });
    },
  };
  const self = {
    parent,
    postMessage() {
      throw new Error("host-owned WebView traffic must be posted to parent");
    },
  };
  return { window: self, calls };
}

function bytesOf(call) {
  assert.ok(call.data instanceof ArrayBuffer);
  assert.equal(call.targetOrigin, "*");
  return new Uint8Array(call.data);
}

function makeWvt2(dropped, records) {
  const buffer = new ArrayBuffer(12 + records.length * 32);
  const bytes = new Uint8Array(buffer);
  bytes.set([0x57, 0x56, 0x54, 0x32]);
  const view = new DataView(buffer);
  view.setUint32(4, dropped, true);
  view.setUint32(8, records.length, true);
  records.forEach((record, index) => {
    const offset = 12 + index * 32;
    view.setUint32(offset, record.kind, true);
    view.setUint32(offset + 4, record.sampleOffset ?? 0, true);
    view.setUint32(offset + 8, record.paramId ?? 0xffffffff, true);
    view.setInt32(offset + 12, record.noteId ?? -1, true);
    view.setInt32(offset + 16, record.port ?? -1, true);
    view.setInt32(offset + 20, record.channel ?? -1, true);
    view.setInt32(offset + 24, record.key ?? -1, true);
    view.setFloat32(offset + 28, record.amount ?? 0, true);
  });
  return buffer;
}

{
  const top = topLevelWindow();
  withWindow(top.window, () => {
    gain.sendParameter(gain.EDIT.BEGIN, gain.PARAM.GAIN);
    gain.sendParameter(gain.EDIT.VALUE, gain.PARAM.GAIN, -6.0);
    gain.sendParameter(gain.EDIT.END, gain.PARAM.GAIN);
    gain.sendParameter(gain.EDIT.BEGIN, gain.PARAM.BYPASS);
    gain.sendParameter(gain.EDIT.VALUE, gain.PARAM.BYPASS, 1.0);
    gain.sendParameter(gain.EDIT.END, gain.PARAM.BYPASS);
    gain.requestPreset(gain.PRESET.NEXT);
  });

  assert.equal(top.calls.length, 7);
  assert.ok(top.calls.every((call) => call.target === "self"));
  const gainValue = bytesOf(top.calls[1]);
  assert.deepEqual([...gainValue.slice(0, 6)], [0x57, 0x56, 0x47, 0x31, gain.EDIT.VALUE, gain.PARAM.GAIN]);
  assert.equal(new DataView(top.calls[1].data).getFloat64(8, true), -6.0);
  const bypassValue = bytesOf(top.calls[4]);
  assert.deepEqual([...bypassValue.slice(0, 6)], [0x57, 0x56, 0x47, 0x31, gain.EDIT.VALUE, gain.PARAM.BYPASS]);
  assert.equal(new DataView(top.calls[4].data).getFloat64(8, true), 1.0);
  assert.deepEqual([...bytesOf(top.calls[6]).slice(0, 4)], [0x57, 0x56, 0x50, 0x32]);
}

{
  const top = topLevelWindow();
  withWindow(top.window, () => {
    polysynth.sendEdit(1, 1004, 4400.0);
    polysynth.sendEdit(2, 1001, 2.0);
    polysynth.sendEdit(3, 1000, -3.0);
    polysynth.requestPreset(polysynth.PRESET.PREVIOUS);
  });

  assert.equal(top.calls.length, 4);
  assert.ok(top.calls.every((call) => call.target === "self"));
  for (const call of top.calls.slice(0, 3))
    assert.deepEqual([...bytesOf(call).slice(0, 4)], [0x57, 0x56, 0x50, 0x31]);
  assert.deepEqual([...bytesOf(top.calls[3]).slice(0, 4)], [0x57, 0x56, 0x50, 0x32]);
}

{
  const hosted = hostedWindow();
  withWindow(hosted.window, () => {
    gain.requestSync();
    polysynth.requestSync();
  });

  assert.equal(hosted.calls.length, 2);
  assert.ok(hosted.calls.every((call) => call.target === "parent"));
  assert.deepEqual([...bytesOf(hosted.calls[0])], [0x57, 0x56, 0x51, 0x31]);
  assert.deepEqual([...bytesOf(hosted.calls[1])], [0x57, 0x56, 0x53, 0x31]);
}

{
  const wire = makeWvt2(0, [
    { kind: polysynth.TELEMETRY_KIND.NOTE_ON, sampleOffset: 1, noteId: 42, port: 0, channel: 1, key: 60 },
    { kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 3, paramId: 1004, noteId: 42, port: 0, channel: 1, key: 60, amount: 0.25 },
    { kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 4, paramId: 1011, amount: -0.5 },
  ]);
  const parsed = polysynth.parseHostMessage(wire);
  assert.equal(parsed.type, "modulationTelemetry");
  assert.equal(parsed.dropped, 0);
  assert.equal(parsed.events.length, 3);
  assert.deepEqual(parsed.events[1], {
    kind: polysynth.TELEMETRY_KIND.MODULATION,
    sampleOffset: 3,
    paramId: 1004,
    noteId: 42,
    port: 0,
    channel: 1,
    key: 60,
    amount: 0.25,
  });
  assert.equal(parsed.events[2].noteId, -1);
  assert.equal(parsed.events[2].amount, -0.5);

  const reset = polysynth.parseHostMessage(makeWvt2(0, [{ kind: polysynth.TELEMETRY_KIND.RESET }]));
  assert.equal(reset.type, "modulationTelemetry");
  assert.equal(reset.events.length, 1);
  assert.equal(reset.events[0].kind, polysynth.TELEMETRY_KIND.RESET);

  const malformed = wire.slice(0, wire.byteLength - 1);
  assert.equal(polysynth.parseHostMessage(malformed), null);
}

{
  let state = emptyModulation;
  state = applyModulationTelemetry(state, {
    dropped: 0,
    events: [
      { kind: polysynth.TELEMETRY_KIND.NOTE_ON, sampleOffset: 0, paramId: 0xffffffff, noteId: 42, port: 0, channel: 1, key: 60, amount: 0 },
      { kind: polysynth.TELEMETRY_KIND.NOTE_ON, sampleOffset: 0, paramId: 0xffffffff, noteId: 43, port: 0, channel: 2, key: 64, amount: 0 },
      { kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 2, paramId: 1003, noteId: 42, port: 0, channel: 1, key: 60, amount: 0.1 },
      { kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 3, paramId: 1004, noteId: 42, port: 0, channel: 1, key: 60, amount: 0.25 },
      { kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 4, paramId: 1011, noteId: 43, port: 0, channel: 2, key: 64, amount: -0.5 },
      { kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 5, paramId: 1000, noteId: -1, port: -1, channel: -1, key: -1, amount: 0.15 },
    ],
  });

  assert.equal(Object.keys(state.current).length, 4, "simultaneous parameter/voice modulation must coexist");
  assert.equal(Object.keys(state.activeNotes).length, 2);
  assert.equal(state.last.paramId, 1000);
  assert.ok(state.current[modulationKey({ paramId: 1004, noteId: 42, port: 0, channel: 1, key: 60 })]);
  assert.ok(state.current[modulationKey({ paramId: 1011, noteId: 43, port: 0, channel: 2, key: 64 })]);

  state = applyModulationTelemetry(state, {
    dropped: 0,
    events: [{ kind: polysynth.TELEMETRY_KIND.NOTE_END, sampleOffset: 7, paramId: 0xffffffff, noteId: 42, port: 0, channel: 1, key: 60, amount: 0 }],
  });
  assert.equal(Object.keys(state.activeNotes).length, 1);
  assert.equal(Object.values(state.current).some(item => item.noteId === 42), false);
  assert.equal(Object.values(state.current).some(item => item.paramId === 1011), true);
  assert.equal(Object.values(state.current).some(item => item.paramId === 1000), true);
  assert.equal(state.last.paramId, 1000);

  state = applyModulationTelemetry(state, {
    dropped: 1,
    events: [
      { kind: polysynth.TELEMETRY_KIND.NOTE_ON, sampleOffset: 8, paramId: 0xffffffff, noteId: 99, port: 0, channel: 3, key: 67, amount: 0 },
      { kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 9, paramId: 1004, noteId: 99, port: 0, channel: 3, key: 67, amount: 0.8 },
    ],
  });
  assert.equal(Object.keys(state.current).length, 0, "overflow must discard the entire pre-drop batch instead of replaying potentially stale modulation");
  assert.equal(Object.keys(state.activeNotes).length, 0, "overflow must discard pre-drop NOTE_ON records as well");
  assert.equal(state.last.paramId, 1000);

  state = applyModulationTelemetry(state, {
    dropped: 1,
    events: [
      { kind: polysynth.TELEMETRY_KIND.NOTE_ON, sampleOffset: 10, paramId: 0xffffffff, noteId: 100, port: 0, channel: 4, key: 69, amount: 0 },
      { kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 11, paramId: 1004, noteId: 100, port: 0, channel: 4, key: 69, amount: 0.3 },
    ],
  });
  assert.equal(Object.keys(state.activeNotes).length, 1);
  assert.equal(Object.keys(state.current).length, 1);

  state = applyModulationTelemetry(state, {
    dropped: 0,
    events: [{ kind: polysynth.TELEMETRY_KIND.RESET, sampleOffset: 0, paramId: 0xffffffff, noteId: -1, port: -1, channel: -1, key: -1, amount: 0 }],
  });
  assert.equal(Object.keys(state.current).length, 0);
  assert.equal(Object.keys(state.activeNotes).length, 0);
  assert.equal(state.last, null);
}

{
  let state = emptyModulation;
  const anonymousNote = { kind: polysynth.TELEMETRY_KIND.NOTE_ON, sampleOffset: 0, paramId: 0xffffffff, noteId: -1, port: 0, channel: 5, key: 72, amount: 0 };
  state = applyModulationTelemetry(state, { dropped: 0, events: [anonymousNote, anonymousNote] });
  state = applyModulationTelemetry(state, {
    dropped: 0,
    events: [{ kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 1, paramId: 1004, noteId: -1, port: 0, channel: 5, key: 72, amount: 0.4 }],
  });
  const anonymousKey = "-1|0|5|72";
  assert.equal(state.activeNotes[anonymousKey].generationCount, 2, "no-note-id overlapping generations must not collapse into one lifecycle bit");
  assert.equal(Object.keys(state.current).length, 1);

  const anonymousEnd = { kind: polysynth.TELEMETRY_KIND.NOTE_END, sampleOffset: 2, paramId: 0xffffffff, noteId: -1, port: 0, channel: 5, key: 72, amount: 0 };
  state = applyModulationTelemetry(state, { dropped: 0, events: [anonymousEnd] });
  assert.equal(state.activeNotes[anonymousKey].generationCount, 1, "first NOTE_END must leave the overlapping generation active");
  assert.equal(Object.keys(state.current).length, 1, "targeted modulation remains current while a matching generation survives");
  state = applyModulationTelemetry(state, { dropped: 0, events: [anonymousEnd] });
  assert.equal(Object.keys(state.activeNotes).length, 0);
  assert.equal(Object.keys(state.current).length, 0, "last matching NOTE_END clears targeted modulation");

  state = applyModulationTelemetry(state, {
    dropped: 0,
    events: [
      { kind: polysynth.TELEMETRY_KIND.NOTE_ON, sampleOffset: 3, paramId: 0xffffffff, noteId: 7, port: 2, channel: 1, key: 60, amount: 0 },
      { kind: polysynth.TELEMETRY_KIND.MODULATION, sampleOffset: 4, paramId: 1004, noteId: 7, port: 2, channel: 1, key: 60, amount: 0.6 },
    ],
  });
  assert.equal(Object.keys(state.activeNotes).length, 0, "nonexistent PolySynth note ports must not create UI voice generations");
  assert.equal(Object.keys(state.current).length, 0, "modulation addressed only to a nonexistent port must not appear current");
}

console.log("WebView transport and PolySynth WVT2 modulation telemetry contracts passed");
