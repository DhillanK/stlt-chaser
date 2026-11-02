import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { parseAdvancedOutput } from '../src/advancedOutput.mjs';
import * as D from '../src/document.mjs';
import { generate, autoGrid } from '../src/factory.mjs';
import { evaluate, sampleLut, createEnvelopeLibrary, removeEnvelope } from '../src/envelopes.mjs';

const SAMPLE = `<?xml version="1.0" encoding="utf-8"?>
<ScreenSetup name="ScreenSetup">
  <CurrentCompositionTextureSize width="1000" height="500"/>
  <screens>
    <Screen name="Screen 1" uniqueId="10">
      <Params name="Params"><Param name="Name" T="STRING" default="" value="Main &amp; Side"/></Params>
      <layers>
        <Slice uniqueId="2"><Params name="Common"><Param name="Name" T="STRING" default="Layer" value="B"/></Params>
          <InputRect orientation="0"><v x="500" y="0"/><v x="1000" y="0"/><v x="1000" y="500"/><v x="500" y="500"/></InputRect>
          <OutputRect orientation="0"><v x="0" y="0"/><v x="10" y="0"/><v x="10" y="10"/><v x="0" y="10"/></OutputRect>
        </Slice>
        <Slice uniqueId="1"><Params name="Common"><Param name="Name" T="STRING" default="Layer" value="A"/></Params>
          <InputRect orientation="0"><v x="0" y="0"/><v x="500" y="0"/><v x="500" y="500"/><v x="0" y="500"/></InputRect>
        </Slice>
        <Polygon uniqueId="3"><Params name="Common"><Param name="Name" T="STRING" default="Layer" value="P"/></Params>
          <InputContour><v x="0" y="0"/><v x="10" y="0"/><v x="5" y="10"/></InputContour>
        </Polygon>
      </layers>
      <OutputDevice><OutputDeviceVirtual name="Screen 1" width="1920" height="1080"/></OutputDevice>
    </Screen>
  </screens>
</ScreenSetup>`;

test('parses slices oldest first with names, points and screens', () => {
  const setup = parseAdvancedOutput(SAMPLE);
  assert.equal(setup.compWidth, 1000);
  assert.deepEqual(setup.slices.map((s) => s.name), ['P', 'A', 'B']);
  assert.equal(setup.screens[0].name, 'Main & Side');
  assert.equal(setup.screens[0].width, 1920);
  assert.deepEqual(setup.slices[2].points[1], [1000, 0]);
  assert.equal(setup.slices[0].points.length, 3);
});

const realPath = `${process.env.USERPROFILE}\\Documents\\Resolume Arena\\Preferences\\AdvancedOutput.xml`;
test('parses the AdvancedOutput.xml on this machine', { skip: !existsSync(realPath) }, () => {
  const setup = parseAdvancedOutput(readFileSync(realPath, 'utf8'));
  assert.ok(setup.slices.length > 0);
  for (const slice of setup.slices) assert.ok(slice.points.length >= 3 && slice.id);
});

function sampleDoc() {
  return D.createDocument(parseAdvancedOutput(SAMPLE), 'x.xml');
}

test('fill buttons repeat and ping-pong the filled steps', () => {
  const seq = D.createSequence('S', 8);
  seq.steps[0] = ['a'];
  seq.steps[1] = ['b'];
  seq.steps[2] = ['c'];
  D.repeatFill(seq);
  assert.deepEqual(seq.steps.map((s) => s[0]), ['a', 'b', 'c', 'a', 'b', 'c', 'a', 'b']);
  const pp = D.createSequence('P', 8);
  pp.steps[0] = ['a'];
  pp.steps[1] = ['b'];
  pp.steps[2] = ['c'];
  D.pingPongFill(pp);
  assert.deepEqual(pp.steps.map((s) => s[0]), ['a', 'b', 'c', 'b', 'a', 'b', 'c', 'b']);
});

test('multi-copy fills every n-th step', () => {
  const seq = D.createSequence('S', 16);
  seq.steps[0] = ['x'];
  D.multiCopyStep(seq, 0, 4);
  assert.deepEqual(seq.steps.map((s, i) => (s.length ? i : -1)).filter((i) => i >= 0), [0, 4, 8, 12]);
});

test('step count clamps between 1 and 1024 and keeps content', () => {
  const seq = D.createSequence('S', 4);
  seq.steps[1] = ['a'];
  assert.equal(D.setStepCount(seq, 5000), 1024);
  assert.equal(D.setStepCount(seq, 0), 1);
  assert.equal(D.setStepCount(seq, 2), 2);
  assert.deepEqual(seq.steps[0], []);
});

test('relink keeps steps by id, maps renamed ids by name, drops removed slices', () => {
  const doc = sampleDoc();
  doc.sequences[0].steps[0] = ['1', '2', '3'];
  const view = doc.views[D.addView(doc)];
  const setup = parseAdvancedOutput(SAMPLE.replace('uniqueId="2"', 'uniqueId="22"').replace(/<Polygon[\s\S]*<\/Polygon>/, ''));
  D.relink(doc, setup, 'x.xml');
  assert.deepEqual(doc.sequences[0].steps[0].sort(), ['1', '22']);
  assert.deepEqual(view.visible.sort(), ['1', '22']);
});

test('custom views start with new slices hidden', () => {
  const doc = sampleDoc();
  const view = doc.views[D.addView(doc, true)];
  assert.deepEqual(D.viewSliceIds(doc, view), []);
  D.setVisibleInView(view, '1', true);
  assert.deepEqual(D.viewSliceIds(doc, view), ['1']);
  assert.equal(D.removeView(doc, 0), 0);
  assert.equal(doc.views[0].id, 'all');
});

function gridSlices() {
  // 3 columns x 2 rows of unit squares.
  const slices = [];
  for (let row = 0; row < 2; row += 1) {
    for (let col = 0; col < 3; col += 1) {
      const x = col * 10;
      const y = row * 10;
      slices.push({ id: `${col}${row}`, points: [[x, y], [x + 8, y], [x + 8, y + 8], [x, y + 8]] });
    }
  }
  return slices;
}

test('factory grid and directions', () => {
  const slices = gridSlices();
  const grid = autoGrid(slices, 30, 20);
  assert.equal(grid.xs.length, 2);
  assert.equal(grid.ys.length, 1);
  const base = { direction: 'Horizontal', block: 1, modulo: 0 };
  assert.deepEqual(generate(slices, base, 30, 20).steps.map((s) => s[0]), ['00', '10', '20', '01', '11', '21']);
  assert.deepEqual(generate(slices, { ...base, direction: 'Vertical' }, 30, 20).steps.map((s) => s[0]), ['00', '01', '10', '11', '20', '21']);
  assert.deepEqual(generate(slices, { ...base, direction: 'Column', flipX: true }, 30, 20).steps.map((s) => s.sort()), [['20', '21'], ['10', '11'], ['00', '01']]);
  assert.equal(generate(slices, { ...base, direction: 'Row' }, 30, 20).steps.length, 2);
});

test('factory grid treats roughly aligned slices as one row and column', () => {
  const square = (id, x, y) => ({ id, points: [[x, y], [x + 100, y], [x + 100, y + 100], [x, y + 100]] });
  const slices = [square('a', 0, 0), square('b', 150, 4), square('c', 12, 160), square('d', 145, 150)];
  const grid = autoGrid(slices, 400, 400);
  assert.equal(grid.xs.length, 1);
  assert.equal(grid.ys.length, 1);
});

test('factory block, modulo, mirror and random', () => {
  const slices = gridSlices();
  const base = { direction: 'Horizontal', block: 1, modulo: 0 };
  assert.equal(generate(slices, { ...base, block: 2 }, 30, 20).steps.length, 3);
  const odd = generate(slices, { ...base, modulo: 2 }, 30, 20).steps;
  // Every other cell in reading order: a checkerboard on a 3-wide grid.
  assert.deepEqual(odd.map((s) => s.sort()), [['00', '11', '20'], ['01', '10', '21']]);
  const mirrored = generate(slices, { ...base, direction: 'Column', mirrorX: true }, 30, 20).steps;
  assert.deepEqual(mirrored[0].sort(), ['00', '01', '20', '21']);
  const random = generate(slices, { ...base, direction: 'Random' }, 30, 20).steps.map((s) => s[0]);
  assert.equal(new Set(random).size, 6);
});

test('envelope curve evaluation and LUT', () => {
  const points = [
    { x: 0, y: 1, in: [0, 0], out: [0, 0] },
    { x: 1, y: 0, in: [0, 0], out: [0, 0] },
  ];
  assert.ok(Math.abs(evaluate(points, 0.5) - 0.5) < 1e-3);
  const lut = sampleLut(points);
  assert.equal(lut.length, 256);
  assert.equal(lut[0], 1);
  assert.equal(lut[255], 0);
  const library = createEnvelopeLibrary();
  assert.equal(removeEnvelope(library, 0), 0);
  assert.equal(library.envelopes.length, 1);
  assert.ok(library.envelopes[0].lut[0] > 0.99 && library.envelopes[0].lut[255] < 0.01);
});

test('reads the stage STLT writes and relinks a document onto it', async () => {
  const { parseStltVjStage, isStltVjStage } = await import('../src/stltvjStage.mjs');
  const stage = {
    format: 'stlt-vj-chaser-stage',
    version: 1,
    compWidth: 1000,
    compHeight: 500,
    screens: [{ id: 'screen-1', name: 'Screen 1', width: 1920, height: 1080 }],
    slices: [
      { id: 'a1', name: 'A', screen: 'screen-1', points: [[0, 0], [500, 0], [500, 500], [0, 500]], outPoints: [[0, 0], [960, 0], [960, 1080], [0, 1080]] },
      { id: 'b2', name: 'B', screen: 'screen-1', points: [[500, 0], [1000, 0], [1000, 500], [500, 500]], outPoints: [] },
      { id: 'a1', name: 'duplicate', screen: 'screen-1', points: [[0, 0], [1, 0], [1, 1]] },
      { id: 'c3', name: 'too few points', screen: 'screen-1', points: [[0, 0], [1, 0]] },
    ],
  };
  const setup = parseStltVjStage(JSON.stringify(stage));
  assert.equal(setup.compWidth, 1000);
  assert.deepEqual(setup.slices.map((s) => s.id), ['a1', 'b2']);
  assert.equal(setup.screens[0].width, 1920);
  assert.ok(isStltVjStage('C:\\x\\chaser-stage.json') && !isStltVjStage('AdvancedOutput.xml'));
  assert.throws(() => parseStltVjStage('{"format":"other"}'));
  assert.throws(() => parseStltVjStage('{"format":"stlt-vj-chaser-stage","compWidth":0,"compHeight":0}'));

  // A document made on Resolume's slices keeps its steps for slices of the same name.
  const doc = sampleDoc();
  const arenaA = doc.slices.find((s) => s.name === 'A').id;
  doc.sequences[0].steps[0] = [arenaA];
  D.relink(doc, setup, 'chaser-stage.json');
  assert.deepEqual(doc.sequences[0].steps[0], ['a1']);
});
