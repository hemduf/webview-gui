import React, { useEffect, useMemo, useRef, useState } from "react";
import { PRESET, PRESET_KIND, parseHostMessage, requestPreset, requestSync, sendEdit } from "./bridge.js";

const params = [
  { group: "Oscillator", id: 1001, label: "Waveform", type: "select", options: [[0, "Sine"], [1, "Saw"], [2, "Square"]], initial: 0 },
  { group: "Oscillator", id: 1002, label: "Coarse", min: -48, max: 48, step: 1, initial: 0 },
  { group: "Oscillator", id: 1003, label: "Fine", min: -100, max: 100, step: 0.1, initial: 0, mod: true },
  { group: "Filter", id: 1004, label: "Cutoff", min: 20, max: 20000, step: 1, initial: 6000, mod: true },
  { group: "Filter", id: 1005, label: "Resonance", min: 0, max: 0.99, step: 0.01, initial: 0, mod: true },
  { group: "Filter", id: 1010, label: "Env amount", min: -1, max: 1, step: 0.01, initial: 0, mod: true },
  { group: "Amp Envelope", id: 1006, label: "Attack", min: 0, max: 10, step: 0.01, initial: 0.01 },
  { group: "Amp Envelope", id: 1007, label: "Decay", min: 0, max: 10, step: 0.01, initial: 0.1 },
  { group: "Amp Envelope", id: 1008, label: "Sustain", min: 0, max: 1, step: 0.01, initial: 0.8 },
  { group: "Amp Envelope", id: 1009, label: "Release", min: 0.001, max: 10, step: 0.01, initial: 0.25 },
  { group: "Output", id: 1000, label: "Master gain", min: -60, max: 12, step: 0.1, initial: 0 },
  { group: "Output", id: 1011, label: "Pan", min: -1, max: 1, step: 0.01, initial: 0, mod: true },
  { group: "Output", id: 1012, label: "Amp level", min: 0, max: 1, step: 0.01, initial: 1, mod: true },
];
const groups = ["Oscillator", "Filter", "Amp Envelope", "Output"];
const expressionNames = ["volume", "pan", "tuning", "vibrato", "expression", "brightness", "pressure"];
const emptyPreset = { currentKind: PRESET_KIND.NONE, currentIdentity: "", currentName: "Init", dirty: false, userMutations: false, entries: [] };

function displayValue(spec, value) {
  if (spec.id === 1001) return spec.options[Math.max(0, Math.min(2, Math.trunc(value)))]?.[1] ?? "?";
  if (spec.id === 1002 || spec.id === 1004) return String(Math.round(value));
  if (spec.id === 1000 || spec.id === 1003) return value.toFixed(1);
  return value.toFixed(2);
}
function slug(value) {
  const normalized = value.trim().toLowerCase().replace(/[^a-z0-9._-]+/g, "-").replace(/^-+|-+$/g, "");
  return `${normalized || "preset"}.wvpreset`;
}

export default function App() {
  const initialValues = useMemo(() => Object.fromEntries(params.map(spec => [spec.id, spec.initial])), []);
  const [values, setValues] = useState(initialValues);
  const [telemetry, setTelemetry] = useState({ voices: 0, left: 0, right: 0, modId: 0xffffffff, modAmount: 0, expressionId: 0xffffffff, expressionValue: 0 });
  const [preset, setPreset] = useState(emptyPreset);
  const [query, setQuery] = useState("");
  const [tag, setTag] = useState("");
  const [presetName, setPresetName] = useState("");
  const openGestures = useRef(new Set());

  const begin = id => {
    if (openGestures.current.has(id)) return;
    openGestures.current.add(id);
    sendEdit(1, id);
  };
  const end = id => {
    if (!openGestures.current.has(id)) return;
    openGestures.current.delete(id);
    sendEdit(3, id);
  };
  const setParam = (id, value) => {
    if (!Number.isFinite(value)) return;
    begin(id);
    setValues(previous => ({ ...previous, [id]: value }));
    sendEdit(2, id, value);
  };

  useEffect(() => {
    const onMessage = event => {
      const message = parseHostMessage(event.data);
      if (!message) return;
      if (message.type === "preset") setPreset(message);
      else if (message.type === "base") {
        if (!openGestures.current.has(message.id)) setValues(previous => ({ ...previous, [message.id]: message.value }));
      } else if (message.type === "telemetry") setTelemetry(message);
    };
    const closeAll = () => { for (const id of [...openGestures.current]) end(id); };
    window.addEventListener("message", onMessage);
    window.addEventListener("pagehide", closeAll);
    requestSync();
    requestPreset(PRESET.REFRESH);
    const syncTimer = window.setInterval(requestSync, 50);
    const presetTimer = window.setInterval(() => requestPreset(PRESET.SNAPSHOT), 250);
    return () => {
      window.clearInterval(syncTimer);
      window.clearInterval(presetTimer);
      window.removeEventListener("message", onMessage);
      window.removeEventListener("pagehide", closeAll);
      closeAll();
    };
  }, []);

  const tags = useMemo(() => [...new Set(preset.entries.flatMap(entry => entry.tags))].sort((a, b) => a.localeCompare(b)), [preset.entries]);
  const filtered = useMemo(() => {
    const needle = query.trim().toLowerCase();
    return preset.entries.filter(entry => (!tag || entry.tags.includes(tag)) && (!needle || `${entry.name} ${entry.identity} ${entry.tags.join(" ")}`.toLowerCase().includes(needle)));
  }, [preset.entries, query, tag]);
  const factoryEntries = filtered.filter(entry => entry.kind === PRESET_KIND.FACTORY);
  const userEntries = filtered.filter(entry => entry.kind === PRESET_KIND.USER);
  const selection = preset.currentIdentity ? `${preset.currentKind}:${preset.currentIdentity}` : "";
  const loadSelection = value => {
    const split = value.indexOf(":");
    if (split <= 0) return;
    requestPreset(PRESET.LOAD, Number(value.slice(0, split)), value.slice(split + 1));
  };
  const savePreset = () => {
    if (!preset.userMutations) return;
    const displayName = presetName.trim() || "User Preset";
    const overwrite = preset.currentKind === PRESET_KIND.USER && preset.currentIdentity.length > 0;
    requestPreset(PRESET.SAVE_AS, PRESET_KIND.USER, overwrite ? preset.currentIdentity : slug(displayName), displayName, overwrite);
  };

  return (
    <main className="grid gap-4 p-4">
      <header className="flex items-center justify-between gap-5">
        <div><h1 className="m-0 text-lg font-semibold">PolySynth</h1><small className="text-neutral-400">CLAP WebView reference editor</small></div>
        <div className="flex items-center gap-3 text-xs">
          <span>Voices <strong>{telemetry.voices}</strong>/16</span>
          <span className="flex items-center gap-1">L <meter min="0" max="1" value={telemetry.left} /></span>
          <span className="flex items-center gap-1">R <meter min="0" max="1" value={telemetry.right} /></span>
        </div>
      </header>

      <section className="grid gap-2 rounded-lg border border-neutral-700 bg-neutral-900 p-3" aria-label="Preset browser">
        <div className="flex items-center gap-2"><strong className="min-w-0 flex-1 truncate">{preset.currentName || "Init"}{preset.dirty ? " *" : ""}</strong><button className="rounded border border-neutral-600 px-2 py-1" onClick={() => requestPreset(PRESET.PREVIOUS)}>◀</button><button className="rounded border border-neutral-600 px-2 py-1" onClick={() => requestPreset(PRESET.NEXT)}>▶</button></div>
        <div className="grid grid-cols-[2fr_1fr] gap-2"><input className="rounded border border-neutral-600 bg-neutral-950 p-1" type="search" maxLength={96} placeholder="Search presets or tags" value={query} onChange={event => setQuery(event.target.value)} /><select className="rounded border border-neutral-600 bg-neutral-950 p-1" value={tag} onChange={event => setTag(event.target.value)}><option value="">All tags</option>{tags.map(value => <option key={value} value={value}>{value}</option>)}</select></div>
        <select className="rounded border border-neutral-600 bg-neutral-950 p-1" value={selection} onChange={event => loadSelection(event.target.value)}>{!selection && <option value="">Select preset</option>}{factoryEntries.length > 0 && <optgroup label="Factory">{factoryEntries.map(entry => <option key={`f:${entry.identity}`} value={`${entry.kind}:${entry.identity}`}>{entry.name}</option>)}</optgroup>}{userEntries.length > 0 && <optgroup label="User">{userEntries.map(entry => <option key={`u:${entry.identity}`} value={`${entry.kind}:${entry.identity}`}>{entry.name}</option>)}</optgroup>}</select>
        <div className="flex items-center gap-2"><button className="rounded border border-neutral-600 px-2 py-1" onClick={() => requestPreset(PRESET.INIT)}>Init</button><input className="min-w-0 flex-1 rounded border border-neutral-600 bg-neutral-950 p-1" maxLength={96} placeholder="User preset name" value={presetName} onChange={event => setPresetName(event.target.value)} /><button className="rounded border border-neutral-600 px-2 py-1 disabled:opacity-40" disabled={!preset.userMutations} onClick={savePreset}>Save As</button><button className="rounded border border-neutral-600 px-2 py-1 disabled:opacity-40" disabled={!preset.userMutations || preset.currentKind !== PRESET_KIND.USER || !preset.currentIdentity} onClick={() => requestPreset(PRESET.DELETE, PRESET_KIND.USER, preset.currentIdentity)}>Delete</button></div>
      </section>

      <div className="grid grid-cols-3 gap-3 max-[680px]:grid-cols-1">
        {groups.map(group => <section key={group} className="rounded-lg border border-neutral-700 bg-neutral-900 p-3"><h2 className="mb-2 text-xs font-semibold uppercase tracking-wider text-neutral-400">{group}</h2>{params.filter(spec => spec.group === group).map(spec => <label key={spec.id} className="my-2 grid grid-cols-[1fr_auto] items-center gap-x-2 gap-y-1 text-sm"><span>{spec.label}</span><span className="font-mono text-xs text-neutral-300">{displayValue(spec, values[spec.id] ?? spec.initial)} {spec.mod && telemetry.modId === spec.id && Number.isFinite(telemetry.modAmount) ? <span className="text-sky-300">{telemetry.modAmount >= 0 ? "+" : ""}{telemetry.modAmount.toFixed(2)}</span> : null}</span>{spec.type === "select" ? <select className="col-span-2 w-full rounded border border-neutral-600 bg-neutral-950 p-1" value={values[spec.id] ?? spec.initial} onChange={event => { const value = Number(event.currentTarget.value); begin(spec.id); setParam(spec.id, value); end(spec.id); }}>{spec.options.map(([value, label]) => <option key={value} value={value}>{label}</option>)}</select> : <input className="col-span-2 w-full" type="range" min={spec.min} max={spec.max} step={spec.step} value={values[spec.id] ?? spec.initial} onPointerDown={() => begin(spec.id)} onInput={event => setParam(spec.id, Number(event.currentTarget.value))} onPointerUp={() => end(spec.id)} onPointerCancel={() => end(spec.id)} onKeyUp={() => end(spec.id)} onBlur={() => end(spec.id)} />}</label>)}</section>)}
        <section className="rounded-lg border border-neutral-700 bg-neutral-900 p-3"><h2 className="mb-2 text-xs font-semibold uppercase tracking-wider text-neutral-400">Voice Inspector</h2><div className="grid gap-2 text-sm"><div className="flex justify-between gap-3"><span>Last modulation</span><span className="font-mono text-xs">{telemetry.modId === 0xffffffff ? "—" : `${telemetry.modId}: ${telemetry.modAmount.toFixed(3)}`}</span></div><div className="flex justify-between gap-3"><span>Last expression</span><span className="font-mono text-xs">{telemetry.expressionId === 0xffffffff ? "—" : `${expressionNames[telemetry.expressionId] ?? telemetry.expressionId}: ${telemetry.expressionValue.toFixed(3)}`}</span></div><small className="text-neutral-400">Base parameter values stay separate from per-note modulation and note-expression telemetry.</small></div></section>
      </div>
    </main>
  );
}
