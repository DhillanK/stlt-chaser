// Reads Resolume Arena's AdvancedOutput.xml. Arena writes it in a fixed, regular shape,
// so plain string scanning is enough and keeps this module usable outside the browser.

function attribute(tag, name) {
  const match = tag.match(new RegExp(`\\s${name}="([^"]*)"`));
  return match ? decodeEntities(match[1]) : '';
}

function decodeEntities(text) {
  return text
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'")
    .replace(/&amp;/g, '&');
}

// Every <tag ...>...</tag> block directly searchable in `xml`, in document order.
function blocks(xml, tag) {
  const result = [];
  const open = new RegExp(`<${tag}[\\s>]`, 'g');
  let match;
  while ((match = open.exec(xml))) {
    const close = xml.indexOf(`</${tag}>`, match.index);
    if (close < 0) break;
    const headEnd = xml.indexOf('>', match.index);
    result.push({ head: xml.slice(match.index, headEnd + 1), body: xml.slice(headEnd + 1, close) });
    open.lastIndex = close;
  }
  return result;
}

function vertices(xml) {
  const points = [];
  for (const match of xml.matchAll(/<v\s[^>]*>/g)) {
    points.push([parseFloat(attribute(match[0], 'x')), parseFloat(attribute(match[0], 'y'))]);
  }
  return points;
}

function firstParam(body, name) {
  const match = body.match(new RegExp(`<Param name="${name}"[^>]*>`));
  return match ? attribute(match[0], 'value') : '';
}

function layerPoints(type, body) {
  const container = type === 'Slice' ? 'InputRect' : 'InputContour';
  const found = blocks(body, container)[0];
  return found ? vertices(found.body) : [];
}

function outputPoints(type, body) {
  const container = type === 'Slice' ? 'OutputRect' : 'OutputContour';
  const found = blocks(body, container)[0];
  return found ? vertices(found.body) : [];
}

// ---- Arena 7.28+ preset stubs ----
// From 7.28 Arena can write Preferences/AdvancedOutput.xml as a stub that only names the
// active preset (<ScreenSetup presetFile="..."> with no size, screens or slices); the setup
// itself lives in Presets/Advanced Output/<preset>.xml. The name in presetFile is not always
// the file name verbatim (e.g. "P3.9" is written as "P39"), so names are compared loosely.

/** The preset a stub points to, or null when `xml` carries its own setup. */
export function presetReference(xml) {
  if (/<CurrentCompositionTextureSize[\s>/]/.test(xml)) return null;
  const root = xml.match(/<ScreenSetup\s[^>]*>/);
  const name = root ? attribute(root[0], 'presetFile').trim() : '';
  return name || null;
}

const looseName = (name) => name.replace(/\.xml$/i, '').toLowerCase().replace(/[^\p{L}\p{N}]/gu, '');

/**
 * The preset file for `reference` among `files` ([{name, mtimeMs}] in the presets folder):
 * an exact name first, then a loose match (newest if several). Null when nothing matches.
 */
export function matchPreset(reference, files) {
  const xml = files.filter((f) => /\.xml$/i.test(f.name));
  const exact = xml.find((f) => f.name.replace(/\.xml$/i, '') === reference);
  if (exact) return exact.name;
  const want = looseName(reference);
  const loose = xml.filter((f) => looseName(f.name) === want).sort((a, b) => b.mtimeMs - a.mtimeMs);
  return loose.length ? loose[0].name : null;
}

/** Presets folder next to a Preferences/AdvancedOutput.xml path, keeping its separator style. */
export function presetFolderFor(advancedOutputPath) {
  const sep = advancedOutputPath.includes('\\') ? '\\' : '/';
  const parts = advancedOutputPath.split(/[\\/]/);
  return [...parts.slice(0, -2), 'Presets', 'Advanced Output'].join(sep);
}

/**
 * @returns {{compWidth:number, compHeight:number, screens:Array, slices:Array}}
 * Slices come in the order Arena's slice panel shows them (oldest first within each screen).
 */
export function parseAdvancedOutput(xml) {
  const sizeTag = xml.match(/<CurrentCompositionTextureSize[^>]*>/);
  const compWidth = sizeTag ? parseFloat(attribute(sizeTag[0], 'width')) : 0;
  const compHeight = sizeTag ? parseFloat(attribute(sizeTag[0], 'height')) : 0;
  if (!(compWidth > 0 && compHeight > 0)) throw new Error('No composition size in AdvancedOutput.xml');

  const screens = [];
  const slices = [];
  for (const screen of blocks(xml, 'Screen')) {
    const screenId = attribute(screen.head, 'uniqueId');
    const device = screen.body.match(/<OutputDevice\w*\s[^>]*width="[^"]*"[^>]*>/);
    screens.push({
      id: screenId,
      name: firstParam(screen.body, 'Name') || attribute(screen.head, 'name'),
      width: device ? parseFloat(attribute(device[0], 'width')) : 0,
      height: device ? parseFloat(attribute(device[0], 'height')) : 0,
    });

    const layersBlock = blocks(screen.body, 'layers')[0];
    if (!layersBlock) continue;
    const screenSlices = [];
    const layerTag = /<(Slice|Polygon)\s[^>]*uniqueId="[^"]*"[^>]*>/g;
    let match;
    while ((match = layerTag.exec(layersBlock.body))) {
      const type = match[1];
      const close = layersBlock.body.indexOf(`</${type}>`, match.index);
      if (close < 0) break;
      const body = layersBlock.body.slice(match.index + match[0].length, close);
      layerTag.lastIndex = close;
      const points = layerPoints(type, body);
      if (points.length < 3) continue;
      screenSlices.push({
        id: attribute(match[0], 'uniqueId'),
        name: firstParam(body, 'Name'),
        screen: screenId,
        points,
        outPoints: outputPoints(type, body),
      });
    }
    slices.push(...screenSlices.reverse());
  }
  return { compWidth, compHeight, screens, slices };
}
