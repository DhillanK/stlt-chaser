const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('host', {
  paths: () => ipcRenderer.invoke('paths'),
  readText: (file) => ipcRenderer.invoke('read-text', file),
  listDir: (folder) => ipcRenderer.invoke('list-dir', folder),
  writeText: (file, text) => ipcRenderer.invoke('write-text', file, text),
  writeBinary: (file, base64) => ipcRenderer.invoke('write-binary', file, base64),
  openDialog: (options) => ipcRenderer.invoke('open-dialog', options),
  saveDialog: (options) => ipcRenderer.invoke('save-dialog', options),
  watch: (file) => ipcRenderer.invoke('watch', file),
  unwatch: (file) => ipcRenderer.invoke('unwatch', file),
  onFileChanged: (callback) => ipcRenderer.on('file-changed', (_e, file) => callback(file)),
  pluginStatus: () => ipcRenderer.invoke('plugin-status'),
  installPlugins: () => ipcRenderer.invoke('install-plugins'),
  showFolder: (folder) => ipcRenderer.invoke('show-folder', folder),
});
