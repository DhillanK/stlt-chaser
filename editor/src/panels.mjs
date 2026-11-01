// The side panels opened from the left menu.
import * as D from './document.mjs';
import { DIRECTIONS, defaultFactorySettings, maxBlock } from './factory.mjs';
import { addEnvelope, duplicateEnvelope, removeEnvelope } from './envelopes.mjs';
import { renderList, escapeHtml } from './listView.mjs';
import { HOST_NAME } from './theme.mjs';

export function createPanels(root, store, actions) {
  const ui = () => store.state.ui;
  const doc = () => store.state.doc;
  let lastEyeIndex = null;

  function render() {
    const panel = ui().panel;
    root.hidden = !panel;
    if (!panel) return;
    // Keep keyboard focus (e.g. a rename in progress) by not rebuilding under it.
    if (root.contains(document.activeElement) && document.activeElement.tagName === 'INPUT') return;
    if (root.dataset.panel !== panel) {
      root.innerHTML = '';
      root.onclick = null;
      root.dataset.panel = panel;
    }
    ({ file: renderFile, views: renderViews, sequences: renderSequences, factory: renderFactory, envelopes: renderEnvelopes, export: renderExport })[panel]();
  }

  // ---- File ----

  // A plain command menu, like the File menu of a desktop app.
  let chooseVersion = false;
  let pluginStatus = null;
  let pluginMessage = '';

  async function refreshPluginStatus() {
    pluginStatus = await window.host.pluginStatus();
    if (ui().panel === 'file') renderFile();
  }

  // Installing the STLT plugins into Resolume's Extra Effects (desktop app only).
  function pluginSection() {
    if (!window.host.pluginStatus) return '';
    if (!pluginStatus) return '<h3>Resolume plugins</h3><div class="hint">Checking…</div>';
    const { available, targets } = pluginStatus;
    const lines = targets.length
      ? targets
          .map(
            (t) => `<div class="row"><label>${escapeHtml(t.product)}</label><span>${t.upToDate ? 'up to date' : 'not installed'}</span>
              <button class="btn" data-folder="${escapeHtml(t.folder)}" title="${escapeHtml(t.folder)}">Show</button></div>`,
          )
          .join('')
      : '<div class="hint">No Resolume Arena or Avenue folder found in Documents.</div>';
    return `<h3>Resolume plugins</h3>${lines}
      <button class="btn wide" data-act="install-plugins" ${available.length && targets.length ? '' : 'disabled'}>Install / update plugins</button>
      <div class="hint">${escapeHtml(pluginMessage || 'Copies STLT-CHASER, STLT-WIPER and STLT-OUTLINER into Extra Effects. Close Resolume first.')}</div>`;
  }
  // Opened from STLT-VJ: the document always follows STLT-VJ's slices, and the effects come with STLT-VJ.
  function renderStltVjFile() {
    const { prefs } = store.state;
    root.innerHTML = `
      <div class="menu-list">
        <button class="menu-item" data-act="new" title="A new document on STLT-VJ's slices">New</button>
        <button class="menu-item" data-act="load">Load</button>
        <button class="menu-item" data-act="save-as">Save As…</button>
        <button class="menu-item" data-act="relink" title="Read STLT-VJ's slices again">Relink</button>
      </div>
      <div class="hint">${escapeHtml(prefs.lastFile ?? 'Not saved as a file yet. Everything is kept in the working copy the effects read.')}</div>
      <div class="hint">Linked to STLT-VJ's Advanced Output: change the slices there and they update here.</div>
      <h3>Effects</h3>
      <div class="hint">STLT-CHASER, STLT-WIPER and STLT-OUTLINER come with STLT-VJ: drag them from its Effects panel onto the composition, a layer or a clip, and pick the Sequence there.</div>`;
    root.onclick = (event) => {
      const act = event.target.closest('button')?.dataset.act;
      if (act === 'new') actions.newFromXml(null);
      else if (act === 'load') actions.load();
      else if (act === 'save-as') actions.saveAs();
      else if (act === 'relink') actions.relinkTo(store.state.paths.stltvj);
    };
  }

  function renderFile() {
    const { paths, prefs } = store.state;
    if (paths.stltvj) return renderStltVjFile();
    const installed = paths.arenaOutputs.filter((o) => o.exists);
    const versions = chooseVersion
      ? installed.map((o) => `<button class="menu-item sub" data-new="${escapeHtml(o.path)}">${escapeHtml(o.label)}</button>`).join('') +
        '<button class="menu-item sub" data-act="new-file">Other XML file…</button>'
      : '';
    root.innerHTML = `
      <div class="menu-list">
        <button class="menu-item" data-act="new">New</button>
        ${versions}
        <button class="menu-item" data-act="load">Load</button>
        <button class="menu-item" data-act="save-as">Save As…</button>
        <button class="menu-item" data-act="relink" title="Read the linked Advanced Output again; Shift-click to pick another file">Relink</button>
      </div>
      <div class="hint">${escapeHtml(prefs.lastFile ?? 'Not saved as a file yet. Everything is kept in the working copy the plugins read.')}</div>
      <div class="hint">Linked to: ${escapeHtml(doc().source.path || 'nothing')}</div>
      ${pluginSection()}`;
    if (window.host.pluginStatus && !pluginStatus) refreshPluginStatus();
    root.onclick = async (event) => {
      const target = event.target.closest('button');
      if (!target) return;
      const act = target.dataset.act;
      if (act === 'install-plugins') {
        const result = await window.host.installPlugins();
        pluginMessage = [...result.installed.map((p) => `Installed for ${p}. Restart it to load them.`), ...result.errors].join(' ');
        await refreshPluginStatus();
        return;
      }
      if (target.dataset.folder) return window.host.showFolder(target.dataset.folder);
      if (target.dataset.new) {
        chooseVersion = false;
        actions.newFromXml(target.dataset.new);
      } else if (act === 'new') {
        // Ask which Resolume only when there is a choice to make.
        if (installed.length === 1 && !chooseVersion) actions.newFromXml(installed[0].path);
        else {
          chooseVersion = !chooseVersion;
          render();
        }
      } else if (act === 'new-file') {
        chooseVersion = false;
        actions.newFromXml(null);
      } else if (act === 'load') actions.load();
      else if (act === 'save-as') actions.saveAs();
      else if (act === 'relink') actions.relinkTo(event.shiftKey ? null : doc().source.path || null);
    };
  }

  // ---- Views ----

  function renderViews() {
    if (!root.querySelector('.views-list')) root.innerHTML = '<h3>Views</h3><div class="views-list"></div><h3>Slices</h3><ul class="slices"></ul>';
    renderList(root.querySelector('.views-list'), {
      items: doc().views,
      selected: ui().viewIndex,
      fixedFirst: true,
      addTitle: 'Add a view (Alt-click: start with every slice hidden)',
      onSelect: (i) => store.setUi({ viewIndex: i }),
      onRename: (i, name) => store.edit((d) => (d.views[i].name = name)),
      onRemove: (i) => {
        let next = i;
        store.edit((d) => (next = D.removeView(d, i)));
        store.setUi({ viewIndex: next });
      },
      onAdd: (altKey) => {
        let index = 0;
        store.edit((d) => (index = D.addView(d, altKey)));
        store.setUi({ viewIndex: index });
      },
      onMove: (from, to, copy) => {
        let index = to;
        store.edit((d) => (index = copy ? D.duplicateView(d, from, to) : D.moveItem(d.views, from, Math.max(1, to))));
        store.setUi({ viewIndex: index });
      },
    });

    const view = store.currentView;
    const rows = [];
    const order = [];
    for (const screen of doc().screens) {
      rows.push(`<li class="screen">${escapeHtml(screen.name)}</li>`);
      for (const slice of doc().slices.filter((s) => s.screen === screen.id)) {
        const visible = D.isVisibleInView(view, slice.id);
        rows.push(`<li data-slice="${order.length}" class="${visible ? '' : 'hidden-item'}">
          <button class="eye" title="Show / hide (Shift-click: everything in between)">${visible ? '&#x25C9;' : '&#x25CB;'}</button>${escapeHtml(slice.name || slice.id)}</li>`);
        order.push(slice.id);
      }
    }
    const slicesEl = root.querySelector('.slices');
    slicesEl.innerHTML = rows.join('');
    slicesEl.onclick = (event) => {
      const li = event.target.closest('li[data-slice]');
      if (!li || !event.target.closest('.eye')) return;
      const index = Number(li.dataset.slice);
      const viewIndex = ui().viewIndex;
      const show = !D.isVisibleInView(view, order[index]);
      const range = event.shiftKey && lastEyeIndex !== null ? [Math.min(lastEyeIndex, index), Math.max(lastEyeIndex, index)] : [index, index];
      store.edit((d) => {
        for (let i = range[0]; i <= range[1]; i += 1) D.setVisibleInView(d.views[viewIndex], order[i], show);
      });
      lastEyeIndex = index;
    };
  }

  // ---- Sequences ----

  function renderSequences() {
    if (!root.querySelector('.seq-list')) root.innerHTML = '<h3>Sequences</h3><div class="seq-list"></div><div class="hint">Rename: double-click the name, ✎ or F2. Drag ☰ to reorder, Alt-drag to copy.</div>';
    const view = store.currentView;
    renderList(root.querySelector('.seq-list'), {
      items: doc().sequences.map((s) => ({
        name: s.name,
        flag: view.id !== 'all' && D.sequenceLeavesView(doc(), s, view) ? `Uses slices outside the view "${view.name}"` : null,
      })),
      selected: ui().sequenceIndex,
      addTitle: 'Add a sequence',
      onSelect: (i) => store.setUi({ sequenceIndex: i }),
      onRename: (i, name) => store.edit((d) => (d.sequences[i].name = name)),
      onRemove: (i) => {
        let next = i;
        store.edit((d) => (next = D.removeSequence(d, i)));
        store.setUi({ sequenceIndex: next });
      },
      onAdd: () => {
        let index = 0;
        store.edit((d) => (index = D.addSequence(d, ui().sequenceIndex)));
        store.setUi({ sequenceIndex: index });
      },
      onMove: (from, to, copy) => {
        let index = to;
        store.edit((d) => (index = copy ? D.duplicateSequence(d, from, to) : D.moveItem(d.sequences, from, to)));
        store.setUi({ sequenceIndex: index });
      },
    });
  }

  // ---- Factory ----

  function renderFactory() {
    const sequence = store.currentSequence;
    const settings = sequence.factory ?? defaultFactorySettings();
    const { compWidth, compHeight } = doc().source;
    const slices = actions.factorySlices();
    const blockMax = maxBlock(slices, settings, compWidth, compHeight);
    const toggle = (key, label) => `<button class="btn ${settings[key] ? 'on' : ''}" data-toggle="${key}">${label}</button>`;
    root.innerHTML = `
      <h3>Factory</h3>
      <div class="hint">${sequence.factory ? 'Settings of this sequence. Changing them regenerates its steps.' : 'Change any setting to generate this sequence. Its current steps are replaced.'}</div>
      <div class="row"><label>Direction</label><select data-field="direction">${DIRECTIONS.map((d) => `<option ${d === settings.direction ? 'selected' : ''}>${d}</option>`).join('')}</select></div>
      <div class="row">${toggle('flipX', 'Flip X')}${toggle('flipY', 'Flip Y')}</div>
      <div class="row"><label>Block</label><button class="btn" data-step="block:-1">&minus;</button><span class="num">${settings.block}</span><button class="btn" data-step="block:1">+</button></div>
      <div class="row"><label>Modulo</label><button class="btn" data-step="modulo:-1">&minus;</button><span class="num">${settings.modulo || 'OFF'}</span><button class="btn" data-step="modulo:1">+</button></div>
      <div class="row">${toggle('mirrorX', 'Mirror X')}${toggle('mirrorY', 'Mirror Y')}</div>
      <button class="btn wide" data-act="shuffle">Shuffle</button>
      <button class="btn wide" data-act="reset-grid">Reset Grid</button>
      <div class="hint">Grid: drag a line to move it, Alt-drag to add one, hover and press Delete to remove it. Uses the slices of the current view (${slices.length}).</div>`;

    const apply = (patch) => actions.applyFactory({ ...settings, ...patch });
    root.onclick = (event) => {
      const button = event.target.closest('button');
      if (!button) return;
      if (button.dataset.toggle) apply({ [button.dataset.toggle]: !settings[button.dataset.toggle] });
      if (button.dataset.step) {
        const [key, delta] = button.dataset.step.split(':');
        const max = key === 'block' ? blockMax : Math.max(0, Math.ceil(blockMax / settings.block));
        const min = key === 'block' ? 1 : 0;
        apply({ [key]: Math.max(min, Math.min(max, (settings[key] || 0) + Number(delta))) });
      }
      if (button.dataset.act === 'shuffle') apply({ shuffle: Math.floor(Math.random() * 1e9) + 1 });
      if (button.dataset.act === 'reset-grid') apply({ grid: null });
    };
    root.querySelector('[data-field="direction"]').onchange = (event) => apply({ direction: event.target.value });
  }

  // ---- Envelopes ----

  function renderEnvelopes() {
    if (!root.querySelector('.env-list')) {
      root.innerHTML = `<h3>Envelopes</h3><div class="env-list"></div><div class="hint">Shared by every project. Pick one in the effect's Envelope menu in ${HOST_NAME}.</div>`;
    }
    renderList(root.querySelector('.env-list'), {
      items: store.state.library.envelopes,
      selected: ui().envelopeIndex,
      addTitle: 'Add an envelope',
      onSelect: (i) => store.setUi({ envelopeIndex: i }),
      onRename: (i, name) => store.edit((_d, lib) => (lib.envelopes[i].name = name)),
      onRemove: (i) => {
        let next = i;
        store.edit((_d, lib) => (next = removeEnvelope(lib, i)));
        store.setUi({ envelopeIndex: next });
      },
      onAdd: () => {
        let index = 0;
        store.edit((_d, lib) => (index = addEnvelope(lib, ui().envelopeIndex)));
        store.setUi({ envelopeIndex: index });
      },
      onMove: (from, to, copy) => {
        let index = to;
        store.edit((_d, lib) => (index = copy ? duplicateEnvelope(lib, from, to) : D.moveItem(lib.envelopes, from, to)));
        store.setUi({ envelopeIndex: index });
      },
    });
  }

  // ---- Export ----

  function renderExport() {
    const { prefs, paths } = store.state;
    const kind = prefs.exportOptions?.kind ?? 'input';
    const folder = prefs.exportFolder ?? paths.documents;
    root.innerHTML = `
      <div class="menu-list">
        <select class="menu-item" data-field="kind">
          <option value="input" ${kind === 'input' ? 'selected' : ''}>Input</option>
          <option value="output" ${kind === 'output' ? 'selected' : ''}>Output</option>
        </select>
        <div class="menu-item path" title="${escapeHtml(folder)}">${escapeHtml(folder)}</div>
        <button class="menu-item" data-act="folder">Set Folder</button>
        <button class="menu-item" data-act="save">Save</button>
      </div>
      <div class="hint">${kind === 'input' ? 'The composition: where each slice takes its content from. Use it as a template for content.' : "Each screen's output: where " + (HOST_NAME === 'STLT-VJ' ? 'STLT-VJ' : 'Arena') + ' sends each slice.'}</div>`;
    root.onclick = (event) => {
      const act = event.target.closest('button')?.dataset.act;
      if (act === 'folder') actions.setExportFolder();
      if (act === 'save') actions.saveExport();
    };
    root.querySelector('[data-field="kind"]').onchange = (event) =>
      store.savePrefs({ exportOptions: { ...(prefs.exportOptions ?? {}), kind: event.target.value } });
  }

  return { render };
}
