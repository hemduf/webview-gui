import { postToHost } from "../../../common/ui/post_to_host.js";

export const PARAM = Object.freeze({ GAIN: 1, BYPASS: 2 });
export const EDIT = Object.freeze({ BEGIN: 1, VALUE: 2, END: 3 });
export const PRESET = Object.freeze({ SNAPSHOT: 1, LOAD: 2, NEXT: 3, PREVIOUS: 4, INIT: 5, SAVE_AS: 6, DELETE: 7, REFRESH: 8 });
export const PRESET_KIND = Object.freeze({ NONE: 0, FACTORY: 2, USER: 3 });

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });
const syncRequest = new Uint8Array([0x57, 0x56, 0x51, 0x31]).buffer;

export function sendParameter(kind, parameter, value = 0) {
  const buffer = new ArrayBuffer(16);
  const bytes = new Uint8Array(buffer);
  bytes.set([0x57, 0x56, 0x47, 0x31]);
  bytes[4] = kind;
  bytes[5] = parameter;
  new DataView(buffer).setFloat64(8, value, true);
  postToHost(buffer);
}

export function requestSync() {
  postToHost(syncRequest);
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
        const length = view.getUint16(state.offset, true);
        state.offset += 2;
        tags.push(readString(bytes, view, state, length));
      }
      entries.push({ kind, identity, name, tags });
    }
    if (state.offset !== bytes.length) throw new Error("trailing data");
    return { type: "preset", currentKind, currentIdentity, currentName, dirty: (flags & 1) !== 0, userMutations: (flags & 2) !== 0, entries };
  } catch (_) {
    return null;
  }
}

export function parseHostMessage(data) {
  const preset = parsePreset(data);
  if (preset) return preset;
  if (!(data instanceof ArrayBuffer) || data.byteLength !== 16) return null;
  const bytes = new Uint8Array(data);
  if (bytes[0] !== 0x57 || bytes[1] !== 0x56) return null;
  const view = new DataView(data);
  if (bytes[2] === 0x4d && bytes[3] === 0x31) {
    const left = view.getFloat32(4, true);
    const right = view.getFloat32(8, true);
    if (!Number.isFinite(left) || !Number.isFinite(right) || left < 0 || right < 0) return null;
    return { type: "meter", left: Math.min(left, 1), right: Math.min(right, 1) };
  }
  if (bytes[2] !== 0x55 || bytes[3] !== 0x31 || bytes[5] !== 0 || bytes[6] !== 0 || bytes[7] !== 0) return null;
  const value = view.getFloat64(8, true);
  return Number.isFinite(value) ? { type: "parameter", parameter: bytes[4], value } : null;
}
