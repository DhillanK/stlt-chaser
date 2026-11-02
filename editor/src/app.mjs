// Wires the editor together: loads the working copy, keeps it linked to Arena's
// Advanced Output (or, opened from STLT, to STLT's slices), and connects the canvas, sequencer and panels.
import { parseAdvancedOutput } from './advancedOutput.mjs';
import { isStltVjStage, parseStltVjStage } from './stltvjStage.mjs';
import { useHost } from './theme.mjs';
import * as D from './document.mjs';
import { autoGrid, defaultFactorySettings, generate } from './factory.mjs';
import { createEnvelopeLibrary } from './envelopes.mjs';
import { createStore } from './store.mjs';
import { createCanvas } from './canvas.mjs';
import { createSequencer } from './sequencer.mjs';
import { createPanels } from './panels.mjs';
import { createEnvelopeEditor } from './envelopeEditor.mjs';
import { canvasToBase64 } from './exporter.mjs';
import { createExportView, exportOptions, renderMap } from './exportView.mjs';

const host = window.host;
const $ = (selector) => document.querySelector(selector);

async function readJson(file) {
  const text = await host.readText(file);
  if (text === null) return null;
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

async function readSetup(setupPath) {
  const text = await host.readText(setupPath);
  if (text === null) throw new Error(`Cannot find ${setupPath}`);
  return isStltVjStage(setupPath) ? parseStltVjStage(text) : parseAdvancedOutput(text);
}

function emptyDocument() {
  return D.createDocument({ compWidth: 1920, compHeight: 1080, screens: [], slices: [] }, '');
}

async function boot() {
  const paths = await host.paths();
  useHost(Boolean(paths.stltvj));
  const prefs = { bpm: 120, rate: 0, lastFile: null, ...(await readJson(paths.prefsFile)) };
  const library = (await readJson(paths.envelopesFile)) ?? createEnvelopeLibrary();
  let message = null;

  let doc = null;
  try {
    const saved = await readJson(paths.currentFile);
    if (saved) doc = D.validateDocument(saved);
  } catch (error) {
    message = `Working copy unreadable, started a new one (${error.message})`;
  }
  if (paths.stltvj) {
    // Opened from STLT: always on its slices; sequences made before keep the slices that still exist.
    try {
      const setup = await readSetup(paths.stltvj);
      if (doc) D.relink(doc, setup, paths.stltvj);
      else doc = D.createDocument(setup, paths.stltvj);
    } catch (error) {
      doc ??= emptyDocument();
      message = `${error.message}. Open the editor from STLT (Output > Chaser).`;
    }
  } else if (!doc) {
    const arena = paths.arenaOutputs.find((o) => o.exists);
    doc = arena ? D.createDocument(await readSetup(arena.path), arena.path) : emptyDocument();
    if (!arena) message = 'No Advanced Output found: use File > New to pick one.';
  } else if (doc.source.path) {
    // Catch up with anything changed in Arena while the editor was closed.
    try {
      D.relink(doc, await readSetup(doc.source.path), doc.source.path);
    } catch (error) {
      message = `${error.message}. Use File > Relink.`;
    }
  }

  const store = createStore({ doc, library, prefs, paths });
  if (paths.version) $('#menu .version').textContent = `v${paths.version}`;
  store.state.ui.message = message;

  // ---- Actions used by the panels ----

  const factorySlices = () => {
    const allowed = new Set(D.viewSliceIds(store.state.doc, store.currentView));
    return store.state.doc.slices.filter((s) => allowed.has(s.id));
  };

  function applyFactory(settings, coalesce = null) {
    const { compWidth, compHeight } = store.state.doc.source;
    const result = generate(factorySlices(), settings, compWidth, compHeight);
    const index = store.state.ui.sequenceIndex;
    store.edit((d) => {
      d.sequences[index].steps = result.steps.length ? result.steps : [[]];
      d.sequences[index].factory = { ...settings, grid: result.grid };
    }, { coalesce });
  }

  async function watchSource() {
    if (watchedSource) await host.unwatch(watchedSource);
    watchedSource = store.state.doc.source.path || null;
    if (watchedSource) await host.watch(watchedSource);
  }
  let watchedSource = null;

  const actions = {
    factorySlices,
    applyFactory,
    async newFromXml(xmlPath) {
      const file = xmlPath ?? paths.stltvj ?? (await host.openDialog({ title: 'Advanced Output XML', filters: [{ name: 'Advanced Output', extensions: ['xml'] }] }));
      if (!file) return;
      try {
        store.replaceDocument(D.createDocument(await readSetup(file), file));
        await store.savePrefs({ lastFile: null });
        await watchSource();
        canvas.refit();
        say(`New document from ${file}`);
      } catch (error) {
        say(error.message, true);
      }
    },
    async load() {
      const file = await host.openDialog({ title: 'Load Stilletchaser document', defaultPath: paths.documents, filters: [{ name: 'Stilletchaser', extensions: ['stchase'] }] });
      if (!file) return;
      try {
        const loaded = D.validateDocument(JSON.parse(await host.readText(file)));
        // In STLT a document made for Resolume moves onto STLT's slices (matched by name).
        if (paths.stltvj) D.relink(loaded, await readSetup(paths.stltvj), paths.stltvj);
        store.replaceDocument(loaded);
        await store.savePrefs({ lastFile: file });
        await watchSource();
        canvas.refit();
        say(`Loaded ${file}`);
      } catch (error) {
        say(`Could not load: ${error.message}`, true);
      }
    },
    async saveAs() {
      const file = await host.saveDialog({ title: 'Save Stilletchaser document', defaultPath: `${paths.documents}\\${store.state.doc.name}.stchase`, filters: [{ name: 'Stilletchaser', extensions: ['stchase'] }] });
      if (!file) return;
      const name = file.split(/[\\/]/).pop().replace(/\.stchase$/i, '');
      store.edit((d) => (d.name = name));
      await store.savePrefs({ lastFile: file });
      await store.saveNow();
      say(`Saved as ${file}`);
    },
    async relinkTo(xmlPath) {
      const file = xmlPath ?? (await host.openDialog({ title: 'Relink to Advanced Output XML', filters: [{ name: 'Advanced Output', extensions: ['xml'] }] }));
      if (!file) return;
      try {
        const setup = await readSetup(file);
        let result = null;
        store.edit((d) => (result = D.relink(d, setup, file)));
        await watchSource();
        canvas.refit();
        say(`Relinked: ${setup.slices.length} slices${result.removed ? `, ${result.removed} no longer exist` : ''}`);
      } catch (error) {
        say(error.message, true);
      }
    },
    async setExportFolder() {
      const folder = await host.openDialog({ title: 'Folder for exported maps', defaultPath: store.state.prefs.exportFolder ?? paths.documents, properties: ['openDirectory', 'createDirectory'] });
      if (folder) await store.savePrefs({ exportFolder: folder });
    },
    async saveExport() {
      const options = exportOptions(store.state.prefs);
      const folder = store.state.prefs.exportFolder ?? paths.documents;
      const source = paths.stltvj ? store.state.doc.name : (store.state.doc.source.path || store.state.doc.name).split(/[\\/]/).pop().replace(/\.xml$/i, '');
      const file = `${folder}\\${source} ${options.kind === 'output' ? 'Output' : 'Input'} Map.png`;
      try {
        await host.writeBinary(file, canvasToBase64(renderMap(store.state.doc, options)));
        say(`Saved ${file}`);
      } catch (error) {
        say(`Could not save the map: ${error.message}`, true);
      }
    },
  };

  function say(text, isError = false) {
    store.setUi({ message: text, messageIsError: isError });
  }

  // ---- Components ----

  const canvas = createCanvas($('#canvas'), store, {
    onFactoryGridChange: (grid, key) => applyFactory({ ...(store.currentSequence.factory ?? defaultFactorySettings()), grid }, key),
  });
  const sequencer = createSequencer($('#sequencer'), store);
  const panels = createPanels($('#panel'), store, actions);
  const envelopeEditor = createEnvelopeEditor($('#envelope-editor'), store);
  const exportView = createExportView($('#export-view'), store);

  // ---- Top bar and menu ----

  const topbar = $('#topbar');
  topbar.querySelector('#redo-button').insertAdjacentHTML(
    'afterend',
    `<div class="divider"></div>
     <button id="invert-button" class="tool" title="Turn the lit slices of the selected step off and the others on (I)">&#x25D1;&#xFE0E; Invert step</button>
     <button id="clear-step-button" class="tool" title="Turn every slice of the selected step off (Delete)">&#x2298;&#xFE0E; Clear step</button>`,
  );

  $('#zoom-input').addEventListener('change', (event) => {
    const value = parseFloat(event.target.value);
    if (value > 0) canvas.setZoomPercent(value);
  });
  $('#fit-button').onclick = () => canvas.fit();
  $('#hand-button').onclick = () => store.setUi({ tool: store.state.ui.tool === 'hand' ? 'select' : 'hand' });
  $('#undo-button').onclick = () => store.undo();
  $('#redo-button').onclick = () => store.redo();

  const invertStep = () => {
    const { sequenceIndex, stepIndex } = store.state.ui;
    const ids = D.viewSliceIds(store.state.doc, store.currentView);
    store.edit((d) => D.invertStep(d.sequences[sequenceIndex], stepIndex, ids));
  };
  const clearStep = () => {
    const { sequenceIndex, stepIndex } = store.state.ui;
    store.edit((d) => D.clearStep(d.sequences[sequenceIndex], stepIndex));
  };
  $('#invert-button').onclick = invertStep;
  $('#clear-step-button').onclick = clearStep;

  $('#menu').addEventListener('click', (event) => {
    const panel = event.target.closest('[data-panel]')?.dataset.panel;
    if (!panel) return;
    const next = store.state.ui.panel === panel ? null : panel;
    store.setUi({
      panel: next,
      mode: next === 'envelopes' || next === 'export' ? next : 'canvas',
      factoryGrid: next === 'factory' ? autoGrid(factorySlices(), store.state.doc.source.compWidth, store.state.doc.source.compHeight) : null,
    });
    // Opening or closing a panel changes the stage width.
    requestAnimationFrame(() => canvas.fit());
  });

  window.addEventListener('keydown', (event) => {
    if (event.target.closest?.('input, select, textarea')) return;
    const key = event.key.toLowerCase();
    if (event.ctrlKey && key === 'z' && !event.shiftKey) store.undo();
    else if (event.ctrlKey && (key === 'y' || (key === 'z' && event.shiftKey))) store.redo();
    else if (store.state.ui.mode === 'canvas' && !event.ctrlKey && key === 'i') invertStep();
    else if (store.state.ui.mode === 'canvas' && store.state.ui.panel !== 'factory' && key === 'delete') clearStep();
    else return;
    event.preventDefault();
  });

  // ---- Follow Arena (or STLT) ----

  host.onFileChanged(async (file) => {
    if (file !== store.state.doc.source.path) return;
    try {
      const setup = await readSetup(file);
      store.edit((d) => D.relink(d, setup, file));
      say(`Advanced Output updated: ${setup.slices.length} slices`);
    } catch (error) {
      say(error.message, true);
    }
  });
  await watchSource();

  // ---- Render ----

  function render() {
    const { ui, doc: currentDoc, prefs: currentPrefs } = store.state;
    for (const button of document.querySelectorAll('#menu [data-panel]')) button.classList.toggle('active', button.dataset.panel === ui.panel);
    $('#doc-name').textContent = currentDoc.name + (currentPrefs.lastFile ? '' : ' (working copy)');
    const saveEl = $('#save-state');
    saveEl.textContent = { saving: '\u{1F4BE} saving…', saved: 'saved', error: 'NOT SAVED' }[ui.saveState];
    saveEl.classList.toggle('error', ui.saveState === 'error');
    const messageEl = $('#message');
    messageEl.textContent = ui.message ?? '';
    messageEl.title = ui.message ?? '';
    messageEl.classList.toggle('error', Boolean(ui.messageIsError));
    $('#hand-button').classList.toggle('active', ui.tool === 'hand');
    const zoomInput = $('#zoom-input');
    if (document.activeElement !== zoomInput) zoomInput.value = canvas.zoomPercent;
    $('#envelope-editor').hidden = ui.mode !== 'envelopes';
    $('#export-view').hidden = ui.mode !== 'export';
    $('#canvas').hidden = ui.mode !== 'canvas';
    $('#sequencer').hidden = ui.mode === 'export';

    sequencer.render();
    panels.render();
    if (ui.mode === 'envelopes') envelopeEditor.render();
    else if (ui.mode === 'export') exportView.render();
    else canvas.redraw();
  }

  store.subscribe(render);
  render();
  // In the browser preview (dev/mock-host.js) expose the store for checking the UI by script.
  if (host._files) window.__store = store;
}

boot().catch((error) => {
  document.body.innerHTML = `<pre style="color:#ff6b5a;padding:20px">STLT-CHASER could not start:\n${error.stack}</pre>`;
});
