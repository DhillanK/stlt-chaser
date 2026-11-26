// Colours for what the editor draws on canvases; the same palette as styles.css,
// taken from Resolume Arena 7. Opened from STLT, the editor takes STLT's palette instead
// (useHost, with body.host-stltvj in styles.css): its Ember look, amber on warm near-black.
export const THEME = {
  stage: '#333333',
  panel: '#1f1f1f',
  grid: '#3d3d3d',
  text: '#e8e8e8',
  textDim: '#9a9a9a',
  accent: '#7fffd2',
  onAccent: '#10302a',
  accentWash: 'rgba(127, 255, 210, 0.14)',
  sliceOff: '#1f1f1f',
  sliceHover: '#2b3633',
  outline: '#72857f',
  header: '#46524e',
};

const STLTVJ = {
  stage: '#0a0908',
  panel: '#171513',
  grid: '#1d1b18',
  text: '#f1ede7',
  textDim: '#8b847a',
  accent: '#ffa62b',
  onAccent: '#1d1203',
  accentWash: 'rgba(255, 166, 43, 0.14)',
  sliceOff: '#141210',
  sliceHover: '#2a2116',
  outline: '#655f57',
  header: '#2a2724',
};

// The program the effects run in, for the editor's wording: 'Resolume' or 'STLT'.
export let HOST_NAME = 'Resolume';

export function useHost(stltvj) {
  if (!stltvj) return;
  HOST_NAME = 'STLT';
  Object.assign(THEME, STLTVJ);
  document.body.classList.add('host-stltvj');
  document.title = 'STLT-CHASER for STLT';
}
