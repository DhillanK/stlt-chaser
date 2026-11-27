import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdtempSync, mkdirSync, writeFileSync, readdirSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const require = createRequire(import.meta.url);
const plugins = require('../plugins.js');

function fixture() {
  const root = mkdtempSync(join(tmpdir(), 'stlt-test-'));
  const source = join(root, 'plugins');
  mkdirSync(source);
  for (const name of plugins.PLUGINS) {
    const file = join(source, plugins.pluginFile(name));
    if (file.endsWith('.bundle')) {
      mkdirSync(join(file, 'Contents', 'MacOS'), { recursive: true });
      writeFileSync(join(file, 'Contents', 'MacOS', name), `binary ${name}`);
    } else writeFileSync(file, `binary ${name}`);
  }
  const documents = join(root, 'Documents');
  mkdirSync(join(documents, 'Resolume Arena', 'Extra Effects', 'Stilletchaser'), { recursive: true });
  writeFileSync(join(documents, 'Resolume Arena', 'Extra Effects', 'Stilletchaser', 'Stilletchaser.dll'), 'old');
  return { root, source, documents };
}

test('installs the plugins into every Resolume found and removes old copies', () => {
  const { root, source, documents } = fixture();
  try {
    assert.equal(plugins.pluginStatus(source, documents).needsInstall, true);
    const result = plugins.installPlugins(source, documents);
    assert.deepEqual(result, { installed: ['Resolume Arena'], errors: [] });
    const folder = join(documents, 'Resolume Arena', 'Extra Effects', 'STLT-CHASER');
    assert.equal(readdirSync(folder).length, plugins.PLUGINS.length);
    assert.equal(existsSync(join(documents, 'Resolume Arena', 'Extra Effects', 'Stilletchaser', 'Stilletchaser.dll')), false);
    assert.equal(plugins.pluginStatus(source, documents).needsInstall, false);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('a changed plugin needs installing again', () => {
  const { root, source, documents } = fixture();
  try {
    plugins.installPlugins(source, documents);
    const file = join(source, plugins.pluginFile('STLT-WIPER'));
    writeFileSync(file.endsWith('.bundle') ? join(file, 'Contents', 'MacOS', 'STLT-WIPER') : file, 'binary v2');
    assert.equal(plugins.pluginStatus(source, documents).needsInstall, true);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('reports when Resolume is not installed', () => {
  const { root, source } = fixture();
  try {
    const result = plugins.installPlugins(source, join(root, 'NoDocuments'));
    assert.equal(result.installed.length, 0);
    assert.match(result.errors[0], /not seem to be installed/);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
