// The stage canvas: draws the slices, toggles them in the selected step, pans and zooms.
// With the Factory open it shows the step each slice lights in, and a click moves a slice there.
import { bounds, boxCoverage, centroid, pointInPolygon } from './geometry.mjs';
import { isVisibleInView, setVisibleInView, toggleSliceInStep, setSliceInStep } from './document.mjs';
import { firstSteps, isPatternFactory } from './factory.mjs';
import { screenColour, stageLayout } from './layout.mjs';

import { THEME } from './theme.mjs';

// Read when a canvas is made, after the host's palette is in place.
const colors = () => ({
  slice: THEME.sliceOff,
  hover: THEME.sliceHover,
  on: THEME.accent,
  outline: THEME.outline,
  label: THEME.textDim,
  labelOn: THEME.onAccent,
  band: THEME.accentWash,
  bandLine: THEME.accent,
  badge: 'rgba(0, 0, 0, 0.72)',
  frame: THEME.frame ?? 'rgba(255, 255, 255, 0.025)',
  frameLine: THEME.frameLine ?? 'rgba(255, 255, 255, 0.16)',
  title: THEME.text,
  badgeText: '#ffffff',
});
const MIN_ZOOM = 0.02;
const MAX_ZOOM = 20;

export function createCanvas(canvas, store, { onFactoryPick, onShowScreen }) {
  const COLORS = colors();
  const ctx = canvas.getContext('2d');
  const view = { zoom: 1, panX: 0, panY: 0, fitted: false };
  let hoverId = null;
  let drag = null;
  let spaceDown = false;
  let frameRequested = false;

  const doc = () => store.state.doc;
  const ui = () => store.state.ui;
  // The Factory panel is open on a sequence it made: numbers on the slices, clicks move them.
  const factoryOpen = () => ui().panel === 'factory' && isPatternFactory(store.currentSequence.factory);

  // What is shown and where (stageLayout), and every slice's bounds and area there: worked out again only when
  // the document, the view or the choice of screens changes (the mouse asks on every move).
  let cache = null;
  let lastShown = null; // the last choice of screens and window shape drawn
  // The window's shape, roughly (the rows of screens follow it, not every pixel of a resize): read when the canvas
  // is resized, not on every question about the slices.
  let aspect = 16 / 9;
  function measureAspect() {
    const rect = canvas.getBoundingClientRect();
    if (rect.width > 0 && rect.height > 0) aspect = Math.round((rect.width / rect.height) * 4) / 4;
  }
  // Edits mostly light slices in steps: the layout is checked again only then, and worked out again only when the
  // slices, the view's slices or the choice of screens changed.
  let dirty = true;
  store.subscribe((what) => {
    if (what === 'doc' || what === 'all') dirty = true;
  });
  const viewKey = (v) => `${v.id}|${(v.hidden ?? v.visible ?? []).join(',')}`;
  function shownLayout() {
    const v = store.currentView;
    if (cache && !dirty && cache.view === v && cache.aspect === aspect) return cache;
    const key = viewKey(v);
    dirty = false;
    if (cache && cache.slices === doc().slices && cache.view === v && cache.viewKey === key && cache.show === doc().show && cache.aspect === aspect) return cache;
    const layout = stageLayout(doc(), doc().show, aspect);
    // Big slices first, so smaller ones are drawn over them (a slice of the whole picture, such as a stream's,
    // would otherwise hide the slices under it).
    const items = layout.items
      .filter((item) => isVisibleInView(v, item.slice.id))
      .map((item) => {
        const b = bounds(item.points);
        return { ...item, bounds: b, area: (b.maxX - b.minX) * (b.maxY - b.minY), centre: centroid(item.points) };
      })
      .sort((a, b) => b.area - a.area);
    // A name under a smaller slice drawn over it would print over that one's: left out (worked out here, once).
    for (const item of items) {
      const [cx, cy] = item.centre;
      item.labelHidden = items.some(
        (other) =>
          other !== item &&
          other.area < item.area &&
          cx >= other.bounds.minX && cx <= other.bounds.maxX && cy >= other.bounds.minY && cy <= other.bounds.maxY &&
          pointInPolygon(cx, cy, other.points),
      );
    }
    // Another choice of screens (from the picker, or undone), or rows rearranged for the window: fitted again.
    const shownNow = `${layout.show}|${layout.frames.length ? aspect : ''}`;
    if (lastShown !== null && lastShown !== shownNow) view.fitted = false;
    lastShown = shownNow;
    cache = { slices: doc().slices, view: v, viewKey: key, show: doc().show, aspect, frames: layout.frames, items, mode: layout.show };
    return cache;
  }
  const visibleItems = () => shownLayout().items;
  const frames = () => shownLayout().frames;

  function displayedStep() {
    const sequence = store.currentSequence;
    const index = ui().playStep ?? ui().stepIndex;
    return new Set(sequence.steps[index] ?? []);
  }

  const toWorld = (x, y) => [(x - view.panX) / view.zoom, (y - view.panY) / view.zoom];
  const toScreen = (x, y) => [x * view.zoom + view.panX, y * view.zoom + view.panY];

  function itemAt(x, y) {
    const [wx, wy] = toWorld(x, y);
    // The smallest slice under the mouse: it is the one drawn on top, and a big one around it stays reachable
    // by its own free part.
    let best = null;
    for (const item of visibleItems()) {
      const b = item.bounds;
      if (wx < b.minX || wx > b.maxX || wy < b.minY || wy > b.maxY || !pointInPolygon(wx, wy, item.points)) continue;
      if (!best || item.area < best.area) best = item;
    }
    return best;
  }
  const sliceAt = (x, y) => itemAt(x, y)?.slice ?? null;

  // The frame whose title row (above it) is at a point of the canvas, or -1.
  function titleAt(x, y) {
    return frames().findIndex((f) => {
      const [x0, y0] = toScreen(f.x, f.y);
      const [x1] = toScreen(f.x + f.width, f.y);
      return x >= x0 && x <= x1 && y >= y0 - 24 && y < y0;
    });
  }
  function frameAt(x, y) {
    const [wx, wy] = toWorld(x, y);
    return frames().findIndex((f) => wx >= f.x && wx <= f.x + f.width && wy >= f.y && wy <= f.y + f.height);
  }

  function fit() {
    const items = visibleItems();
    const rect = canvas.getBoundingClientRect();
    if ((!items.length && !frames().length) || rect.width === 0) return;
    const b = bounds([
      ...items.flatMap((i) => [[i.bounds.minX, i.bounds.minY], [i.bounds.maxX, i.bounds.maxY]]),
      ...frames().flatMap((f) => [[f.x, f.y], [f.x + f.width, f.y + f.height]]),
    ]);
    const margin = 40;
    const zoom = Math.min((rect.width - margin * 2) / (b.maxX - b.minX || 1), (rect.height - margin * 2) / (b.maxY - b.minY || 1));
    view.zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, zoom));
    view.panX = rect.width / 2 - ((b.minX + b.maxX) / 2) * view.zoom;
    view.panY = rect.height / 2 - ((b.minY + b.maxY) / 2) * view.zoom + (frames().length ? 10 : 0); // room for the titles
    view.fitted = true;
    redraw();
    store.setUi({});
  }

  function zoomAt(factor, x, y) {
    const [wx, wy] = toWorld(x, y);
    view.zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, view.zoom * factor));
    view.panX = x - wx * view.zoom;
    view.panY = y - wy * view.zoom;
    redraw();
    store.setUi({});
  }

  function setZoomPercent(percent) {
    const rect = canvas.getBoundingClientRect();
    zoomAt(Math.max(MIN_ZOOM, percent / 100) / view.zoom, rect.width / 2, rect.height / 2);
  }

  function redraw() {
    if (frameRequested) return;
    frameRequested = true;
    requestAnimationFrame(draw);
  }

  function draw() {
    frameRequested = false;
    const rect = canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    if (canvas.width !== Math.round(rect.width * dpr) || canvas.height !== Math.round(rect.height * dpr)) {
      canvas.width = Math.round(rect.width * dpr);
      canvas.height = Math.round(rect.height * dpr);
    }
    shownLayout(); // first: another choice of screens asks for a new fit
    if (!view.fitted && rect.width > 0) {
      fit();
      return;
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, rect.width, rect.height);

    const lit = displayedStep();
    ctx.save();
    ctx.translate(view.panX, view.panY);
    ctx.scale(view.zoom, view.zoom);
    // The screens: a dark frame each (their titles are drawn over, at a size that does not change with the zoom).
    for (const f of frames()) {
      ctx.fillStyle = COLORS.frame;
      ctx.fillRect(f.x, f.y, f.width, f.height);
      ctx.lineWidth = 1 / view.zoom;
      ctx.strokeStyle = COLORS.frameLine;
      ctx.strokeRect(f.x, f.y, f.width, f.height);
    }
    for (const { slice, points } of visibleItems()) {
      const on = lit.has(slice.id);
      ctx.beginPath();
      points.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
      ctx.closePath();
      ctx.fillStyle = on ? COLORS.on : slice.id === hoverId ? COLORS.hover : COLORS.slice;
      ctx.fill();
      // Dashed outlines, like the input rectangles in Arena's Advanced Output.
      ctx.lineWidth = 1 / view.zoom;
      ctx.setLineDash(on ? [] : [4 / view.zoom, 3 / view.zoom]);
      ctx.strokeStyle = on ? COLORS.on : COLORS.outline;
      ctx.stroke();
    }
    ctx.setLineDash([]);
    ctx.restore();

    drawTitles();
    drawLabels(lit);
    if (factoryOpen()) drawStepNumbers();
    if (drag?.type === 'band') {
      const x = Math.min(drag.x0, drag.x1);
      const y = Math.min(drag.y0, drag.y1);
      ctx.fillStyle = COLORS.band;
      ctx.strokeStyle = COLORS.bandLine;
      ctx.lineWidth = 1;
      ctx.fillRect(x, y, Math.abs(drag.x1 - drag.x0), Math.abs(drag.y1 - drag.y0));
      ctx.strokeRect(x + 0.5, y + 0.5, Math.abs(drag.x1 - drag.x0), Math.abs(drag.y1 - drag.y0));
    }
    canvas.style.cursor = cursorFor();
  }

  // Each screen's number (in its colour), name and size, above its frame.
  function drawTitles() {
    ctx.textBaseline = 'middle';
    for (const f of frames()) {
      const [x0, y0] = toScreen(f.x, f.y);
      const [x1] = toScreen(f.x + f.width, f.y);
      const y = y0 - 12;
      let x = x0;
      if (f.index >= 0) {
        const n = String(f.index + 1);
        ctx.font = '700 11px Segoe UI, sans-serif';
        const w = Math.max(18, ctx.measureText(n).width + 8);
        ctx.fillStyle = screenColour(f.index);
        ctx.fillRect(x, y - 9, w, 18);
        ctx.fillStyle = COLORS.labelOn;
        ctx.textAlign = 'center';
        ctx.fillText(n, x + w / 2, y + 0.5);
        x += w + 8;
      }
      ctx.textAlign = 'left';
      ctx.font = '600 12px Segoe UI, sans-serif';
      ctx.fillStyle = COLORS.title;
      const size = `${f.width} \u00d7 ${f.height}`;
      ctx.font = '11px Segoe UI, sans-serif';
      const sizeWidth = ctx.measureText(size).width;
      ctx.font = '600 12px Segoe UI, sans-serif';
      const room = x1 - x - sizeWidth - 12;
      if (room > 20) ctx.fillText(f.name, x, y, room);
      if (x1 - x0 > sizeWidth + 60) {
        ctx.font = '11px Segoe UI, sans-serif';
        ctx.fillStyle = COLORS.label;
        ctx.textAlign = 'right';
        ctx.fillText(size, x1, y);
      }
    }
  }

  function drawLabels(lit) {
    ctx.font = '11px Segoe UI, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    for (const item of visibleItems()) {
      const { slice, bounds: b } = item;
      const width = (b.maxX - b.minX) * view.zoom;
      const height = (b.maxY - b.minY) * view.zoom;
      if (width < 50 || height < 18 || !slice.name) continue;
      if (item.labelHidden) continue;
      const [cx, cy] = toScreen(...item.centre);
      ctx.fillStyle = lit.has(slice.id) ? COLORS.labelOn : COLORS.label;
      ctx.fillText(slice.name, cx, cy, width - 6);
    }
  }

  // The step each slice first lights in, on a dark disc at its centre.
  function drawStepNumbers() {
    const first = firstSteps(store.currentSequence.steps);
    ctx.font = '600 11px Segoe UI, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    for (const { slice, centre } of visibleItems()) {
      if (!first.has(slice.id)) continue;
      const text = String(first.get(slice.id) + 1);
      const [x, y] = toScreen(...centre);
      const r = Math.max(9, ctx.measureText(text).width / 2 + 5);
      ctx.fillStyle = COLORS.badge;
      ctx.beginPath();
      ctx.arc(x, y + (slice.name ? 14 : 0), r, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = COLORS.badgeText;
      ctx.fillText(text, x, y + (slice.name ? 14 : 0) + 0.5);
    }
  }

  function cursorFor() {
    if (drag?.type === 'pan') return 'grabbing';
    if (spaceDown || ui().tool === 'hand') return 'grab';
    return hoverId ? 'pointer' : 'crosshair';
  }

  // ---- Pointer handling ----

  canvas.addEventListener('pointerdown', (event) => {
    canvas.setPointerCapture(event.pointerId);
    const x = event.offsetX;
    const y = event.offsetY;
    if (event.button === 1 || spaceDown || ui().tool === 'hand') {
      drag = { type: 'pan', x, y, panX: view.panX, panY: view.panY };
      redraw();
      return;
    }
    if (event.button !== 0) return;

    const slice = sliceAt(x, y);
    if (slice && factoryOpen() && !event.shiftKey && !event.ctrlKey && !event.altKey) {
      onFactoryPick(slice.id);
      return;
    }
    if (slice && !event.shiftKey && !event.ctrlKey && !event.altKey) {
      const key = `snake-${Date.now()}`;
      drag = { type: 'snake', visited: new Set([slice.id]), key };
      store.edit((d) => toggleSliceInStep(d.sequences[ui().sequenceIndex], ui().stepIndex, slice.id), { coalesce: key });
      return;
    }
    drag = { type: 'band', x0: x, y0: y, x1: x, y1: y, frame: frameAt(x, y) };
  });

  // A double-click on a screen's title shows that screen alone.
  canvas.addEventListener('dblclick', (event) => {
    const f = titleAt(event.offsetX, event.offsetY);
    if (f >= 0 && frames()[f].index >= 0) onShowScreen?.(doc().screens[frames()[f].index].id);
  });

  canvas.addEventListener('pointermove', (event) => {
    const x = event.offsetX;
    const y = event.offsetY;
    if (!drag) {
      const nextHover = sliceAt(x, y)?.id ?? null;
      if (nextHover !== hoverId) {
        hoverId = nextHover;
        redraw();
      }
      return;
    }
    switch (drag.type) {
      case 'pan':
        view.panX = drag.panX + (x - drag.x);
        view.panY = drag.panY + (y - drag.y);
        redraw();
        break;
      case 'snake': {
        const slice = sliceAt(x, y);
        if (slice && !drag.visited.has(slice.id)) {
          drag.visited.add(slice.id);
          store.edit((d) => toggleSliceInStep(d.sequences[ui().sequenceIndex], ui().stepIndex, slice.id), { coalesce: drag.key });
        }
        break;
      }
      case 'band':
        drag.x1 = x;
        drag.y1 = y;
        redraw();
        break;
    }
  });

  canvas.addEventListener('pointerup', (event) => {
    if (drag?.type === 'band') applyBand(event);
    if (drag?.type === 'snake') store.endCoalesce();
    drag = null;
    redraw();
  });

  function applyBand(event) {
    const [ax, ay] = toWorld(Math.min(drag.x0, drag.x1), Math.min(drag.y0, drag.y1));
    const [bx, by] = toWorld(Math.max(drag.x0, drag.x1), Math.max(drag.y0, drag.y1));
    const box = { minX: ax, minY: ay, maxX: bx, maxY: by };
    // Slices mostly inside the band: a big one around it (the whole picture) is not taken along.
    // Started inside a screen: only that screen's slices.
    const ids = visibleItems()
      .filter((item) => (drag.frame < 0 || item.frame === drag.frame) && boxCoverage(box, item.bounds) >= 0.5)
      .map((item) => item.slice.id);
    if (!ids.length) return;
    const { sequenceIndex, stepIndex, viewIndex } = ui();
    store.edit((d) => {
      const sequence = d.sequences[sequenceIndex];
      for (const id of ids) {
        if (event.altKey) setVisibleInView(d.views[viewIndex], id, false);
        else if (event.shiftKey) setSliceInStep(sequence, stepIndex, id, true);
        else if (event.ctrlKey) setSliceInStep(sequence, stepIndex, id, false);
        else toggleSliceInStep(sequence, stepIndex, id);
      }
    });
  }

  canvas.addEventListener('pointerleave', () => {
    if (!drag && hoverId) {
      hoverId = null;
      redraw();
    }
  });

  canvas.addEventListener('wheel', (event) => {
    event.preventDefault();
    zoomAt(Math.exp(-event.deltaY * 0.0015), event.offsetX, event.offsetY);
  }, { passive: false });

  window.addEventListener('keydown', (event) => {
    if (event.target.closest?.('input, select, textarea')) return;
    if (event.code === 'Space' && !spaceDown) {
      spaceDown = true;
      redraw();
      event.preventDefault();
    }
    if (event.ctrlKey && (event.key === '+' || event.key === '=')) zoomAt(1.25, canvas.clientWidth / 2, canvas.clientHeight / 2);
    if (event.ctrlKey && event.key === '-') zoomAt(0.8, canvas.clientWidth / 2, canvas.clientHeight / 2);
  });
  window.addEventListener('keyup', (event) => {
    if (event.code === 'Space') {
      spaceDown = false;
      redraw();
    }
  });
  new ResizeObserver(() => {
    measureAspect();
    redraw();
  }).observe(canvas);

  return {
    redraw,
    fit,
    setZoomPercent,
    get zoomPercent() {
      return Math.round(view.zoom * 100);
    },
    refit() {
      view.fitted = false;
      redraw();
    },
  };
}
