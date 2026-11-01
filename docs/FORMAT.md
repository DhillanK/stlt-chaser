# Formato dei file STLT-CHASER

Tutti i file sono JSON UTF-8 e stanno nella cartella dati: `%APPDATA%\Stilletchaser\` su Windows (fuori dalle cartelle protette dall'Accesso alle cartelle controllato), `~/Library/Application Support/Stilletchaser/` su macOS. Le copie fatte con *Save As* possono stare ovunque. Se la variabile d'ambiente `STLT_DATA_DIR` è impostata (lo fa STLT-VJ: `<impostazioni di STLT-VJ>/chaser`), editor e plugin usano quella cartella.

## `current.stchase` — documento attivo

L'editor lo riscrive a ogni modifica (sostituzione atomica: scrive `current.stchase.tmp` e poi rinomina). I plugin lo ricaricano quando cambia la data di modifica.

```json
{
  "format": "stilletchaser",
  "version": 1,
  "name": "Untitled",
  "source": { "path": "C:\\...\\AdvancedOutput.xml", "compWidth": 1280, "compHeight": 720 },
  "screens": [ { "id": "1774528974529", "name": "Screen 1" } ],
  "slices": [
    {
      "id": "1774528979054",
      "name": "Slice 4",
      "screen": "1774528974529",
      "points": [ [0, 0], [359, 0], [359, 306], [0, 306] ]
    }
  ],
  "sequences": [
    {
      "id": "s-1",
      "name": "Sequence 1",
      "steps": [ ["1774528979054"], [], ["1774528979002", "1774528974543"] ],
      "factory": null
    }
  ],
  "views": [
    { "id": "all", "name": "All", "hidden": [] },
    { "id": "v-1", "name": "Left", "visible": ["1774528979054"] }
  ]
}
```

- `points`: contorno dell'area di input della slice, in pixel della composizione, origine in alto a sinistra, nell'ordine di Arena. Per le slice rettangolari sono 4 punti (0 = in alto a sinistra, 1 = in alto a destra, 2 = in basso a destra, 3 = in basso a sinistra, anche se la slice è ruotata). Per le slice poligonali sono N punti.
- `steps[i]`: id delle slice accese nel passo `i`. La lunghezza dell'array è il numero di passi (1–1024).
- `screens[]` ha anche `width`/`height` dell'uscita; `slices[]` ha anche `outPoints` (dove la slice finisce sull'uscita). Servono solo all'export delle mappe.
- `views`: la view `all` esiste sempre, è la prima e ha `hidden` (slice nascoste solo nell'anteprima dell'editor: per i plugin `all` vuol dire tutte le slice). Le altre view hanno `visible`: le slice che includono. Le slice nuove non ci sono, quindi restano nascoste finché non le accendi.
- `factory`: impostazioni della Factory usate per generare la sequenza (`null` se fatta a mano). I plugin lo ignorano.

## `envelopes.json` — envelope condivisi fra progetti

```json
{
  "format": "stilletchaser-envelopes",
  "version": 1,
  "envelopes": [
    {
      "id": "e-1",
      "name": "S Curve",
      "points": [ { "x": 0, "y": 1, "in": [0, 0], "out": [0.2, 0] } ],
      "lut": [ 1.0, 0.99, "... 256 valori ..." ]
    }
  ]
}
```

- `points`: punti di ancoraggio con maniglie Bézier relative (`in`, `out`), x e y fra 0 e 1. Servono all'editor.
- `lut`: la curva campionata in 256 valori su x = 0…1. I plugin usano solo questa.

## `stlt-vj-stage.json` — le slice di STLT-VJ

Lo scrive STLT-VJ (nella cartella dati) ogni volta che cambia il suo Advanced Output; l'editor aperto da STLT-VJ (`--stlt-vj <file>`) lo segue come segue `AdvancedOutput.xml` per Resolume. Le maschere non ci sono.

```json
{
  "format": "stlt-vj-chaser-stage",
  "version": 1,
  "compWidth": 1920,
  "compHeight": 1080,
  "screens": [ { "id": "screen-1", "name": "Screen 1", "width": 1920, "height": 1080 } ],
  "slices": [
    { "id": "9d8e020b4521024b", "name": "Slice 1", "screen": "screen-1",
      "points": [ [0, 0], [960, 0], [960, 1080], [0, 1080] ],
      "outPoints": [ [0, 0], [960, 0], [960, 1080], [0, 1080] ] }
  ]
}
```

- `id`: fisso per ogni slice di STLT-VJ, anche se la sposti o la rinomini.
- `points`: l'area di input in pixel della composizione; `outPoints`: dove finisce sull'uscita, in pixel dello schermo.
