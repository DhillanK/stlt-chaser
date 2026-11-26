# STLT-CHASER – design

An independent, from-scratch implementation of a slice "chaser" toolkit for Resolume Arena: an editor for step sequences over Advanced Output slices and FFGL effects that play them.
It is not affiliated with or endorsed by Resolume or by the makers of any other product.

## Architettura

```
 Arena  ──salva──▶  AdvancedOutput.xml
                       │ (letto all'avvio e quando cambia)
                       ▼
 STLT-CHASER Editor (Electron)  ──autosave──▶  documento .stchase (JSON)
                                                     │ (i plugin lo ricaricano quando cambia)
                                                     ▼
                                     Plugin FFGL in Resolume
```

- **Documento**: JSON con slice (id, nome, schermo, quad di input), sequenze (con le impostazioni della Factory), views, envelopes.
- **File corrente**: `current.stchase` nella cartella dati (vedi FORMAT.md). L'editor ci salva sempre in automatico; *Save As* crea copie.
- **Envelopes**: file condiviso fra progetti, `envelopes.json` nella cartella dati.
- **Sincronizzazione**: i plugin controllano la data di modifica del file circa due volte al secondo e ricaricano; i nomi delle sequenze/views/envelopes nei menu di Resolume si aggiornano da soli.
- **Identità delle slice**: l'`uniqueId` di Arena. Così un Relink conserva i passi anche se le slice cambiano posizione o nome.

## Editor (fase 1)

Layout come nei video: barra superiore (zoom %, adatta, mano, nome documento), menu laterale, canvas al centro, sequencer in basso.

**Canvas**
- Slice disegnate come in Arena; quelle accese nel passo corrente in arancione.
- Zoom con rotella o Ctrl +/−, campo percentuale, pulsante "adatta"; pan con spazio o strumento mano.
- Clic: inverte la slice. Trascinamento da una slice ("snake"): inverte quelle che attraversi.
- Riquadro di selezione: inverte; con Shift accende, con Ctrl spegne, con Alt nasconde nella view.

**Sequencer**
- Precedente / Play / Successivo, numero sequenza, nome modificabile.
- Passi da 1 a 1024 (+/−, campo numerico); per default 16. I passi 1, 5, 9… sono evidenziati come battiti.
- Clic o frecce ←/→ per selezionare il passo. Alt+←/→ aggiunge o toglie passi.
- Trascinare un passo su un altro lo copia; trascinare e tenere lo copia ogni N passi.
- `->` ripete i passi pieni su tutta la sequenza, `<->` li ripete in ping-pong, `X` svuota.
- Menu Edit: Inverti passo, Svuota passo.
- L'anteprima Play usa un BPM impostabile.

**Sequences** (pannello): lista illimitata; aggiungi, rinomina con doppio clic, riordina col trascinamento, duplica con Alt+trascina, elimina. ↑/↓ cambiano sequenza.

**Views** (pannello): "All" fissa più views personalizzate; lista schermi/slice con l'occhio. Shift+clic sull'occhio applica a un intervallo. Le slice nuove entrano nascoste nelle views personalizzate. Segnalazione delle sequenze che usano slice fuori dalla view.

**Factory**: schemi calcolati dalla posizione delle slice, senza griglia. Ogni schema dà a ogni slice un valore (x, y, distanza dal centro, angolo da ore 12, x+y); in Auto le slice si raggruppano finché il valore non salta di più di mezza slice tipica e il gruppo non supera una slice, con un numero fisso di passi lo schema si divide in tratti uguali (un vuoto sul palco diventa una pausa). Poi: One step / Build up, Straight / Odd-Even / There & back, Block, Modulo, Shuffle, e le slice spostate a mano (`moves`). Sul canvas ogni slice mostra il passo in cui si accende per la prima volta; un clic la sposta nel passo selezionato. *Create standard sequences…* crea una sequenza per schema.

**Envelopes**: lista (almeno una, massimo 512) ed editor di curve Bézier: doppio clic aggiunge o toglie un punto; maniglie collegate (Alt le separa); Shift blocca un asse; Ctrl disattiva lo snap; selezione multipla; coordinate X/Y del punto; annulla/ripeti; "Make Linear" sul segmento.

**File**: New (dall'AdvancedOutput.xml di Arena), Load, Save As, Relink; salvataggio automatico con indicatore; Export Input Map ed Export Output Map in PNG.

## Plugin

### STLT-CHASER (effetto unico)

Parametri sempre visibili:
- *Sequence*, *Step* (0–1, animabile con BPM Sync, Timeline o FFT di Resolume), *Scaling* (Fill / Mask / Stretch).
- *View* (limita le slice).
- *Driver*: Chaser / Bumper / Random / Wiper.
- *Echoes*, *Sustain*, *Release*, *Envelope* (Envelope sostituisce Sustain/Release; "None" usa loro).

Parametri del driver (visibili solo per il driver scelto):
- **Bumper**: Previous, Next, Blast, Blast Speed, Fade Speed, Auto Reset, Reset Delay.
- **Random**: Selection (All / Step), Speed, Max Slices.
- **Wiper**: Length, Overlap, Loops, Flip Direction, Odd/Even, Swap Directions.

Moduli che si spuntano (i loro parametri compaiono solo quando sono attivi):
- **Delayer**: Delay, unità Frames / Seconds / Beats, Quality, Reverse, Auto Reset.
- **Outliner**: Color, Width, Feather, Animate, Phase, Fan, Randomise, Length, Count, Flip, Odd/Even, Show Original.
- **Sampler**: Sample Mode (Center / Average / Blend), Color Mode (Color / Luminance).
- **Tiler**: Tile Size, Mode (Rainbow / Minimal / Texture / Matrix), Phase, Seed, Mirror, Randomize, Fan Comp, Length, Shape, Radius, Softness, Edge Only, Grid.

Ordine fisso di elaborazione: Delayer → Sampler/Tiler (contenuto) → Outliner → maschera del driver.

### Plugin separati, perché FFGL li richiede di tipo diverso
- **STLT-CHASER Source**: Outliner/Tiler come sorgente (clip generatore).
- **STLT-CHASER Blend**: mixer che applica il chase al risultato dell'effetto sottostante (equivalente delle versioni "Blend"), più la modalità Mask luminanza/alpha.

## Fasi
1. Editor completo + STLT-CHASER con driver Chaser, Scaling, Echoes, Sustain/Release, Envelope, View, sincronizzazione automatica. Prima di tutto: un prototipo che verifica che Arena 7.23 mostri e nasconda i parametri a runtime.
2. Driver Bumper, Random, Wiper; modulo Delayer.
3. Moduli Outliner, Sampler, Tiler; plugin Source e Blend.
