import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { parseAdvancedOutput, presetReference, matchPreset, presetFolderFor } from '../src/advancedOutput.mjs';
import * as D from '../src/document.mjs';
import { generate, firstSteps, maxBlock } from '../src/factory.mjs';
import { evaluate, sampleLut, createEnvelopeLibrary, removeEnvelope } from '../src/envelopes.mjs';
import { boxCoverage } from '../src/geometry.mjs';
import { stageLayout, resolveShow } from '../src/layout.mjs';

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

// Documents may be redirected (e.g. OneDrive), so try that location too.
const realPath = [process.env.OneDrive, process.env.USERPROFILE]
  .filter(Boolean)
  .map((base) => `${base}\\Documents\\Resolume Arena\\Preferences\\AdvancedOutput.xml`)
  .find((file) => existsSync(file)) ?? '';
test('parses the AdvancedOutput.xml on this machine', { skip: !realPath }, () => {
  let xml = readFileSync(realPath, 'utf8');
  const reference = presetReference(xml);
  if (reference) {
    // Arena 7.28+ stub: read the preset it points to, as the editor does.
    const folder = presetFolderFor(realPath);
    const files = readdirSync(folder).map((name) => ({ name, mtimeMs: statSync(`${folder}\\${name}`).mtimeMs }));
    const name = matchPreset(reference, files);
    assert.ok(name, `preset "${reference}" not found in ${folder}`);
    xml = readFileSync(`${folder}\\${name}`, 'utf8');
  }
  const setup = parseAdvancedOutput(xml);
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

const sorted = (steps) => steps.map((s) => [...s].sort());

test('factory patterns order the slices by where they are', () => {
  const slices = gridSlices(); // 3 columns x 2 rows, ids "<col><row>"
  assert.deepEqual(sorted(generate(slices, { pattern: 'left-right' }).steps), [['00', '01'], ['10', '11'], ['20', '21']]);
  assert.deepEqual(sorted(generate(slices, { pattern: 'right-left' }).steps), [['20', '21'], ['10', '11'], ['00', '01']]);
  assert.deepEqual(sorted(generate(slices, { pattern: 'top-bottom' }).steps), [['00', '10', '20'], ['01', '11', '21']]);
  assert.deepEqual(sorted(generate(slices, { pattern: 'bottom-top' }).steps), [['01', '11', '21'], ['00', '10', '20']]);
  assert.deepEqual(sorted(generate(slices, { pattern: 'sides-center' }).steps), [['00', '01', '20', '21'], ['10', '11']]);
  assert.deepEqual(sorted(generate(slices, { pattern: 'diagonal' }).steps), [['00'], ['01', '10'], ['11', '20'], ['21']]);
});

test('factory centre out, clockwise and random', () => {
  const square = (id, x, y) => ({ id, points: [[x, y], [x + 10, y], [x + 10, y + 10], [x, y + 10]] });
  // A plus sign: the middle, then four arms.
  const plus = [square('m', 20, 20), square('n', 20, 0), square('e', 40, 20), square('s', 20, 40), square('w', 0, 20)];
  assert.deepEqual(sorted(generate(plus, { pattern: 'center-out' }).steps), [['m'], ['e', 'n', 's', 'w']]);
  assert.deepEqual(sorted(generate(plus, { pattern: 'out-center' }).steps), [['e', 'n', 's', 'w'], ['m']]);
  // Clockwise from twelve o'clock (the middle has no angle of its own and comes first).
  const arms = generate(plus.slice(1), { pattern: 'clockwise' }).steps.flat();
  assert.deepEqual(arms, ['n', 'e', 's', 'w']);
  const random = generate(plus, { pattern: 'random' }).steps;
  assert.equal(random.length, 5);
  assert.deepEqual(random.flat().sort(), ['e', 'm', 'n', 's', 'w']);
  assert.deepEqual(generate(plus, { pattern: 'random' }).steps, random, 'the same settings give the same sequence');
  assert.equal(generate(plus, { pattern: 'random', steps: 2 }).steps.length, 2);
});

test('factory roughly aligned slices light together', () => {
  const square = (id, x, y) => ({ id, points: [[x, y], [x + 100, y], [x + 100, y + 100], [x, y + 100]] });
  const slices = [square('a', 0, 0), square('b', 150, 4), square('c', 12, 160), square('d', 145, 150)];
  assert.deepEqual(sorted(generate(slices, { pattern: 'left-right' }).steps), [['a', 'c'], ['b', 'd']]);
  assert.deepEqual(sorted(generate(slices, { pattern: 'top-bottom' }).steps), [['a', 'b'], ['c', 'd']]);
});

test('factory steps, fill, order, block, modulo and moves', () => {
  const slices = gridSlices();
  // A fixed number of steps splits the pattern in equal stretches.
  assert.deepEqual(sorted(generate(slices, { pattern: 'left-right', steps: 2 }).steps), [['00', '01'], ['10', '11', '20', '21']]);
  assert.equal(generate(slices, { pattern: 'left-right', steps: 6 }).steps.length, 6);
  const build = generate(slices, { pattern: 'left-right', fill: 'build' }).steps;
  assert.deepEqual(build.map((s) => s.length), [2, 4, 6]);
  assert.deepEqual(sorted(generate(slices, { pattern: 'left-right', alternate: 'odd-even' }).steps), [['00', '01'], ['20', '21'], ['10', '11']]);
  assert.deepEqual(sorted(generate(slices, { pattern: 'left-right', alternate: 'there-back' }).steps), [['00', '01'], ['10', '11'], ['20', '21'], ['10', '11']]);
  assert.equal(generate(slices, { pattern: 'left-right', block: 2 }).steps.length, 2);
  assert.deepEqual(sorted(generate(slices, { pattern: 'left-right', modulo: 2 }).steps), [['00', '01', '20', '21'], ['10', '11']]);
  assert.equal(maxBlock(slices, { pattern: 'left-right' }), 3);
  // A slice moved by hand lights from its new step only.
  const moved = generate(slices, { pattern: 'left-right', moves: { 20: 0, gone: 1 } }).steps;
  assert.deepEqual(sorted(moved), [['00', '01', '20'], ['10', '11'], ['21']]);
  assert.deepEqual(firstSteps(moved).get('20'), 0);
  assert.deepEqual(generate([], { pattern: 'left-right' }).steps, [[]]);
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

// ---- Arena 7.28+ stub AdvancedOutput.xml (written by Arena 7.28.1 rev 26535) ----
const STUB_728 = `<?xml version="1.0" encoding="utf-8"?>
<ScreenSetup name="ScreenSetup" presetFile="Dave 80th - LED Stage P39">
	<versionInfo name="Resolume Arena" majorVersion="7" minorVersion="28" microVersion="1" revision="26535"/>
</ScreenSetup>`;

test('a 7.28 stub names its preset; a full setup does not', () => {
  assert.equal(presetReference(STUB_728), 'Dave 80th - LED Stage P39');
  assert.equal(presetReference(SAMPLE), null);
  assert.throws(() => parseAdvancedOutput(STUB_728), /No composition size/);
});

test('a full setup with a presetFile attribute is still read directly', () => {
  const tagged = SAMPLE.replace('<ScreenSetup name="ScreenSetup">', '<ScreenSetup name="ScreenSetup" presetFile="X">');
  assert.equal(presetReference(tagged), null);
});

test('the stub preset name matches its file loosely, not a similar preset', () => {
  const files = [
    { name: 'Dave 80th - LED Stage P3.9 + Sides.xml', mtimeMs: 3 },
    { name: 'Dave 80th - LED Stage P3.9.xml', mtimeMs: 2 },
    { name: 'notes.txt', mtimeMs: 9 },
  ];
  assert.equal(matchPreset('Dave 80th - LED Stage P39', files), 'Dave 80th - LED Stage P3.9.xml');
  assert.equal(matchPreset('Dave 80th - LED Stage P3.9 + Sides', files), 'Dave 80th - LED Stage P3.9 + Sides.xml');
  assert.equal(matchPreset('Something else', files), null);
});

test('an exact name wins over a loose one, then the newest loose one', () => {
  assert.equal(matchPreset('A.1', [{ name: 'A1.xml', mtimeMs: 5 }, { name: 'A.1.xml', mtimeMs: 1 }]), 'A.1.xml');
  assert.equal(matchPreset('A1', [{ name: 'A_1.xml', mtimeMs: 1 }, { name: 'A.1.xml', mtimeMs: 5 }]), 'A.1.xml');
});

test('presets folder sits next to Preferences, OneDrive paths included', () => {
  assert.equal(
    presetFolderFor('C:\\Users\\Dhill\\OneDrive\\Documents\\Resolume Arena\\Preferences\\AdvancedOutput.xml'),
    'C:\\Users\\Dhill\\OneDrive\\Documents\\Resolume Arena\\Presets\\Advanced Output',
  );
  assert.equal(presetFolderFor('/Users/x/Documents/Resolume Arena/Preferences/AdvancedOutput.xml'), '/Users/x/Documents/Resolume Arena/Presets/Advanced Output');
});

test('a preset file parses like a full AdvancedOutput.xml', () => {
  const preset = `<?xml version="1.0" encoding="utf-8"?>
<XmlState name="Dave 80th - LED Stage P3.9">
${SAMPLE.replace(/^<\?xml[^>]*>\n/, '')}
</XmlState>`;
  const setup = parseAdvancedOutput(preset);
  assert.equal(setup.compWidth, 1000);
  assert.equal(setup.slices.length, 3);
});

test('a band takes the slices mostly inside it, not a big one around it', () => {
  const band = { minX: 0, minY: 0, maxX: 100, maxY: 100 };
  assert.equal(boxCoverage(band, { minX: 10, minY: 10, maxX: 50, maxY: 50 }), 1);
  assert.equal(boxCoverage(band, { minX: 50, minY: 0, maxX: 150, maxY: 100 }), 0.5);
  assert.ok(boxCoverage(band, { minX: 0, minY: 0, maxX: 1920, maxY: 1080 }) < 0.01);
  assert.equal(boxCoverage(band, { minX: 200, minY: 0, maxX: 300, maxY: 100 }), 0);
});

test('the stage shows every screen side by side, one screen, or the composition', () => {
  const sq = (x, y, w, h) => [[x, y], [x + w, y], [x + w, y + h], [x, y + h]];
  const doc = {
    screens: [{ id: 'a', name: 'LED', width: 2048, height: 768 }, { id: 'b', name: 'Stream', width: 1920, height: 1080 }],
    slices: [
      { id: '1', name: 'Main 1', screen: 'a', points: sq(0, 0, 100, 100), outPoints: sq(256, 0, 256, 256) },
      { id: '2', name: 'Everything', screen: 'b', points: sq(0, 0, 1920, 1080), outPoints: sq(0, 0, 1920, 1080) },
      { id: '3', name: 'Lost', screen: 'z', points: sq(10, 10, 50, 50), outPoints: [] },
    ],
  };
  const all = stageLayout(doc);
  assert.equal(all.show, 'screens');
  assert.deepEqual(all.frames.map((f) => f.name), ['LED', 'Stream', 'On no screen']);
  // Frames do not overlap, and a slice sits where it goes on its screen.
  const [led, stream] = all.frames;
  assert.ok(stream.x >= led.x + led.width || stream.y >= led.y + led.height);
  const main = all.items.find((i) => i.slice.id === '1');
  assert.deepEqual(main.points[0], [led.x + 256, led.y]);
  const everything = all.items.find((i) => i.slice.id === '2');
  assert.deepEqual(everything.points[0], [stream.x, stream.y]);
  assert.equal(all.items.find((i) => i.slice.id === '3').frame, 2);

  const one = stageLayout(doc, 'b');
  assert.deepEqual(one.items.map((i) => i.slice.id), ['2']);
  assert.deepEqual(one.frames.map((f) => [f.x, f.y]), [[0, 0]]);

  const composition = stageLayout(doc, 'composition');
  assert.equal(composition.frames.length, 0);
  assert.equal(composition.items.length, 3);

  // A screen that is gone, or a document without output points: back to what can be shown.
  assert.equal(resolveShow(doc, 'gone'), 'screens');
  assert.equal(resolveShow({ screens: [], slices: doc.slices }, 'a'), 'composition');
});
