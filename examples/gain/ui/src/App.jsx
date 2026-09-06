import React, { useEffect, useRef, useState } from "react";
import { EDIT, PARAM, PRESET, PRESET_KIND, parseHostMessage, requestPreset, requestSync, sendParameter } from "./bridge.js";

const emptyPreset = { currentKind: PRESET_KIND.NONE, currentIdentity: "", currentName: "Init", dirty: false, userMutations: false, entries: [] };

function slug(name) {
  const value = name.toLowerCase().replace(/[^a-z0-9_-]+/g, "-").replace(/^-+|-+$/g, "");
  return `${value || "preset"}.wvpreset`;
}

export default function App() {
  const [gain, setGain] = useState(0);
  const [bypass, setBypass] = useState(false);
  const [meter, setMeter] = useState({ left: 0, right: 0 });
  const [preset, setPreset] = useState(emptyPreset);
  const [presetName, setPresetName] = useState("");
  const gainGestureOpen = useRef(false);

  const beginGain = () => {
    if (gainGestureOpen.current) return;
    sendParameter(EDIT.BEGIN, PARAM.GAIN);
    gainGestureOpen.current = true;
  };
  const endGain = () => {
    if (!gainGestureOpen.current) return;
    sendParameter(EDIT.END, PARAM.GAIN);
    gainGestureOpen.current = false;
  };

  useEffect(() => {
    const onMessage = event => {
      const message = parseHostMessage(event.data);
      if (!message) return;
      if (message.type === "preset") setPreset(message);
      else if (message.type === "meter") setMeter({ left: message.left, right: message.right });
      else if (message.type === "parameter") {
        if (message.parameter === PARAM.GAIN && !gainGestureOpen.current) setGain(message.value);
        else if (message.parameter === PARAM.BYPASS) setBypass(message.value !== 0);
      }
    };
    const onPageHide = () => endGain();
    window.addEventListener("message", onMessage);
    window.addEventListener("pagehide", onPageHide);
    requestSync();
    requestPreset(PRESET.REFRESH);
    const syncTimer = window.setInterval(requestSync, 33);
    const presetTimer = window.setInterval(() => requestPreset(PRESET.SNAPSHOT), 250);
    return () => {
      window.clearInterval(syncTimer);
      window.clearInterval(presetTimer);
      window.removeEventListener("message", onMessage);
      window.removeEventListener("pagehide", onPageHide);
      endGain();
    };
  }, []);

  const factoryEntries = preset.entries.filter(entry => entry.kind === PRESET_KIND.FACTORY);
  const userEntries = preset.entries.filter(entry => entry.kind === PRESET_KIND.USER);
  const selection = preset.currentIdentity ? `${preset.currentKind}:${preset.currentIdentity}` : "";

  const loadSelection = value => {
    const separator = value.indexOf(":");
    if (separator <= 0) return;
    requestPreset(PRESET.LOAD, Number(value.slice(0, separator)), value.slice(separator + 1));
  };

  const save = () => {
    if (!preset.userMutations) return;
    const name = presetName.trim();
    if (!name) return;
    const overwrite = preset.currentKind === PRESET_KIND.USER && preset.currentIdentity.length > 0;
    requestPreset(PRESET.SAVE_AS, PRESET_KIND.USER, overwrite ? preset.currentIdentity : slug(name), name, overwrite);
  };

  return (
    <main className="mx-auto grid min-h-screen w-full max-w-[30rem] place-content-center gap-3 px-6 py-4">
      <header className="flex items-baseline justify-between gap-4">
        <h1 className="text-lg font-semibold">Gain</h1>
        <output className="font-mono text-sm">{gain.toFixed(2)} dB</output>
      </header>

      <section className="grid gap-2 rounded-lg border border-neutral-700 bg-neutral-900 p-3" aria-label="Preset browser">
        <div className="flex items-center gap-2">
          <strong className="min-w-0 flex-1 truncate">{preset.currentName || "Init"}{preset.dirty ? " *" : ""}</strong>
          <button className="rounded border border-neutral-600 px-2 py-1" onClick={() => requestPreset(PRESET.PREVIOUS)} title="Previous preset">◀</button>
          <button className="rounded border border-neutral-600 px-2 py-1" onClick={() => requestPreset(PRESET.NEXT)} title="Next preset">▶</button>
        </div>
        <select className="w-full rounded border border-neutral-600 bg-neutral-950 p-1" value={selection} onChange={event => loadSelection(event.target.value)} aria-label="Preset">
          {!selection && <option value="">Select preset</option>}
          {factoryEntries.length > 0 && <optgroup label="Factory">{factoryEntries.map(entry => <option key={`f:${entry.identity}`} value={`${entry.kind}:${entry.identity}`}>{entry.name}</option>)}</optgroup>}
          {userEntries.length > 0 && <optgroup label="User">{userEntries.map(entry => <option key={`u:${entry.identity}`} value={`${entry.kind}:${entry.identity}`}>{entry.name}</option>)}</optgroup>}
        </select>
        <div className="flex items-center gap-2">
          <button className="rounded border border-neutral-600 px-2 py-1" onClick={() => requestPreset(PRESET.INIT)}>Init</button>
          <input className="min-w-0 flex-1 rounded border border-neutral-600 bg-neutral-950 p-1" value={presetName} onChange={event => setPresetName(event.target.value)} maxLength={96} placeholder="User preset name" />
          <button className="rounded border border-neutral-600 px-2 py-1 disabled:opacity-40" disabled={!preset.userMutations} onClick={save}>Save As</button>
          <button className="rounded border border-neutral-600 px-2 py-1 disabled:opacity-40" disabled={!preset.userMutations || preset.currentKind !== PRESET_KIND.USER || !preset.currentIdentity} onClick={() => requestPreset(PRESET.DELETE, PRESET_KIND.USER, preset.currentIdentity)}>Delete</button>
        </div>
      </section>

      <label className="grid gap-1 text-sm">Gain
        <input type="range" min="-60" max="12" step="0.1" value={gain}
          onPointerDown={beginGain}
          onInput={event => { beginGain(); const value = Number(event.currentTarget.value); if (Number.isFinite(value)) { setGain(value); sendParameter(EDIT.VALUE, PARAM.GAIN, value); } }}
          onPointerUp={endGain} onPointerCancel={endGain} onKeyUp={endGain} onBlur={endGain} />
      </label>

      <label className="flex items-center justify-between gap-3 text-sm">Bypass
        <input type="checkbox" checked={bypass} onChange={event => { const value = event.currentTarget.checked; setBypass(value); sendParameter(EDIT.BEGIN, PARAM.BYPASS); sendParameter(EDIT.VALUE, PARAM.BYPASS, value ? 1 : 0); sendParameter(EDIT.END, PARAM.BYPASS); }} />
      </label>

      <section className="grid grid-cols-2 gap-3 text-sm" aria-label="Stereo output meter">
        <label className="grid gap-1">L<meter min="0" max="1" value={meter.left} /></label>
        <label className="grid gap-1">R<meter min="0" max="1" value={meter.right} /></label>
      </section>
      <small className="text-xs text-neutral-400">Edits are sent as live CLAP parameter gestures through the WebView bridge.</small>
    </main>
  );
}
