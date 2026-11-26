// What the stage canvas shows, and where: every screen side by side with its slices where they go on it, one
// screen, or the composition (where the slices take their picture from).
import { bounds } from './geometry.mjs';

export const SHOW_SCREENS = 'screens';
export const SHOW_COMPOSITION = 'composition';

// The screens' colours in STLT's Advanced Output, so that a screen's number looks the same in both.
const SCREEN_COLOURS = ['#f5b84a', '#e682af', '#78cd8c', '#b496f0', '#64cdd2', '#eb785f'];
export const screenColour = (index) => SCREEN_COLOURS[index % SCREEN_COLOURS.length];

const hasOutput = (slice) => Array.isArray(slice.outPoints) && slice.outPoints.length >= 3;

// Whether the document knows where its slices go (screens, and slices with output points).
export function canShowScreens(doc) {
  return (doc.screens?.length ?? 0) > 0 && doc.slices.some(hasOutput);
}

// The view in use: what the document asks for, when it still can be shown.
export function resolveShow(doc, show = doc.show) {
  if (!canShowScreens(doc)) return SHOW_COMPOSITION;
  if (show === SHOW_COMPOSITION) return SHOW_COMPOSITION;
  if (show && show !== SHOW_SCREENS && doc.screens.some((s) => s.id === show)) return show;
  return SHOW_SCREENS;
}

// The slices of each screen (the screen's index -1: those on no screen, or without output points).
export function slicesByScreen(doc) {
  const byScreen = new Map(doc.screens?.map((s, i) => [s.id, i]) ?? []);
  const groups = new Map();
  for (const slice of doc.slices) {
    const index = hasOutput(slice) && byScreen.has(slice.screen) ? byScreen.get(slice.screen) : -1;
    if (!groups.has(index)) groups.set(index, []);
    groups.get(index).push(slice);
  }
  return groups;
}

function screenSize(screen, slices) {
  if (screen.width > 0 && screen.height > 0) return [screen.width, screen.height];
  const b = slices.length ? bounds(slices.flatMap((s) => s.outPoints)) : { maxX: 1, maxY: 1 };
  return [Math.max(1, Math.ceil(b.maxX)), Math.max(1, Math.ceil(b.maxY))];
}

/**
 * @returns {{frames: Array<{index:number, name:string, x:number, y:number, width:number, height:number}>,
 *            items: Array<{slice:object, points:number[][], frame:number}>}}
 * `frame` is the index into `frames` (-1 in the composition view). Frames are laid out in rows, in the
 * screens' order, at their pixel size, so that big and small screens keep their proportions; `aspect` is the
 * width / height of the window they are shown in.
 */
export function stageLayout(doc, show = doc.show, aspect = 16 / 9) {
  const shown = resolveShow(doc, show);
  if (shown === SHOW_COMPOSITION) return { show: shown, frames: [], items: doc.slices.map((slice) => ({ slice, points: slice.points, frame: -1 })) };

  const groups = slicesByScreen(doc);
  const boxes = [];
  doc.screens.forEach((screen, index) => {
    if (shown !== SHOW_SCREENS && screen.id !== shown) return;
    const slices = groups.get(index) ?? [];
    const [width, height] = screenSize(screen, slices);
    boxes.push({ index, name: screen.name, width, height, slices, out: true });
  });
  // Slices that go to no screen known here: in a frame of their own, where they take from.
  const loose = groups.get(-1) ?? [];
  if (shown === SHOW_SCREENS && loose.length) {
    const b = bounds(loose.flatMap((s) => s.points));
    boxes.push({ index: -1, name: 'On no screen', width: Math.max(1, Math.ceil(b.maxX)), height: Math.max(1, Math.ceil(b.maxY)), slices: loose, out: false });
  }

  // Rows of 1, 2, 3... screens: the one whose whole picture fits the window largest (`aspect`: its width / height).
  const gap = Math.max(60, 0.12 * Math.max(...boxes.map((b) => b.height), 1));
  const arrange = (perRow) => {
    const placed = [];
    let x = 0;
    let y = 0;
    let rowHeight = 0;
    let width = 0;
    boxes.forEach((box, i) => {
      if (i > 0 && i % perRow === 0) {
        x = 0;
        y += rowHeight + gap * 1.5; // room for the next row's titles
        rowHeight = 0;
      }
      placed.push({ x, y });
      width = Math.max(width, x + box.width);
      x += box.width + gap;
      rowHeight = Math.max(rowHeight, box.height);
    });
    return { placed, width, height: y + rowHeight + gap }; // the titles over the first row
  };
  let best = null;
  for (let perRow = 1; perRow <= Math.max(1, boxes.length); perRow += 1) {
    const candidate = arrange(perRow);
    const scale = Math.min(aspect / candidate.width, 1 / candidate.height);
    if (!best || scale > best.scale * 1.0001) best = { ...candidate, scale };
  }
  const frames = [];
  const items = [];
  boxes.forEach((box, i) => {
    const { x, y } = best.placed[i];
    const frame = frames.length;
    frames.push({ index: box.index, name: box.name, x, y, width: box.width, height: box.height });
    for (const slice of box.slices) {
      const source = box.out ? slice.outPoints : slice.points;
      items.push({ slice, points: source.map(([px, py]) => [px + x, py + y]), frame });
    }
  });
  return { show: shown, frames, items };
}
