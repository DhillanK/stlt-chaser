const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const fs = require('fs');
const path = require('path');
const plugins = require('./plugins');

const pluginSource = () =>
  plugins.bundledPluginDir({ resourcesPath: process.resourcesPath, appPath: app.getAppPath(), isPackaged: app.isPackaged });

// Everything the editor and the plugins share lives here. AppData is outside the folders
// Windows' Controlled Folder Access protects, so the editor can always save.
// STLT_DATA_DIR points the editor at another folder, e.g. a demo stage for screenshots.
const dataDir = process.env.STLT_DATA_DIR || path.join(app.getPath('appData'), 'Stilletchaser');

// Opened from STLT (Output > Chaser): `--stlt-vj <stage file>` names the slices STLT writes for the
// editor, which then follows them instead of Resolume's Advanced Output. STLT also sets STLT_DATA_DIR,
// so its sequences stay apart from a Resolume setup's.
function stltvjStage(argv) {
  const at = argv.indexOf('--stlt-vj');
  return at >= 0 && argv[at + 1] ? path.resolve(argv[at + 1]) : process.env.STLT_VJ_STAGE || null;
}
const stltvj = stltvjStage(process.argv);
// The folder keeps the program's old name (STLT-VJ), so the editor's own settings stay where they are.
if (stltvj) app.setPath('userData', path.join(app.getPath('appData'), 'STLT-CHASER for STLT-VJ'));

function arenaOutputs() {
  const documents = app.getPath('documents');
  return ['Resolume Arena', 'Resolume Avenue'].map((product) => {
    const file = path.join(documents, product, 'Preferences', 'AdvancedOutput.xml');
    return { label: product, path: file, exists: fs.existsSync(file) };
  });
}

function writeAtomic(file, text) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const tmp = `${file}.tmp`;
  fs.writeFileSync(tmp, text, 'utf8');
  fs.renameSync(tmp, file);
}

const watched = new Map();

function createWindow() {
  const win = new BrowserWindow({
    width: 1480,
    height: 920,
    minWidth: 900,
    minHeight: 600,
    backgroundColor: stltvj ? '#0f1114' : '#333333',
    title: stltvj ? 'STLT-CHASER for STLT' : 'STLT-CHASER',
    icon: path.join(__dirname, 'build', 'icon.png'),
    autoHideMenuBar: true,
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false },
  });
  win.removeMenu();
  win.loadFile('index.html');
  win.webContents.on('before-input-event', (event, input) => {
    if (input.type === 'keyDown' && input.key === 'F12') win.webContents.toggleDevTools();
  });

  ipcMain.handle('paths', () => ({
    dataDir,
    currentFile: path.join(dataDir, 'current.stchase'),
    envelopesFile: path.join(dataDir, 'envelopes.json'),
    prefsFile: path.join(dataDir, 'prefs.json'),
    arenaOutputs: arenaOutputs(),
    stltvj,
    documents: app.getPath('documents'),
    version: app.getVersion(),
  }));
  ipcMain.handle('read-text', (_e, file) => (fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : null));
  ipcMain.handle('write-text', (_e, file, text) => writeAtomic(file, text));
  ipcMain.handle('write-binary', (_e, file, base64) => {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, Buffer.from(base64, 'base64'));
  });
  ipcMain.handle('open-dialog', async (_e, options) => {
    const result = await dialog.showOpenDialog(win, { properties: ['openFile'], ...options });
    return result.canceled ? null : result.filePaths[0];
  });
  ipcMain.handle('save-dialog', async (_e, options) => {
    const result = await dialog.showSaveDialog(win, options);
    return result.canceled ? null : result.filePath;
  });
  ipcMain.handle('watch', (_e, file) => {
    if (watched.has(file)) return;
    const listener = (curr, prev) => {
      if (curr.mtimeMs !== prev.mtimeMs && !win.isDestroyed()) win.webContents.send('file-changed', file);
    };
    fs.watchFile(file, { interval: 500 }, listener);
    watched.set(file, listener);
  });
  ipcMain.handle('unwatch', (_e, file) => {
    const listener = watched.get(file);
    if (listener) fs.unwatchFile(file, listener);
    watched.delete(file);
  });
  ipcMain.handle('plugin-status', () => plugins.pluginStatus(pluginSource(), app.getPath('documents')));
  ipcMain.handle('install-plugins', () => plugins.installPlugins(pluginSource(), app.getPath('documents')));
  ipcMain.handle('show-folder', (_e, folder) => shell.openPath(folder));

  // First run, or after an update: offer to put the plugins into Resolume.
  win.webContents.once('did-finish-load', async () => {
    const status = plugins.pluginStatus(pluginSource(), app.getPath('documents'));
    if (stltvj || !app.isPackaged || !status.needsInstall) return;
    const { response } = await dialog.showMessageBox(win, {
      type: 'question',
      buttons: ['Install', 'Later'],
      defaultId: 0,
      cancelId: 1,
      title: 'STLT-CHASER',
      message: 'Install the STLT plugins into Resolume?',
      detail: `STLT-CHASER, STLT-WIPER and STLT-OUTLINER will be copied to:\n${status.targets.map((t) => t.folder).join('\n')}\n\nClose Resolume first, then restart it to load them.`,
    });
    if (response !== 0) return;
    const result = plugins.installPlugins(pluginSource(), app.getPath('documents'));
    await dialog.showMessageBox(win, {
      type: result.errors.length ? 'warning' : 'info',
      title: 'STLT-CHASER',
      message: result.errors.length ? 'Some plugins were not installed' : 'Plugins installed. Restart Resolume to load them.',
      detail: [...result.installed.map((p) => `Installed for ${p}.`), ...result.errors].join('\n'),
    });
  });
}

// One editor at a time: Output > Chaser in STLT brings the open one to the front.
if (!app.requestSingleInstanceLock()) app.quit();
else {
  app.on('second-instance', () => {
    const win = BrowserWindow.getAllWindows()[0];
    if (!win) return;
    if (win.isMinimized()) win.restore();
    win.show();
    win.focus();
  });
  app.whenReady().then(createWindow);
}
app.on('window-all-closed', () => app.quit());
