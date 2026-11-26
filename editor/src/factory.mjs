// The Factory: builds a sequence from where the slices are. A pattern (left to right, centre
// out...) gives every slice a place along it; slices at about the same place light together.
import { bounds, centroid } from './geometry.mjs';

export const PATTERNS = [
  { id: 'left-right', name: 'Left → Right' },
  { id: 'right-left', name: 'Right → Left' },
  { id: 'top-bottom', name: 'Top → Bottom' },
  { id: 'bottom-top', name: 'Bottom → Top' },
  { id: 'center-out', name: 'Center → Out' },
  { id: 'out-center', name: 'Out → Center' },
  { id: 'sides-center', name: 'Sides → Center' },
  { id: 'diagonal', name: 'Diagonal ↘' },
  { id: 'clockwise', name: 'Clockwise' },
  { id: 'random', name: 'Random' },
];

export const patternName = (id) => PATTERNS.find((p) => p.id === id)?.name ?? id;

// steps 0: one step per group of aligned slices. fill 'step': each step lights its own slices;
// 'build': the slices stay on, so the last step lights them all. alternate: 'odd-even' plays the odd
// steps then the even ones, 'there-back' plays them forwards then backwards. moves: slices put in
// another step by hand ({ sliceId: stepIndex }).
export function defaultFactorySettings() {
  return { pattern: 'left-right', steps: 0, fill: 'step', alternate: 'none', block: 1, modulo: 0, shuffle: 0, moves: {} };
}

// A sequence made before the patterns (with the grid) has `direction` and no `pattern`.
export const isPatternFactory = (factory) => Boolean(factory?.pattern);

function median(values) {
  const sorted = [...values].sort((a, b) => a - b);
  return sorted.length ? sorted[Math.floor(sorted.length / 2)] : 0;
}

// Deterministic shuffle so the same settings always give the same sequence.
function seededShuffle(items, seed) {
  const result = [...items];
  let state = (seed * 2654435761) >>> 0 || 1;
  const next = () => {
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    return (state >>> 0) / 4294967296;
  };
  for (let i = result.length - 1; i > 0; i -= 1) {
    const j = Math.floor(next() * (i + 1));
    [result[i], result[j]] = [result[j], result[i]];
  }
  return result;
}

// Where each slice is along the pattern, and how far apart two places must be to count as
// different steps (half a typical slice, so hand-placed stages that are not pixel aligned still
// line up).
function places(slices, pattern) {
  const all = bounds(slices.flatMap((s) => s.points));
  const cx = (all.minX + all.maxX) / 2;
  const cy = (all.minY + all.maxY) / 2;
  const centres = slices.map((s) => centroid(s.points));
  const extents = slices.map((s) => bounds(s.points));
  const width = median(extents.map((b) => b.maxX - b.minX));
  const height = median(extents.map((b) => b.maxY - b.minY));
  const size = (width + height) / 2;
  const byPattern = {
    'left-right': { key: ([x]) => x, gap: width / 2 },
    'right-left': { key: ([x]) => -x, gap: width / 2 },
    'top-bottom': { key: ([, y]) => y, gap: height / 2 },
    'bottom-top': { key: ([, y]) => -y, gap: height / 2 },
    'center-out': { key: ([x, y]) => Math.hypot(x - cx, y - cy), gap: size / 2 },
    'out-center': { key: ([x, y]) => -Math.hypot(x - cx, y - cy), gap: size / 2 },
    'sides-center': { key: ([x]) => -Math.abs(x - cx), gap: width / 2 },
    diagonal: { key: ([x, y]) => x + y, gap: size / 2 },
    // From twelve o'clock, clockwise (y grows downwards); the gap is half a slice seen from the middle.
    clockwise: {
      key: ([x, y]) => (Math.atan2(x - cx, cy - y) + 2 * Math.PI) % (2 * Math.PI),
      gap: size / 2 / Math.max(1, median(centres.map(([x, y]) => Math.hypot(x - cx, y - cy)))),
    },
  };
  const { key, gap } = byPattern[pattern] ?? byPattern['left-right'];
  return { keys: centres.map(key), gap: Math.max(gap, 1e-6) };
}

// Groups of slice ids, in the order they light.
function groups(slices, settings) {
  const count = Math.max(0, Math.round(settings.steps || 0));
  if (settings.pattern === 'random') {
    const order = seededShuffle(slices.map((s) => s.id), 1 + (settings.shuffle || 0));
    if (!count) return order.map((id) => [id]);
    const result = Array.from({ length: Math.min(count, order.length) }, () => []);
    order.forEach((id, i) => result[i % result.length].push(id));
    return result;
  }
  const { keys, gap } = places(slices, settings.pattern);
  const order = slices.map((s, i) => ({ id: s.id, key: keys[i] })).sort((a, b) => a.key - b.key);
  if (count) {
    // A fixed number of steps: equal stretches of the pattern, so a gap on stage is a pause.
    const min = order[0].key;
    const span = order[order.length - 1].key - min || 1;
    const result = Array.from({ length: count }, () => []);
    for (const { id, key } of order) result[Math.min(count - 1, Math.floor(((key - min) / span) * count))].push(id);
    return result;
  }
  // A new step where the pattern jumps by more than half a slice, or once a step has grown a whole
  // slice wide (otherwise evenly spaced slices, as around a centre, would chain into one step).
  const result = [[order[0].id]];
  let start = order[0].key;
  for (let i = 1; i < order.length; i += 1) {
    if (order[i].key - order[i - 1].key > gap || order[i].key - start > 2 * gap) {
      result.push([]);
      start = order[i].key;
    }
    result[result.length - 1].push(order[i].id);
  }
  return result;
}

/** @returns {{steps: string[][]}} */
export function generate(slices, settings) {
  if (!slices.length) return { steps: [[]] };
  const s = { ...defaultFactorySettings(), ...settings };
  let steps = groups(slices, s);

  if (s.alternate === 'odd-even') steps = [...steps.filter((_, i) => i % 2 === 0), ...steps.filter((_, i) => i % 2 === 1)];
  if (s.alternate === 'there-back' && steps.length > 2) steps = [...steps, ...steps.slice(1, -1).reverse()];

  const block = Math.max(1, Math.min(s.block || 1, steps.length));
  if (block > 1) {
    const merged = [];
    for (let i = 0; i < steps.length; i += block) merged.push(steps.slice(i, i + block).flat());
    steps = merged;
  }
  if (s.modulo > 0 && s.modulo < steps.length) {
    const folded = Array.from({ length: s.modulo }, () => []);
    steps.forEach((step, i) => folded[i % s.modulo].push(...step));
    steps = folded;
  }
  if (s.shuffle && s.pattern !== 'random') steps = seededShuffle(steps, s.shuffle);

  // Slices moved by hand leave every step and light from the chosen one.
  const known = new Set(slices.map((sl) => sl.id));
  const moves = Object.entries(s.moves || {}).filter(([id]) => known.has(id));
  if (moves.length) {
    const moved = new Set(moves.map(([id]) => id));
    steps = steps.map((step) => step.filter((id) => !moved.has(id)));
    for (const [id, index] of moves) {
      const at = Math.max(0, Math.round(index));
      while (steps.length <= at) steps.push([]);
      steps[at].push(id);
    }
  }

  if (s.fill === 'build') {
    const lit = [];
    steps = steps.map((step) => {
      lit.push(...step.filter((id) => !lit.includes(id)));
      return [...lit];
    });
  }
  // Each step lists a slice once, in slice order.
  const order = new Map(slices.map((sl, i) => [sl.id, i]));
  steps = steps.map((step) => [...new Set(step)].sort((a, b) => order.get(a) - order.get(b)));
  return { steps: steps.length ? steps : [[]] };
}

// The first step each slice lights in (for the numbers drawn on the stage).
export function firstSteps(steps) {
  const first = new Map();
  steps.forEach((step, i) => step.forEach((id) => first.has(id) || first.set(id, i)));
  return first;
}

// The longest Block that still leaves more than one step.
export function maxBlock(slices, settings) {
  return Math.max(1, generate(slices, { ...settings, block: 1, modulo: 0, shuffle: 0, moves: {} }).steps.length);
}
