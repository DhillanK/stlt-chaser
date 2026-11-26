// Writes a demo stage (an invented LED set) into a data folder, for screenshots and trying the editor:
//   node tools/demo-stage.mjs <folder>
//   STLT_DATA_DIR=<folder> npm start
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import * as D from '../src/document.mjs';
import { defaultFactorySettings, generate } from '../src/factory.mjs';
import { createEnvelope } from '../src/envelopes.mjs';

const folder = process.argv[2];
if (!folder) {
  console.error('usage: node tools/demo-stage.mjs <folder>');
  process.exit(2);
}

const W = 1920;
const H = 1080;
const slices = [];
let id = 1000;
const rect = (name, x, y, w, h) => slices.push({ id: String(id++), name, screen: 'main', points: [[x, y], [x + w, y], [x + w, y + h], [x, y + h]] });

// Header bars across the top.
for (let i = 0; i < 6; i += 1) rect(`HEAD ${i + 1}`, 120 + i * 285, 40, 255, 70);
// Two towers of 3 x 4 panels either side of the main screen.
for (const [side, x0] of [['L', 110], ['R', 1340]]) {
  for (let row = 0; row < 4; row += 1) {
    for (let col = 0; col < 3; col += 1) rect(`${side}${row * 3 + col + 1}`, x0 + col * 160, 170 + row * 160, 150, 150);
  }
}
// Main screen and DJ booth.
rect('SCREEN', 640, 200, 640, 520);
rect('BOOTH', 760, 790, 400, 150);
// Floor strips under the towers.
for (let i = 0; i < 4; i += 1) rect(`FLOOR ${i + 1}`, 110 + i * 120, 860, 100, 60);
for (let i = 0; i < 4; i += 1) rect(`FLOOR ${i + 5}`, 1340 + i * 120, 860, 100, 60);

const doc = D.createDocument({ compWidth: W, compHeight: H, screens: [{ id: 'main', name: 'LED wall', width: W, height: H }], slices }, '', 'Demo stage');
doc.source.path = '';

const factory = (name, patch) => {
  const settings = { ...defaultFactorySettings(), ...patch };
  return { id: D.newId('s'), name, steps: generate(slices, settings).steps, factory: settings };
};
const byName = (names) => slices.filter((s) => names.includes(s.name)).map((s) => s.id);

doc.sequences = [
  factory('Sweep L→R', { pattern: 'left-right' }),
  factory('Odd / even', { pattern: 'left-right', alternate: 'odd-even' }),
  factory('Outside in', { pattern: 'sides-center' }),
  factory('Random 16ths', { pattern: 'random', steps: 16, shuffle: 7 }),
  {
    id: D.newId('s'),
    name: 'Drop',
    factory: null,
    steps: [
      byName(['SCREEN', 'BOOTH']),
      [],
      byName(['HEAD 1', 'HEAD 2', 'HEAD 3', 'HEAD 4', 'HEAD 5', 'HEAD 6']),
      [],
      slices.filter((s) => /^[LR]\d/.test(s.name)).map((s) => s.id),
      [],
      byName(['SCREEN', 'BOOTH', 'HEAD 1', 'HEAD 6']),
      slices.map((s) => s.id),
    ],
  },
];
doc.views.push({ id: 'v-towers', name: 'Towers', visible: slices.filter((s) => /^[LR]\d/.test(s.name)).map((s) => s.id) });
doc.views.push({ id: 'v-top', name: 'Headers', visible: slices.filter((s) => s.name.startsWith('HEAD')).map((s) => s.id) });

const punch = createEnvelope('Punch', [
  { x: 0, y: 1, in: [0, 0], out: [0.05, 0] },
  { x: 0.18, y: 0.55, in: [-0.06, 0.2], out: [0.12, -0.15] },
  { x: 1, y: 0, in: [-0.35, 0], out: [0, 0] },
]);
const library = { format: 'stilletchaser-envelopes', version: 1, envelopes: [createEnvelope('S Curve'), punch] };

mkdirSync(folder, { recursive: true });
writeFileSync(join(folder, 'current.stchase'), JSON.stringify(doc));
writeFileSync(join(folder, 'envelopes.json'), JSON.stringify(library));
writeFileSync(join(folder, 'prefs.json'), JSON.stringify({ bpm: 128, rate: 1, lastFile: null }));
console.log(`demo stage with ${slices.length} slices and ${doc.sequences.length} sequences written to ${folder}`);
