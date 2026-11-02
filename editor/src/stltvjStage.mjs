// Reads the stage STLT writes for the editor (chaser-stage.json, next to its Advanced Output setup):
// the composition size, the screens and every slice's input (composition pixels) and output (screen pixels),
// already in the shape parseAdvancedOutput returns.

function point(value) {
  return Array.isArray(value) && value.length >= 2 && value.every((n) => typeof n === 'number' && Number.isFinite(n)) ? [value[0], value[1]] : null;
}

function points(list) {
  if (!Array.isArray(list)) return [];
  const result = list.map(point);
  return result.every(Boolean) ? result : [];
}

/**
 * @returns {{compWidth:number, compHeight:number, screens:Array, slices:Array}}
 */
export function parseStltVjStage(text) {
  let root;
  try {
    root = JSON.parse(text);
  } catch {
    throw new Error('The STLT stage is not valid JSON');
  }
  if (!root || root.format !== 'stlt-vj-chaser-stage') throw new Error('Not an STLT stage');
  const compWidth = Number(root.compWidth);
  const compHeight = Number(root.compHeight);
  if (!(compWidth > 0 && compHeight > 0)) throw new Error('No composition size in the STLT stage');

  const screens = (Array.isArray(root.screens) ? root.screens : []).map((s, i) => ({
    id: String(s.id ?? `screen-${i + 1}`),
    name: String(s.name ?? `Screen ${i + 1}`),
    width: Number(s.width) || 0,
    height: Number(s.height) || 0,
  }));
  const slices = [];
  const seen = new Set();
  for (const s of Array.isArray(root.slices) ? root.slices : []) {
    const id = String(s.id ?? '');
    const input = points(s.points);
    if (!id || seen.has(id) || input.length < 3) continue;
    seen.add(id);
    slices.push({ id, name: String(s.name ?? ''), screen: String(s.screen ?? ''), points: input, outPoints: points(s.outPoints) });
  }
  return { compWidth, compHeight, screens, slices };
}

// The setup file at `path`: STLT's stage (.json) or Resolume's AdvancedOutput.xml.
export function isStltVjStage(path) {
  return /\.json$/i.test(path ?? '');
}
