import { TELEMETRY_KIND } from "./bridge.js";

export const emptyModulation = Object.freeze({ dropped: 0, current: {}, activeNotes: {}, last: null });

export function noteKey(event) {
  return `${event.noteId}|${event.port}|${event.channel}|${event.key}`;
}

export function modulationKey(event) {
  return `${event.paramId}|${event.noteId}|${event.port}|${event.channel}|${event.key}`;
}

export function isGlobalModulation(event) {
  return event.noteId < 0 && event.port < 0 && event.channel < 0 && event.key < 0;
}

export function noteMatchesModulation(note, modulation) {
  return (modulation.noteId < 0 || modulation.noteId === note.noteId) &&
    (modulation.port < 0 || modulation.port === note.port) &&
    (modulation.channel < 0 || modulation.channel === note.channel) &&
    (modulation.key < 0 || modulation.key === note.key);
}

function validNoteLifecycle(event) {
  return event.noteId >= -1 && event.port === 0 &&
    event.channel >= 0 && event.channel <= 15 &&
    event.key >= 0 && event.key <= 127;
}

export function applyModulationTelemetry(previous, message) {
  const overflowed = message.dropped !== previous.dropped;
  if (overflowed) {
    // Drop-newest means the records delivered with the first changed drop count
    // necessarily predate an unknown lost event. Replaying those records could
    // resurrect a voice/modulation that the missing event ended. Discard the
    // whole batch and rebuild only from records observed after this invalidation.
    return { dropped: message.dropped, activeNotes: {}, current: {}, last: previous.last };
  }

  let activeNotes = { ...previous.activeNotes };
  let current = { ...previous.current };
  let last = previous.last;

  for (const item of message.events) {
    if (item.kind === TELEMETRY_KIND.RESET) {
      // RESET is ordered in the same SPSC stream as later note/modulation
      // records. Clear everything observed before it, but keep processing the
      // remainder of this batch so post-reset state is not silently lost.
      activeNotes = {};
      current = {};
      last = null;
      continue;
    }
    if (item.kind === TELEMETRY_KIND.NOTE_ON) {
      if (!validNoteLifecycle(item)) continue;
      const key = noteKey(item);
      const previousGeneration = activeNotes[key];
      const generationCount = item.noteId < 0
        ? (previousGeneration?.generationCount ?? 0) + 1
        : 1;
      activeNotes[key] = { ...item, generationCount };
      continue;
    }
    if (item.kind === TELEMETRY_KIND.NOTE_END) {
      if (!validNoteLifecycle(item)) continue;
      const key = noteKey(item);
      const existing = activeNotes[key];
      if (existing?.generationCount > 1) {
        activeNotes[key] = { ...existing, generationCount: existing.generationCount - 1 };
      } else {
        delete activeNotes[key];
      }
      const remainingNotes = Object.values(activeNotes);
      for (const [targetKey, target] of Object.entries(current)) {
        if (!isGlobalModulation(target) &&
            !remainingNotes.some(note => noteMatchesModulation(note, target))) {
          delete current[targetKey];
        }
      }
      continue;
    }
    if (item.kind === TELEMETRY_KIND.MODULATION) {
      last = item;
      const key = modulationKey(item);
      if (item.amount === 0) {
        delete current[key];
      } else if (isGlobalModulation(item) ||
                 Object.values(activeNotes).some(note => noteMatchesModulation(note, item))) {
        current[key] = item;
      }
    }
  }

  return { dropped: message.dropped, activeNotes, current, last };
}

export function formatAddress(event) {
  if (!event) return "—";
  if (isGlobalModulation(event)) return "global";
  const part = (label, value) => `${label}${value < 0 ? "*" : value}`;
  return [part("note ", event.noteId), part("port ", event.port), part("ch ", event.channel), part("key ", event.key)].join(" · ");
}

export function formatAmount(value) {
  return `${value >= 0 ? "+" : ""}${value.toFixed(3)}`;
}
