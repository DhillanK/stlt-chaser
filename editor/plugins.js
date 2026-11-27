// Copies the STLT plugins that ship with the editor into Resolume's Extra Effects folders.
// Main-process code (Node); used by main.js.
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const PLUGINS = ['STLT-CHASER', 'STLT-WIPER', 'STLT-OUTLINER', 'STLT-SAMPLER', 'STLT-TILER'];
const PRODUCTS = ['Resolume Arena', 'Resolume Avenue'];
const FOLDER = 'STLT-CHASER';
// Folders and files earlier versions installed; left in place they would load twice.
const LEGACY = [path.join('Stilletchaser', 'Stilletchaser.dll'), ...PLUGINS.map((p) => path.join('Stilletchaser', `${p}.dll`))];

function pluginFile(name, platform = process.platform) {
  return platform === 'darwin' ? `${name}.bundle` : `${name}.dll`;
}

// The plugins next to the editor: packaged in resources/plugins, or the local build when developing.
function bundledPluginDir({ resourcesPath, appPath, isPackaged }) {
  if (isPackaged) return path.join(resourcesPath, 'plugins');
  const dist = path.join(appPath, '..', 'plugin', 'dist');
  if (fs.existsSync(dist)) return dist;
  return path.join(appPath, '..', 'plugin', 'build', process.platform === 'win32' ? 'Release' : '');
}

// The file that changes when a plugin is rebuilt (inside the bundle on macOS).
function binaryOf(pluginPath) {
  if (!pluginPath.endsWith('.bundle')) return pluginPath;
  return path.join(pluginPath, 'Contents', 'MacOS', path.basename(pluginPath, '.bundle'));
}

function sameFile(a, b) {
  try {
    const sa = fs.statSync(binaryOf(a));
    const sb = fs.statSync(binaryOf(b));
    return sa.size === sb.size && fs.readFileSync(binaryOf(a)).equals(fs.readFileSync(binaryOf(b)));
  } catch {
    return false;
  }
}

/** Which Resolume installs exist and whether their copy of the plugins matches ours. */
function pluginStatus(sourceDir, documentsDir) {
  const available = PLUGINS.filter((p) => fs.existsSync(path.join(sourceDir, pluginFile(p))));
  const targets = PRODUCTS.filter((product) => fs.existsSync(path.join(documentsDir, product))).map((product) => {
    const folder = path.join(documentsDir, product, 'Extra Effects', FOLDER);
    const upToDate = available.length > 0 && available.every((p) => sameFile(path.join(sourceDir, pluginFile(p)), path.join(folder, pluginFile(p))));
    return { product, folder, upToDate };
  });
  return { sourceDir, available, targets, needsInstall: available.length > 0 && targets.some((t) => !t.upToDate) };
}

function explain(error, product) {
  if (error.code === 'EBUSY' || error.code === 'EPERM' || error.code === 'EACCES') {
    return process.platform === 'win32'
      ? `${product}: the plugin files are in use or protected. Close ${product} and try again. If Windows "Controlled folder access" is on, allow STLT-CHASER in Windows Security first.`
      : `${product}: no permission to write the plugins. Close ${product} and try again.`;
  }
  return `${product}: ${error.message}`;
}

/** Copies the plugins into every Resolume install found. Returns { installed: [], errors: [] }. */
function installPlugins(sourceDir, documentsDir) {
  const status = pluginStatus(sourceDir, documentsDir);
  const result = { installed: [], errors: [] };
  if (!status.available.length) {
    result.errors.push(`No plugins found next to the editor (${sourceDir}).`);
    return result;
  }
  if (!status.targets.length) {
    result.errors.push('Resolume Arena or Avenue does not seem to be installed (no folder in Documents).');
    return result;
  }
  for (const target of status.targets) {
    try {
      fs.mkdirSync(target.folder, { recursive: true });
      for (const plugin of status.available) {
        const from = path.join(sourceDir, pluginFile(plugin));
        const to = path.join(target.folder, pluginFile(plugin));
        fs.rmSync(to, { recursive: true, force: true });
        fs.cpSync(from, to, { recursive: true });
      }
      for (const legacy of LEGACY) fs.rmSync(path.join(path.dirname(target.folder), legacy), { force: true });
      if (process.platform === 'darwin') {
        // Downloaded files carry a quarantine flag that stops Resolume loading the bundles.
        try {
          execFileSync('xattr', ['-dr', 'com.apple.quarantine', target.folder]);
        } catch {
          // Nothing to clear.
        }
      }
      result.installed.push(target.product);
    } catch (error) {
      result.errors.push(explain(error, target.product));
    }
  }
  return result;
}

module.exports = { PLUGINS, pluginFile, bundledPluginDir, pluginStatus, installPlugins };
