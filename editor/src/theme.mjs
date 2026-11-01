// Colours for what the editor draws on canvases; the same palette as styles.css,
// taken from Resolume Arena 7. Opened from STLT-VJ, the editor takes STLT-VJ's palette instead
// (useHost, with body.host-stltvj in styles.css).
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
  stage: '#0f1114',
  panel: '#14161a',
  grid: '#1e2127',
  text: '#e6e8ec',
  textDim: '#8a92a0',
  accent: '#96bee4',
  onAccent: '#0a121e',
  accentWash: 'rgba(150, 190, 228, 0.14)',
  sliceOff: '#16181d',
  sliceHover: '#1f2a36',
  outline: '#6a7b8f',
  header: '#2a3a4c',
};

// The program the effects run in, for the editor's wording: 'Resolume' or 'STLT-VJ'.
export let HOST_NAME = 'Resolume';

export function useHost(stltvj) {
  if (!stltvj) return;
  HOST_NAME = 'STLT-VJ';
  Object.assign(THEME, STLTVJ);
  document.body.classList.add('host-stltvj');
  document.title = 'STLT-CHASER for STLT-VJ';
}
