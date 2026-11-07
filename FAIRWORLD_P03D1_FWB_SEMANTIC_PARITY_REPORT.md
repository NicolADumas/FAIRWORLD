# FAIRWORLD P03d.1 — FWB Semantic Parity & Project Persistence

## Esito sintetico

**Serializzazione JSON/FWB v8 e compatibilità v7: PASS sui test mirati.** La causa del difetto P03d era la perdita, nel formato FWB v7, di `MapRegion.influence`, `MapRegion.overrides` e dei campi persistenti del terreno planetario. Se `WorldProjectManager` selezionava il binario v7 come cache più recente, il documento runtime divergeva dal JSON e il TerrainSolver poteva produrre BlockID diversi.

**Gate complessivo: NO-GO per considerare chiusa tutta la regressione P03d.** Il full runner Release continua a fallire su mesher e locomozione; il test runtime del fixture P03d fallisce due asserzioni non legate alla parità seriale, identiche nei rami JSON e FWB. La parità semantica è dimostrata separatamente.

Nessuna prova GUI/GPU è stata eseguita. Non è stata stabilita alcuna causa per i blocchi bianchi.

## 1. Safety gate e working tree

Il working tree è DIRTY e contiene numerose modifiche e file non tracciati precedenti a questa fase. Non sono stati eseguiti reset, clean, checkout distruttivi, commit, push o riconfigurazioni CMake. I progetti e fixture sorgente sono stati letti; gli output dei test sono stati creati nel sandbox `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`.

Per questa fase i file sorgente modificati sono:

- `src/world/MapDocument.cpp`
- `src/world/terrain/validation/TerrainValidation.cpp`
- `src/world/terrain/validation/TerrainValidation.h`

È stato creato questo rapporto. Le modifiche preesistenti in altri file sono state preservate.

**Eccezione di integrità da segnalare:** un primo tentativo MSBuild dell'Editor con `OutDir` sandbox ha comunque scritto il PDB nella destinazione hardcoded del progetto, aggiornando `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.pdb` (dimensione osservata 490.795.008 byte; timestamp osservato 06/11/2026 20:12:16). Non era disponibile un hash/backup precedente e il file non è stato ripristinato né rimosso. Le build successive del core e dei runner hanno usato destinazioni sandbox. Questo artefatto di build va considerato una variazione introdotta durante la validazione, distinta dai diff preesistenti.

`git diff --check` sui tre sorgenti sopra: **exit 0**, nessun errore; Git segnala solo che il file header LF potrebbe essere convertito in CRLF. Il controllo globale del repository trova anche whitespace error preesistenti in `src/states/BlockMakerState.cpp`, non modificato da questa fase.

## 2. Root cause e matrice dei campi

Legenda: ✓ persistito e ricaricato; — assente/non applicabile; default v7 indica il valore iniziale della struct quando il vecchio layout non contiene il campo.

| Campo | JSON writer / reader | FWB v7 | FWB v8 | Valore runtime / nota |
|---|---|---|---|---|
| Template id, name, PlanetSize, baseType, baseAngularRadius, seed | ✓ / ✓ | ✓ / ✓ | ✓ / ✓ | Definiscono identità e parametri base del template |
| Template baseRules (Height/Layers/Caves/Water/Biome/Erosion) | ✓ / ✓ | JSON serializzato / ✓ | JSON serializzato / ✓ | Le regole determinano il contenuto generato |
| SubRegion e Planet Region: eulerAngles, angularRadius, rectMin/Max, type, shape, label, optional seed | ✓ / ✓ | ✓ / ✓ | ✓ / ✓ | Geometria/ambito e classificazione della regione |
| SubRegion e Planet Region: influence | ✓ / ✓ | — (default struct 1.0) | ✓ / ✓, estensione v8 | Peso della regione nel resolver |
| SubRegion e Planet Region: overrides | ✓ / ✓ | — (overrides vuoti) | ✓ / ✓, JSON per regione | Override parziali di regole, inclusi layer e BlockName |
| SubRegion e Planet Region: isGridAligned, faceIndex, gridX, gridY | ✓ / ✓ | ✓ / ✓ | ✓ / ✓ | Coordinate/orientamento della regione |
| SubRegion e Planet Region: isBackgroundFill | ✓ / ✓ | — (false) | ✓ / ✓, estensione v8 | Semantica di generazione sparsa |
| Planet type/name/PlanetSize/isFlat, axialTilt/yearLength, min/max bounds | ✓ / ✓ | ✓ / ✓ | ✓ / ✓ | Metadati e limiti del pianeta |
| Planetary relief scale | ✓ / ✓ | — (default struct) | ✓ / ✓, estensione v8 | Scala del rilievo procedurale |
| Planet base terrain: biome/baseRules/seed | ✓ / ✓ | — (default struct) | ✓ / ✓, estensione v8 | Terreno base planetario |
| Planet `resolvedSurfaceBlock` | non persistito | non persistito | non persistito | Campo derivato dal registry; ricostruito/runtime, non autoritativo |
| Chunk instances: name/templateId/eulerAngles/angularRadius/grid flag/face/grid/isActive | ✓ / ✓ | ✓ / ✓ | ✓ / ✓ | Mantiene riferimento al template e collocazione |
| Chunk overrides: coordinate x/z, metadata type/biome/spawn/destructible | ✓ / ✓ | ✓ / ✓ | ✓ / ✓ | Override documentali legacy |
| Spawn points: name/face/localX/localZ/height/color | ✓ / ✓ | ✓ / ✓ | ✓ / ✓ | Coordinate e orientamento spawn |
| `isCompiled` | ✓ / ✓ | ✓ / ✓ | ✓ / ✓ | Stato del documento |

I record FWB v7 conservavano già le regole base dei template, le coordinate delle regioni, le istanze, gli spawn e i chunk overrides; il gap dimostrato era nei campi omessi sopra. In particolare, la lettura v7 inizializzava `influence` al default `1.0` e lasciava vuoti gli `overrides`, mentre il JSON manteneva i valori espliciti. Anche i campi planetari aggiunti a v8 non esistevano nel layout v7.

## 3. Layout FWB v8 e compatibilità

Il magic resta `FWB1`; la versione passa da 7 a 8.

- Il prefisso di ogni `MapRegion` mantiene il layout v7, poi v8 aggiunge `float influence`, `string overridesJson` e `bool isBackgroundFill`.
- Dopo i bounds planetari, v8 aggiunge `float planetaryReliefScale`, `int32 biome`, `string baseRulesJson`, `uint32 seed`; poi seguono i record planet region, istanze, chunk overrides e spawn nel layout esistente.
- Le altre regole già rappresentate tramite JSON, gli ID, gli ordinamenti e la semantica delle coordinate restano invariati.
- Il loader mantiene un ramo esplicito v7: legge esattamente il vecchio layout e applica i default dei campi assenti. Nessun byte v7 viene reinterpretato come estensione v8.
- `LoadSmart` tratta il JSON sidecar come fonte semantica completa quando trova una cache v7, anche se il binario è più recente, poi rigenera la cache nel formato corrente. Un FWB v7 autonomo resta leggibile per compatibilità, ma naturalmente non può ricostruire i campi che il suo layout non conteneva.
- Il caricamento binario ora legge in un documento temporaneo e lo assegna al documento attivo solo dopo il parsing completo. File troncati, versioni non supportate, JSON annidato malformato, valori float non finiti, booleani non canonici, conteggi oltre il limite e byte finali aggiuntivi vengono rifiutati senza lasciare un documento parzialmente sovrascritto.
- Limiti introdotti: stringhe fino a 16 MiB, conteggi di record fino a 1.000.000. Questi sono limiti difensivi, non una migrazione dei dati.

La compatibilità del v7 è quindi **read-compatible**, mentre la parità dei campi non rappresentabili viene recuperata dal sidecar JSON quando disponibile. Il loader non riscrive il file sorgente v7 standalone; la cache è rigenerata soltanto nel normale percorso `LoadSmart` con JSON associato.

## 4. Modifiche

`MapDocument.cpp` ora scrive/legge i campi JSON omessi per le regioni e il terreno planetario; scrive FWB v8 e legge sia v7 sia v8; rifiuta input binari incompleti/corrotti senza accettazione silenziosa; e preferisce il JSON semanticamente completo a una cache v7 incompleta.

`TerrainValidation.cpp/.h` aggiungono il gate `TestP03D1FwbSemanticParity` al percorso di validazione D1.2. Il test usa una directory univoca sotto la working directory corrente e copre regole annidate, più sottoregioni, influence diverse, override, layer/BlockName, campi opzionali, coordinate, regioni planetarie, istanze, spawn, chunk overrides, v7, cache v7 più JSON, file troncato e versione 999. Gli output di test sono conservati nel runtime sandbox.

Il test verifica anche che `resolvedSurfaceBlock` resti derivato e non venga serializzato come dato autoritativo.

## 5. Evidenze e risultati

Tutti i comandi riportati sono stati eseguiti con output e prodotti in `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`, salvo il PDB indicato nel safety gate.

| Gate | Comando/artefatto | Risultato | Exit code |
|---|---|---:|---:|
| Diff check mirato | `git diff --check -- src/world/MapDocument.cpp src/world/terrain/validation/TerrainValidation.cpp src/world/terrain/validation/TerrainValidation.h` | PASS | 0 |
| Build Debug core | MSBuild `build_r3z7\FairworldCore.vcxproj`, Debug x64, bin/obj nel runtime sandbox | PASS | 0 |
| Build Release core | MSBuild `build_r3z7\FairworldCore.vcxproj`, Release x64, bin/obj nel runtime sandbox | PASS | 0 |
| Build Debug full runner | target runner in build tree esistente, bin/obj sandbox | PASS | 0 |
| Build Release full runner | target runner in build tree esistente, bin/obj sandbox | PASS | 0 |
| Build editor/link finale | Editor relink contro la libreria core Debug sandbox | PASS | 0 |
| `--validate-d12` finale | `FairworldEditor.exe --validate-d12` dalla working directory sandbox | PASS, incl. nuovo P03d.1, D1.2 chart/grid/spawn | 0 |
| JSON/FWB/WPM round-trip | `p03d1-roundtrip-run.log` | JSON load semantic=1; FWB load semantic=1; WPM Save/Load semantic=1 | 0 |
| Full runner Debug, fixture Tiny originale in sola lettura | `p03d1-full-runner-debug.log` | FAIL complessivo: mesh PASS; fail locomozione body/Transform/camera | 1 |
| Full runner Release, fixture Tiny originale in sola lettura | `p03d1-full-runner-release.log` | FAIL: mesh contract; fallimento locomozione radiale | 1 |
| Fixture P03d custom, Debug FWB vs JSON | `p03d1-runtime-fwb-debug.log`, `p03d1-runtime-json-debug.log` | Parità tra i due ingressi; entrambi runner FAIL su due assertion indipendenti | 1 ciascuno |
| Fixture P03d custom, Release FWB vs JSON | `p03d1-runtime-fwb-release.log`, `p03d1-runtime-json-release.log` | Parità tra i due ingressi; entrambi runner FAIL sulle stesse due assertion indipendenti | 1 ciascuno |
| GUI/GPU | non eseguita per vincolo | NOT VERIFIED | — |

### Round-trip runtime del fixture P03d

Per ogni configurazione, il documento selezionato dal ramo FWB e quello selezionato dal ramo JSON hanno generato lo stesso output osservato:

| Configurazione | Chunk | Voxel non-Air | BlockID 2 | BlockID 3 | Mesh CPU / triangoli |
|---|---:|---:|---:|---:|---:|
| Debug FWB | 54 | 508.717 | 2.048 | 506.669 | 54 / 99.388 |
| Debug JSON | 54 | 508.717 | 2.048 | 506.669 | 54 / 99.388 |
| Release FWB | 54 | 508.720 | 2.048 | 506.672 | 54 / 99.392 |
| Release JSON | 54 | 508.720 | 2.048 | 506.672 | 54 / 99.392 |

Nel fixture P03d, i **2.048 voxel BlockID 2 sono presenti sia dopo caricamento binario sia JSON**, e i conteggi per ciascuna configurazione coincidono. Il custom runner riporta gli stessi due fallimenti in tutti e quattro i run: `shared production CPU voxel mesh contract` e `solid voxel sample increments contact count and permits Grounded`. Questi FAIL impediscono di etichettare PASS il runner del fixture, ma non mostrano una divergenza JSON/FWB.

Sul Tiny standard, i log riportano Debug 504.877 voxel non-Air e 99.252 triangoli; Release 504.879 voxel e 99.256 triangoli. Il runner Release fallisce anche il test di locomozione radiale. Queste divergenze sono rimaste aperte e non sono state nascoste aggiornando hash o aspettative.

## 6. Regressioni e interpretazione

- **Dimostrato PASS:** round-trip semantico JSON↔FWB v8; compatibilità di lettura FWB v7; fallback al sidecar JSON quando una cache v7 è presente; rifiuto di FWB troncato/versione incompatibile senza perdita dello stato caricato; parità dei conteggi BlockID JSON/FWB nel fixture P03d, incluso ID 2 = 2.048.
- **FAIL indipendenti dalla serializzazione:** full runner Debug (locomozione/ancoraggio body-Transform-camera); full runner Release (contratto mesher e locomozione); custom runner del fixture P03d (le due assertion sopra falliscono in modo identico per JSON e FWB). Non attribuiti a questa patch senza evidenza causale.
- **NOT VERIFIED:** mesh GPU, rendering e spiegazione dei blocchi bianchi. La sessione non ha testato Vulkan né il comportamento visivo.
- La parità Debug/Release globale non è ottenuta: gli output del fixture differiscono di 3 voxel ID 3 e 4 triangoli. L'uguaglianza richiesta JSON/FWB è invece rispettata all'interno di ciascuna configurazione.

## 7. Rischi residui

1. Campi FWB v7 non rappresentabili restano irrecuperabili da un binario v7 standalone privo del JSON sidecar; vengono applicati i default strutturali. Il comportamento è esplicito e compatibile, ma non può ricostruire dati già persi.
2. Il layout v8 è versionato ma non include checksum o scrittura atomica del file; questi aspetti non erano necessari per la parità richiesta e restano fuori perimetro.
3. `SaveJSON` e `SaveBinary` sono ancora percorsi distinti e v8 include JSON serializzato per regole/override annidati; ulteriori evoluzioni degli struct serializzati richiedono test e revisione della versione.
4. Rimangono i noti FAIL Release/locomozione e l'anomalia PDB del safety gate.
5. Materiali bianchi e GPU rendering non sono stati investigati in questa fase.

## 8. Decisione

**P03d.1 serializzazione: PASS mirato.**

**Gate complessivo P03d.1: NO-GO per chiusura/freeze**, perché i runner completi e le regressioni Release non sono tutti PASS e GPU/GUI non sono verificate.

**GO condizionato per P03d.2** limitatamente all'isolamento/risoluzione delle divergenze già note di mesher e locomozione, mantenendo il perimetro separato dalla serializzazione. La causa dei blocchi bianchi resta una domanda aperta e richiede una fase diagnostica dedicata. Nessun P04 avviato.
