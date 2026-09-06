export const PRESET = Object.freeze({ SNAPSHOT: 1, LOAD: 2, NEXT: 3, PREVIOUS: 4, INIT: 5, SAVE_AS: 6, DELETE: 7, REFRESH: 8 });
export const PRESET_KIND = Object.freeze({ NONE: 0, FACTORY: 2, USER: 3 });

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });

function postToHost(data) {
  if (window.parent === window) {
    window.postMessage(data, "*");
    return;
  }
  window.parent.postMessage(data, "*");
}

export function sendEdit(kind, paramId, value = 0) {
  const buffer = new ArrayBuffer(24);
  const bytes = new Uint8Array(buffer);
  bytes.set([0x57, 0x56, 0x50, 0x31]);
  bytes[4] = kind;
  const view = new DataView(buffer);
  view.setUint32(8, paramId, true);
  view.setFloat64(16, value, true);
  postToHost(buffer);
}

export function requestSync() {
  postToHost(new Uint8Array([0x57, 0x56, 0x53, 0x31]).buffer);
}

export function requestPreset(command, kind = PRESET_KIND.NONE, identity = "", name = "", overwrite = false) {
  const identityBytes = encoder.encode(identity);
  const nameBytes = encoder.encode(name);
  if (identityBytes.length > 1024 || nameBytes.length > 1024) return;
  const buffer = new ArrayBuffer(12 + identityBytes.length + nameBytes.length);
  const bytes = new Uint8Array(buffer);
  bytes.set([0x57, 0x56, 0x50, 0x32]);
  bytes[4] = command;
  bytes[5] = kind;
  bytes[6] = overwrite ? 1 : 0;
  const view = new DataView(buffer);
  view.setUint16(8, identityBytes.length, true);
  view.setUint16(10, nameBytes.length, true);
  bytes.set(identityBytes, 12);
  bytes.set(nameBytes, 12 + identityBytes.length);
  postToHost(buffer);
}

function readString(bytes, view, state, length) {
  if (length > 1024 || state.offset + length > bytes.length) throw new Error("invalid string");
  const value = decoder.decode(bytes.slice(state.offset, state.offset + length));
  state.offset += length;
  return value;
}

function parsePreset(data) {
  if (!(data instanceof ArrayBuffer) || data.byteLength < 12 || data.byteLength > 65535) return null;
  const bytes = new Uint8Array(data);
  if (bytes[0] !== 0x57 || bytes[1] !== 0x56 || bytes[2] !== 0x42 || bytes[3] !== 0x32) return null;
  try {
    const view = new DataView(data);
    const state = { offset: 12 };
    const currentKind = bytes[4];
    const flags = bytes[5];
    const count = view.getUint16(6, true);
    const currentIdentity = readString(bytes, view, state, view.getUint16(8, true));
    const currentName = readString(bytes, view, state, view.getUint16(10, true));
    const entries = [];
    for (let i = 0; i < count; ++i) {
      if (state.offset + 6 > bytes.length) throw new Error("truncated entry");
      const kind = bytes[state.offset];
      const tagCount = bytes[state.offset + 1];
      const identityLength = view.getUint16(state.offset + 2, true);
      const nameLength = view.getUint16(state.offset + 4, true);
      state.offset += 6;
      const identity = readString(bytes, view, state, identityLength);
      const name = readString(bytes, view, state, nameLength);
      const tags = [];
      for (let tag = 0; tag < tagCount; ++tag) {
        if (state.offset + 2 > bytes.length) throw new Error("truncated tag");
        const length = view.getUint16(state.offset, true); state.offset += 2;
        tags.push(readString(bytes, view, state, length));
      }
      entries.push({ kind, identity, name, tags });
    }
    if (state.offset !== bytes.length) throw new Error("trailing data");
    return { type: "preset", currentKind, currentIdentity, currentName, dirty: (flags & 1) !== 0, userMutations: (flags & 2) !== 0, entries };
  } catch (_) { return null; }
}

export function parseHostMessage(data) {
  const preset = parsePreset(data);
  if (preset) return preset;
  if (!(data instanceof ArrayBuffer)) return null;
  const bytes = new Uint8Array(data);
  const view = new DataView(data);
  if (bytes.length === 16 && bytes[0] === 0x57 && bytes[1] === 0x56 && bytes[2] === 0x42 && bytes[3] === 0x31) {
    const value = view.getFloat64(8, true);
    return Number.isFinite(value) ? { type: "base", id: view.getUint32(4, true), value } : null;
  }
  if (bytes.length === 32 && bytes[0] === 0x57 && bytes[1] === 0x56 && bytes[2] === 0x54 && bytes[3] === 0x31) {
    return {
      type: "telemetry",
      voices: view.getUint32(4, true),
      left: Math.min(1, Math.max(0, view.getFloat32(8, true))),
      right: Math.min(1, Math.max(0, view.getFloat32(12, true))),
      modId: view.getUint32(16, true),
      modAmount: view.getFloat32(20, true),
      expressionId: view.getUint32(24, true),
      expressionValue: view.getFloat32(28, true),
    };
  }
  return null;
}
