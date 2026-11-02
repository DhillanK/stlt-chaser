# STLT-CHASER – guida in italiano

Chase e screen bump sulle slice dell'Advanced Output di Resolume Arena: un editor per disegnare le sequenze e tre effetti FFGL che le suonano (STLT-CHASER, STLT-WIPER, STLT-OUTLINER).

![L'editor STLT-CHASER: uno stage LED dimostrativo con le slice del passo selezionato accese](images/editor.png)

## Installazione

1. Scarica l'ultima versione dalla pagina *Releases* del progetto: `STLT-CHASER-…-Windows-Setup.exe` per Windows, `STLT-CHASER-…-macOS.dmg` per Mac.
2. Windows: avvia il Setup. Se compare "Windows ha protetto il PC", scegli *Ulteriori informazioni → Esegui comunque* (il programma non è firmato digitalmente). Mac: apri il dmg e trascina STLT-CHASER in Applicazioni; la prima volta aprilo con *clic destro → Apri*.
3. Al primo avvio l'editor propone di installare i plugin in Resolume (*Extra Effects/STLT-CHASER*). Chiudi Resolume prima. Si può rifare in qualsiasi momento da *File → Install / update plugins*.
4. Riapri Resolume: trovi gli effetti STLT-CHASER, STLT-WIPER e STLT-OUTLINER.

## Uso

![La Factory: una sequenza a colonne generata dalla griglia sui centri delle slice](images/factory.png)

1. Prepara l'Advanced Output in Arena (Save & Close).
2. Apri l'editor STLT-CHASER. Al primo avvio carica da solo l'Advanced Output di Arena.
3. Crea le sequenze: scegli un passo in basso e clicca le slice da accendere. Salva da solo, a ogni modifica.
4. In Arena aggiungi l'effetto **STLT-CHASER** a un layer, un gruppo, una clip o alla composizione.
   - *Driver*: **Chaser** accende le slice seguendo la sequenza; **None** le tiene tutte accese (per usare solo i moduli, per esempio il Delayer).
   - **Sequences**: un pulsante per ogni sequenza scritta nell'editor, col suo nome; quella che suona ha ▶ davanti. Mappa i pulsanti su pad MIDI o tasti per cambiare sequenza dal vivo.
   - *Step*: anima questo parametro (BPM Sync, Timeline o FFT): 0 → 1 attraversa tutta la sequenza.
   - *Scaling*: Mask (ritaglia), Fill (adatta il contenuto a ogni slice, mantenendo le proporzioni), Stretch (lo deforma per riempirla).
   - *View*: limita l'effetto alle slice di una view.
   - *Echoes*: quanti passi in più resta acceso ogni passo, mentre sfuma.
   - *Envelope*: curva di dissolvenza disegnata nell'editor; con "Sustain / Release" usi i due cursori.
5. **Delayer** (spunta *Delayer* nel gruppo Delayer): ogni slice mostra il contenuto com'era un po' di tempo prima.
   - *Delay Sequence*: decide quanto ritarda ogni slice. Le slice del primo passo non ritardano, quelle dell'ultimo ritardano del massimo, quelle in mezzo in proporzione; le slice che la sequenza non accende non ritardano. Una sequenza Factory "Horizontal" fa scorrere il contenuto da sinistra a destra.
   - *Delay*: quanto del ritardo massimo usare (0–100 %), da animare dal vivo.
   - *Set Delay In*: Frames, Seconds o Beats; compare il relativo *Max Frames / Max Seconds / Max Beats*.
   - *Quality*: risoluzione dei fotogrammi memorizzati (75 % di default). Più bassa = meno memoria video.
   - *Reverse*: inverte i ritardi senza rifare la sequenza.
   - *Auto Reset* / *Reset After (min)*: dopo una pausa così lunga i fotogrammi vecchi vengono dimenticati.
   - Attenzione: ritardi lunghi usano molta memoria video (es. 2 battute a 120 BPM, 60 fps, 1920×1080 al 75 % ≈ 280 MB). Il plugin si limita a 3 GB.
   - Delayer e Chaser si sommano: con Driver Chaser vedi il contenuto ritardato solo nelle slice accese.
6. **Wiper** (spunta *Wiper* nel gruppo Wiper): wipe che scorrono nelle slice di una seconda sequenza (*Wipe Sequence*, *Wipe Step*, *Wipe Length*, *Wipe Overlap*, *Wipe Loops*, *Wipe Flip*, *Wipe Odd/Even*, *Wipe Swap*, *Wipe Envelope*), con il video o un colore pieno (*Wipe Source*). Chase e wipe si sommano: una slice si vede se la accende il chase oppure se ci passa un wipe, quindi un solo STLT-CHASER sulla composizione fa entrambe le cose.
7. **Outliner** (spunta *Outliner* nel gruppo Outliner): contorni sopra le slice, con gli stessi controlli di STLT-OUTLINER (*Outline Red / Green / Blue*, *Outline Width*, *Outline Feather*, *Outline Animate* e le relative *Phase*, *Fan*, *Randomise*, *Length*, *Count*, *Flip*, *Odd/Even*, *Envelope*).
   - *Outline Target*: *Lit Slices* (default) contorna solo le slice accese dal chase (seguendo la dissolvenza degli echi) o attraversate da un wipe; *All Slices* contorna tutte le slice della View.
   - *Outlines Only*: nasconde video e wipe e lascia solo i contorni.

## Effetti indipendenti

Usano lo stesso documento dell'editor, ma ognuno funziona da solo.

**STLT-WIPER**: ogni passo della sequenza fa scorrere una sfumatura dentro le sue slice.
- *Sequence*, *Step* (da animare come in STLT-CHASER), *Scaling* (Mask / Fill / Stretch).
- La direzione è automatica: le slice verticali vanno dal basso in alto; quelle orizzontali si allontanano dal centro del palco (a destra verso destra, a sinistra verso sinistra). Le slice ruotate seguono la loro rotazione.
- *Length*: lunghezza della sfumatura (frazione della slice). *Overlap*: il passo dopo parte prima che finisca quello prima (con Length corta e un po' di Overlap scorrono più sfumature insieme). *Loops*: quante volte la sequenza si ripete mentre Step va da 0 a 1.
- *Flip Direction* inverte tutte le direzioni, *Odd/Even* inverte i passi dispari, *Swap Directions* fa scorrere in orizzontale le slice verticali e viceversa.
- *Envelope*: forma della sfumatura (dalla testa alla coda); "Gradient" è una sfumatura lineare.
- *Source*: il video oppure un colore pieno (*Solid Color*, con Red / Green / Blue). *Show Original*: disegna i wipe sopra quello che arriva invece che su sfondo trasparente.

**STLT-OUTLINER**: contorni disegnati dentro ogni slice, così non sbordano nelle vicine.
- *View*: su quali slice. *Red / Green / Blue*: colore. *Width*: spessore in pixel. *Feather*: sfumatura verso l'interno (cornice / vignetta).
- *Animate*: i contorni diventano linee rastremate che girano lungo il bordo. *Phase* le muove (animalo con BPM Sync), *Fan* sfasa le slice in base alla distanza dal centro, *Randomise* le sfasa a caso, *Length* e *Count* lunghezza e numero di linee, *Flip Direction* a specchio (destra in senso orario, sinistra antiorario), *Odd/Even* inverte una slice sì e una no, *Envelope* la forma della coda.
- *Show Original* (acceso di default): disegna i contorni sopra quello che arriva dall'effetto precedente. Spento, restano solo i contorni su sfondo trasparente. Metti più Outliner uno sopra l'altro, con view e colori diversi, per combinarli.
- *Lit Slices Only*: contorna solo le slice dove arriva qualcosa, seguendone la dissolvenza. Messo dopo STLT-CHASER, contorna solo le slice che il chase sta accendendo.

## Combinare gli effetti

In Resolume gli effetti sono in catena: ognuno lavora su ciò che gli passa quello sopra.
- **STLT-CHASER → STLT-OUTLINER**: chase con contorni sopra; con *Lit Slices Only* solo sulle slice accese.
- **STLT-OUTLINER (Show Original spento) → STLT-CHASER**: i contorni fanno il chase.
- **STLT-CHASER → STLT-WIPER** (Source *Solid Color*, *Show Original* acceso): wipe colorati sopra il chase.
- **Chase, wipe e contorni su tutta la composizione**: un solo STLT-CHASER sulla Composition con i moduli **Wiper** e **Outliner** accesi. Sotto puoi cambiare layer e clip liberamente.
- *Scaling*: **Fill** o **Stretch** per clip a schermo intero; **Mask** per contenuti già distribuiti sul palco (uscita di un altro effetto STLT, layer mappati, la composizione).

Le modifiche fatte nell'editor arrivano in Arena entro mezzo secondo, anche con l'effetto già in uso.


## Editor: comandi

| Dove | Comando |
| --- | --- |
| Canvas | clic: accende o spegne la slice nel passo · trascina sulle slice: le inverte una per una |
| Canvas | riquadro: inverte · Shift: accende · Ctrl: spegne · Alt: nasconde nella view |
| Canvas | rotella o Ctrl +/−: zoom · Spazio o rotella premuta: sposta · ⛶: adatta |
| Passi | clic o ←/→: scegli · trascina su un altro passo: copia · trascina e tieni: copia ogni N passi |
| Passi | Alt+←/→: togli/aggiungi un passo · `→` ripete · `↔` ping-pong · `✖` svuota la sequenza |
| Passo corrente | I: inverti · Canc: svuota |
| Sequenze | ↑/↓: cambia · Alt+↓/↑: aggiungi/togli · Invio: play/stop |
| Liste (Sequences, Views, Envelopes) | rinomina: doppio clic sul nome, ✎ o F2 · ☰ trascina per riordinare, Alt per copiare |
| Ovunque | Ctrl+Z / Ctrl+Y: annulla / ripeti |

## File

- Documento di lavoro, letto dai plugin: `current.stchase` in `%APPDATA%Stilletchaser` (Windows) o `~/Library/Application Support/Stilletchaser/` (Mac)
- Envelope, condivisi fra progetti: `envelopes.json` nella stessa cartella
- *File > Save As* salva anche una copia con nome; da quel momento l'editor aggiorna anche quella.

## Compilare

Vedi la sezione *Building from source* del README (in inglese).
