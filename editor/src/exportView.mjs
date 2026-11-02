// Export mode: a live preview of the input or output map plus its settings.
import { HOST_NAME } from './theme.mjs';
import { FONTS, POSITIONS, defaultExportOptions, renderInputMap, renderOutputMap, overlappingOutputs } from './exporter.mjs';
import { escapeHtml } from './listView.mjs';

export function exportOptions(prefs) {
  return { ...defaultExportOptions(), ...(prefs.exportOptions ?? {}) };
}

export function renderMap(doc, options) {
  return options.kind === 'output' ? renderOutputMap(doc, options) : renderInputMap(doc, options);
}

export function createExportView(root, store) {
  root.innerHTML = `
    <canvas class="preview"></canvas>
    <div class="export-note" hidden></div>
    <div class="export-settings"></div>`;
  const preview = root.querySelector('.preview');
  const note = root.querySelector('.export-note');
  const settings = root.querySelector('.export-settings');

  const setOption = (patch) => store.savePrefs({ exportOptions: { ...exportOptions(store.state.prefs), ...patch } });

  function renderSettings(options) {
    if (settings.contains(document.activeElement) && document.activeElement.tagName === 'INPUT' && document.activeElement.type !== 'checkbox') return;
    const check = (key, label) =>
      `<label class="setting"><span>${label}</span><input type="checkbox" data-key="${key}" ${options[key] ? 'checked' : ''} /></label>`;
    settings.innerHTML = `
      <h4>Project</h4>
      <label class="setting"><span>Font</span><select data-key="font">${FONTS.map((f) => `<option ${f === options.font ? 'selected' : ''}>${f}</option>`).join('')}</select></label>
      <label class="setting"><span>Font Size</span><input class="num" type="number" min="8" max="400" step="1" data-key="fontSize" value="${options.fontSize}" /></label>
      <label class="setting"><span>Position</span><select data-key="position">${POSITIONS.map((p) => `<option ${p === options.position ? 'selected' : ''}>${p}</option>`).join('')}</select></label>
      ${check('showScreenName', 'Show Screen Name')}
      ${check('showResolution', 'Show Resolution')}
      ${check('showProjectInfo', 'Show Project Info')}
      <label class="setting"><span>Project Info</span><input type="text" data-key="projectInfo" value="${escapeHtml(options.projectInfo)}" /></label>
      ${check('showAspectRatio', 'Show Aspect Ratio')}
      ${check('showBorder', 'Show Border')}
      ${check('transparent', 'Transparent Background')}
      <button class="btn wide" data-act="reset">Reset</button>`;
  }

  settings.addEventListener('change', (event) => {
    const key = event.target.dataset.key;
    if (!key) return;
    if (event.target.type === 'checkbox') setOption({ [key]: event.target.checked });
    else if (key === 'fontSize') setOption({ fontSize: Math.max(8, Math.min(400, parseFloat(event.target.value) || 45)) });
    else setOption({ [key]: event.target.value });
  });
  settings.addEventListener('input', (event) => {
    if (event.target.dataset.key === 'projectInfo') setOption({ projectInfo: event.target.value });
  });
  settings.addEventListener('click', (event) => {
    if (event.target.closest('[data-act="reset"]')) store.savePrefs({ exportOptions: { ...defaultExportOptions(), kind: exportOptions(store.state.prefs).kind } });
  });

  function drawPreview(map, transparent) {
    const rect = preview.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    preview.width = Math.round(rect.width * dpr);
    preview.height = Math.round(rect.height * dpr);
    const ctx = preview.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, rect.width, rect.height);
    const margin = 30;
    const scale = Math.min((rect.width - margin * 2) / map.width, (rect.height - margin * 2) / map.height);
    if (!(scale > 0)) return;
    const w = map.width * scale;
    const h = map.height * scale;
    const x = (rect.width - w) / 2;
    const y = (rect.height - h) / 2;
    if (transparent) {
      // Checkerboard so transparency is visible.
      const cell = 12;
      for (let cy = 0; cy < h; cy += cell) {
        for (let cx = 0; cx < w; cx += cell) {
          ctx.fillStyle = (cx / cell + cy / cell) % 2 ? '#3d3d3d' : '#2a2a2a';
          ctx.fillRect(x + cx, y + cy, Math.min(cell, w - cx), Math.min(cell, h - cy));
        }
      }
    }
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(map, x, y, w, h);
  }

  const view = {
    render() {
      if (root.hidden) return;
      const options = exportOptions(store.state.prefs);
      renderSettings(options);
      const map = renderMap(store.state.doc, options);
      drawPreview(map, options.transparent);
      const overlaps = options.kind === 'output' ? overlappingOutputs(store.state.doc) : 0;
      note.hidden = overlaps === 0;
      note.textContent = `${overlaps} slices are sent to exactly the same place on the output, so they cover each other here. Set their Output Transformation in ${HOST_NAME === 'STLT' ? "STLT's" : "Arena's"} Advanced Output to see them apart.`;
    },
  };
  new ResizeObserver(() => view.render()).observe(preview);
  return view;
}
