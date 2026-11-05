// Stand-in for the Electron bridge so the editor UI can be tried in a plain browser
// (node dev/serve.mjs). In the real app preload.js defines window.host first and this does nothing.
(function () {
  if (window.host) return;
  const files = new Map();
  const listeners = [];
  window.host = {
    async paths() {
      return {
        dataDir: '/mock',
        currentFile: '/mock/current.stchase',
        envelopesFile: '/mock/envelopes.json',
        prefsFile: '/mock/prefs.json',
        documents: '/mock',
        arenaOutputs: [{ label: 'Resolume Arena', path: '/__arena/AdvancedOutput.xml', exists: true }],
      };
    },
    async readText(file) {
      if (files.has(file)) return files.get(file);
      if (file.startsWith('/__')) {
        const response = await fetch(file);
        return response.ok ? response.text() : null;
      }
      return null;
    },
    async listDir() {
      return [];
    },
    async writeText(file, text) {
      files.set(file, text);
    },
    async writeBinary(file) {
      files.set(file, '(binary)');
    },
    async openDialog() {
      return null;
    },
    async saveDialog() {
      return null;
    },
    async watch() {},
    async unwatch() {},
    onFileChanged(callback) {
      listeners.push(callback);
    },
    _files: files,
  };
})();
