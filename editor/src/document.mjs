// The Stilletchaser document and every edit the editor makes to it.
// Edits mutate the document in place; the store snapshots it beforehand for undo.

export const MAX_STEPS = 1024;
export const DEFAULT_STEPS = 16;

let idCounter = 0;
export function newId(prefix) {
  idCounter += 1;
  return `${prefix}-${Date.now().toString(36)}-${idCounter.toString(36)}`;
}

function emptySteps(count) {
  return Array.from({ length: count }, () => []);
}

export function createSequence(name, stepCount = DEFAULT_STEPS) {
  return { id: newId('s'), name, steps: emptySteps(stepCount), factory: null };
}

export function createDocument(setup, sourcePath, name = 'Untitled') {
  return {
    format: 'stilletchaser',
    version: 1,
    name,
    source: { path: sourcePath, compWidth: setup.compWidth, compHeight: setup.compHeight },
    screens: setup.screens,
    slices: setup.slices,
    sequences: [createSequence('Sequence 1')],
    views: [{ id: 'all', name: 'All', hidden: [] }],
  };
}

export function validateDocument(doc) {
  if (!doc || doc.format !== 'stilletchaser') throw new Error('Not a Stilletchaser document');
  if (!Array.isArray(doc.slices) || !Array.isArray(doc.sequences) || !Array.isArray(doc.views)) {
    throw new Error('Damaged Stilletchaser document');
  }
  if (doc.sequences.length === 0) doc.sequences.push(createSequence('Sequence 1'));
  if (!doc.views.length || doc.views[0].id !== 'all') doc.views.unshift({ id: 'all', name: 'All', hidden: [] });
  if (doc.directions !== undefined && (typeof doc.directions !== 'object' || doc.directions === null || Array.isArray(doc.directions))) delete doc.directions;
  return doc;
}

/**
 * Brings the document in line with a new or changed Advanced Output.
 * Slices are matched by Arena's uniqueId, then by name (for a different file of the same stage).
 * Steps keep the slices that still exist; custom views keep new slices hidden.
 */
export function relink(doc, setup, sourcePath) {
  const byId = new Map(doc.slices.map((s) => [s.id, s]));
  const byName = new Map();
  for (const s of doc.slices) if (s.name && !byName.has(s.name)) byName.set(s.name, s);

  const rename = new Map();
  for (const slice of setup.slices) {
    if (byId.has(slice.id)) continue;
    const old = byName.get(slice.name);
    if (old && !setup.slices.some((s) => s.id === old.id)) rename.set(old.id, slice.id);
  }
  const alive = new Set(setup.slices.map((s) => s.id));
  const mapId = (id) => rename.get(id) ?? id;

  for (const sequence of doc.sequences) {
    sequence.steps = sequence.steps.map((step) => [...new Set(step.map(mapId))].filter((id) => alive.has(id)));
  }
  for (const view of doc.views) {
    if (view.id === 'all') view.hidden = view.hidden.map(mapId).filter((id) => alive.has(id));
    else view.visible = view.visible.map(mapId).filter((id) => alive.has(id));
  }
  if (doc.directions) {
    const kept = {};
    for (const [id, direction] of Object.entries(doc.directions)) if (alive.has(mapId(id))) kept[mapId(id)] = direction;
    doc.directions = kept;
  }

  doc.source = { path: sourcePath, compWidth: setup.compWidth, compHeight: setup.compHeight };
  doc.screens = setup.screens;
  doc.slices = setup.slices;
  return { renamed: rename.size, removed: [...byId.keys()].filter((id) => !alive.has(mapId(id))).length };
}

// ---- Sequences ----

export function addSequence(doc, afterIndex = doc.sequences.length - 1) {
  const stepCount = doc.sequences[afterIndex]?.steps.length ?? DEFAULT_STEPS;
  const sequence = createSequence(numberedName(doc.sequences, 'Sequence'), stepCount);
  doc.sequences.splice(afterIndex + 1, 0, sequence);
  return afterIndex + 1;
}

export function duplicateSequence(doc, index, toIndex = index + 1) {
  const copy = structuredClone(doc.sequences[index]);
  copy.id = newId('s');
  copy.name = uniqueName(doc.sequences, `${doc.sequences[index].name} copy`);
  doc.sequences.splice(toIndex, 0, copy);
  return toIndex;
}

export function removeSequence(doc, index) {
  doc.sequences.splice(index, 1);
  if (doc.sequences.length === 0) doc.sequences.push(createSequence('Sequence 1'));
  return Math.min(index, doc.sequences.length - 1);
}

export function moveItem(list, from, to) {
  const [item] = list.splice(from, 1);
  list.splice(to, 0, item);
  return to;
}

// "Sequence 1", "Sequence 2", ...: the lowest number not in use.
export function numberedName(list, base) {
  const names = new Set(list.map((item) => item.name));
  let n = 1;
  while (names.has(`${base} ${n}`)) n += 1;
  return `${base} ${n}`;
}

export function uniqueName(list, base) {
  const names = new Set(list.map((item) => item.name));
  if (!names.has(base)) return base;
  let n = 2;
  while (names.has(`${base} ${n}`)) n += 1;
  return `${base} ${n}`;
}

export function setStepCount(sequence, count) {
  const clamped = Math.max(1, Math.min(MAX_STEPS, Math.round(count)));
  while (sequence.steps.length < clamped) sequence.steps.push([]);
  sequence.steps.length = clamped;
  return clamped;
}

// ---- Steps ----

export function setSliceInStep(sequence, stepIndex, sliceId, on) {
  const step = sequence.steps[stepIndex];
  const has = step.includes(sliceId);
  if (on && !has) step.push(sliceId);
  if (!on && has) step.splice(step.indexOf(sliceId), 1);
}

export function toggleSliceInStep(sequence, stepIndex, sliceId) {
  setSliceInStep(sequence, stepIndex, sliceId, !sequence.steps[stepIndex].includes(sliceId));
}

export function copyStep(sequence, from, to) {
  sequence.steps[to] = [...sequence.steps[from]];
}

// Copies a step to every `interval`-th step after it ("drag and hold").
export function multiCopyStep(sequence, from, interval) {
  if (interval <= 0) return;
  for (let i = from + interval; i < sequence.steps.length; i += interval) sequence.steps[i] = [...sequence.steps[from]];
}

// The steps up to and including the last one with anything in it.
function filledLength(sequence) {
  for (let i = sequence.steps.length - 1; i >= 0; i -= 1) if (sequence.steps[i].length) return i + 1;
  return 0;
}

// "->": loops the filled part of the sequence over the whole length.
export function repeatFill(sequence) {
  const length = filledLength(sequence);
  if (length === 0) return;
  for (let i = length; i < sequence.steps.length; i += 1) sequence.steps[i] = [...sequence.steps[i % length]];
}

// "<->": bounces back and forth over the filled part (1 2 3 4 3 2 1 2 ...).
export function pingPongFill(sequence) {
  const length = filledLength(sequence);
  if (length === 0) return;
  if (length === 1) return repeatFill(sequence);
  const period = 2 * length - 2;
  for (let i = length; i < sequence.steps.length; i += 1) {
    const pos = i % period;
    sequence.steps[i] = [...sequence.steps[pos < length ? pos : period - pos]];
  }
}

export function clearSequence(sequence) {
  sequence.steps = emptySteps(sequence.steps.length);
  sequence.factory = null;
}

export function clearStep(sequence, stepIndex) {
  sequence.steps[stepIndex] = [];
}

export function invertStep(sequence, stepIndex, sliceIds) {
  const current = new Set(sequence.steps[stepIndex]);
  sequence.steps[stepIndex] = sliceIds.filter((id) => !current.has(id));
}

// ---- Views ----

export function viewSliceIds(doc, view) {
  if (view.id === 'all') {
    const hidden = new Set(view.hidden);
    return doc.slices.filter((s) => !hidden.has(s.id)).map((s) => s.id);
  }
  const visible = new Set(view.visible);
  return doc.slices.filter((s) => visible.has(s.id)).map((s) => s.id);
}

export function isVisibleInView(view, sliceId) {
  return view.id === 'all' ? !view.hidden.includes(sliceId) : view.visible.includes(sliceId);
}

export function setVisibleInView(view, sliceId, visible) {
  if (view.id === 'all') {
    const hidden = new Set(view.hidden);
    if (visible) hidden.delete(sliceId);
    else hidden.add(sliceId);
    view.hidden = [...hidden];
  } else {
    const shown = new Set(view.visible);
    if (visible) shown.add(sliceId);
    else shown.delete(sliceId);
    view.visible = [...shown];
  }
}

export function addView(doc, allOff = false) {
  const view = { id: newId('v'), name: numberedName(doc.views, 'View'), visible: allOff ? [] : doc.slices.map((s) => s.id) };
  doc.views.push(view);
  return doc.views.length - 1;
}

export function duplicateView(doc, index, toIndex = index + 1) {
  const source = doc.views[index];
  const copy = { id: newId('v'), name: uniqueName(doc.views, `${source.name} copy`), visible: viewSliceIds(doc, source) };
  doc.views.splice(Math.max(1, toIndex), 0, copy);
  return Math.max(1, toIndex);
}

export function removeView(doc, index) {
  if (index === 0) return 0;
  doc.views.splice(index, 1);
  return Math.min(index, doc.views.length - 1);
}

// Whether a sequence lights slices the view does not show (flagged in the sequence list).
export function sequenceLeavesView(doc, sequence, view) {
  const allowed = new Set(viewSliceIds(doc, view));
  return sequence.steps.some((step) => step.some((id) => !allowed.has(id)));
}

// ---- Directions ----
// Each slice can say which way wipes run through it and which way animated outlines run around it.
// Kept apart from the slices (which come from Arena and are replaced on every relink), by slice id;
// a slice that is not listed, or a value that is not listed here, is Auto.

export const WIPE_DIRECTIONS = ['auto', 'up', 'right', 'down', 'left', 'mirror-v', 'mirror-h'];
export const OUTLINE_DIRECTIONS = ['auto', 'cw', 'ccw', 'mirrored'];

export function sliceDirection(doc, sliceId) {
  const set = doc.directions?.[sliceId] ?? {};
  return {
    wipe: WIPE_DIRECTIONS.includes(set.wipe) ? set.wipe : 'auto',
    outline: OUTLINE_DIRECTIONS.includes(set.outline) ? set.outline : 'auto',
  };
}

/** Sets `kind` ('wipe' or 'outline') of every slice in `sliceIds`; Auto is stored as nothing. */
export function setSliceDirection(doc, sliceIds, kind, value) {
  const allowed = kind === 'wipe' ? WIPE_DIRECTIONS : kind === 'outline' ? OUTLINE_DIRECTIONS : null;
  if (!allowed || !allowed.includes(value)) throw new Error(`Unknown ${kind} direction: ${value}`);
  const directions = { ...(doc.directions ?? {}) };
  for (const id of sliceIds) {
    const next = { ...(directions[id] ?? {}) };
    if (value === 'auto') delete next[kind];
    else next[kind] = value;
    if (Object.keys(next).length) directions[id] = next;
    else delete directions[id];
  }
  if (Object.keys(directions).length) doc.directions = directions;
  else delete doc.directions;
}

// What Auto means for a slice, worked out like the plugins do: a slice taller than wide (on screen, in
// composition pixels) wipes up; a wider one wipes away from the centre of the stage. Outlines run clockwise.
export function autoWipe(doc, slice) {
  const p = slice.points;
  const edge = (a, b) => Math.hypot(b[0] - a[0], b[1] - a[1]);
  let width, height, centreX;
  if (p.length === 4) {
    width = edge(p[0], p[1]);
    height = edge(p[0], p[3]);
    centreX = (p[1][0] + p[3][0]) / 2; // the middle of the frame the plugins use
  } else {
    const xs = p.map((q) => q[0]);
    const ys = p.map((q) => q[1]);
    width = Math.max(...xs) - Math.min(...xs);
    height = Math.max(...ys) - Math.min(...ys);
    centreX = (Math.max(...xs) + Math.min(...xs)) / 2;
  }
  centreX /= doc.source?.compWidth || 1;
  if (height > width) return 'up';
  return centreX < 0.5 ? 'left' : 'right';
}
