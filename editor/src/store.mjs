// Application state, undo/redo and autosave.
// `doc` is the open Stilletchaser document, `library` the shared envelopes; `ui` is never saved.

const UNDO_LIMIT = 200;
const SAVE_DELAY_MS = 250;

export function createStore({ doc, library, prefs, paths }) {
  const state = {
    doc,
    library,
    prefs,
    paths,
    ui: {
      sequenceIndex: 0,
      stepIndex: 0,
      viewIndex: 0,
      envelopeIndex: 0,
      panel: null,
      directionSelection: [], // slices picked in the Directions panel
      mode: 'canvas',
      tool: 'select',
      playing: false,
      playStep: null,
      saveState: 'saved',
      message: null,
    },
  };
  const listeners = new Set();
  const undoStack = [];
  const redoStack = [];
  let saveTimer = null;
  let lastCoalesce = null;

  const notify = (what) => listeners.forEach((listener) => listener(what));

  function snapshot() {
    return { doc: JSON.stringify(state.doc), library: JSON.stringify(state.library) };
  }

  function restore(entry) {
    state.doc = JSON.parse(entry.doc);
    state.library = JSON.parse(entry.library);
    clampUi();
    notify('all');
    scheduleSave();
  }

  function clampUi() {
    const ui = state.ui;
    ui.sequenceIndex = Math.min(ui.sequenceIndex, state.doc.sequences.length - 1);
    ui.stepIndex = Math.min(ui.stepIndex, state.doc.sequences[ui.sequenceIndex].steps.length - 1);
    ui.viewIndex = Math.min(ui.viewIndex, state.doc.views.length - 1);
    ui.envelopeIndex = Math.min(ui.envelopeIndex, state.library.envelopes.length - 1);
  }

  function scheduleSave() {
    state.ui.saveState = 'saving';
    clearTimeout(saveTimer);
    saveTimer = setTimeout(save, SAVE_DELAY_MS);
  }

  async function save() {
    try {
      const text = JSON.stringify(state.doc);
      await window.host.writeText(state.paths.currentFile, text);
      if (state.prefs.lastFile) await window.host.writeText(state.prefs.lastFile, text);
      await window.host.writeText(state.paths.envelopesFile, JSON.stringify(state.library));
      state.ui.saveState = 'saved';
    } catch (error) {
      state.ui.saveState = 'error';
      state.ui.message = `Could not save: ${error.message}`;
    }
    notify('save');
  }

  return {
    state,
    subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    /**
     * Applies an undoable change. Changes with the same `coalesce` key in a row
     * (a drag, typing a name) become one undo step.
     */
    edit(change, { coalesce = null } = {}) {
      if (!coalesce || coalesce !== lastCoalesce) {
        undoStack.push(snapshot());
        if (undoStack.length > UNDO_LIMIT) undoStack.shift();
        redoStack.length = 0;
      }
      lastCoalesce = coalesce;
      change(state.doc, state.library);
      clampUi();
      notify('doc');
      scheduleSave();
    },
    endCoalesce() {
      lastCoalesce = null;
    },
    undo() {
      if (!undoStack.length) return;
      redoStack.push(snapshot());
      restore(undoStack.pop());
      lastCoalesce = null;
    },
    redo() {
      if (!redoStack.length) return;
      undoStack.push(snapshot());
      restore(redoStack.pop());
      lastCoalesce = null;
    },
    // Replaces the whole document (New, Load); not undoable.
    replaceDocument(doc) {
      state.doc = doc;
      undoStack.length = 0;
      redoStack.length = 0;
      Object.assign(state.ui, { sequenceIndex: 0, stepIndex: 0, viewIndex: 0 });
      notify('all');
      scheduleSave();
    },
    setUi(patch) {
      Object.assign(state.ui, patch);
      clampUi();
      notify('ui');
    },
    async savePrefs(patch) {
      Object.assign(state.prefs, patch);
      await window.host.writeText(state.paths.prefsFile, JSON.stringify(state.prefs, null, 2));
      notify('ui');
    },
    saveNow: save,
    get currentSequence() {
      return state.doc.sequences[state.ui.sequenceIndex];
    },
    get currentView() {
      return state.doc.views[state.ui.viewIndex];
    },
  };
}
