import assert from "node:assert/strict";

import * as gain from "../gain/ui/src/bridge.js";
import * as polysynth from "../polysynth/ui/src/bridge.js";

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
    polysynth.sendEdit(1, 1005, 0.25); // continuous parameter
    polysynth.sendEdit(2, 1000, 2.0);  // stepped parameter
    polysynth.sendEdit(3, 1012, -3.0); // output parameter
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

console.log("WebView UI transport routes native top-level pages to window.postMessage and host-owned pages to parent.postMessage");
