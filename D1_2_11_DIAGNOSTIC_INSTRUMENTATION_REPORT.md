# FAIRWORLD — D1.2.11 Temporary Diagnostic Instrumentation Report

**Data:** 2026-11-04 | **Stato:** strumentazione diagnostica compilata e test D1.2 PASS; GUI non avviata.

## Riepilogo

Le aggiunte diagnostiche sono limitate a `PlanetMapperCamera.cpp` e al ramo di teleport UI in `PlayState.cpp`. Non cambiano la query raycast, `m_lastRayHit`, le formule, i dati spawn, le assegnazioni di teleport o i serializer. Build Debug completa e `--validate-d12` sono PASS. La GUI non è stata avviata, quindi i nuovi messaggi raycast/teleport non sono ancora stati osservati a runtime.

Nessun commit o push. Nessun salvataggio originale è stato modificato; il verificatore D1.2.8 conferma `ORIGINAL_INPUT_HASHES_MATCH=40`.

## A — Modifiche temporanee

### `src/states/components/PlanetMapperCamera.cpp`

Nel ramo esistente `hit.hit && hit.type == RaycastHitType::Voxel`, immediatamente dopo `DirectionToFaceUV`, è stato aggiunto un blocco delimitato da commenti `D1.2.11 TEMP DIAGNOSTICS BEGIN/END`.

Per risolvere `N`, il log usa prima `BiomeDataComponent.planetSize` del chunk colpito (`sizeSource=hit-chunk`). Se il target non ha quel componente, usa il primo pianeta del documento (`first-planet-fallback`); se non c’è documento disponibile, usa Medium (`default-fallback`). La provenienza compare nel log per rendere visibile il fallback. Il caso normale di raycast voxel dovrebbe usare il componente del chunk bersaglio.

Con `CubeSphereMapping::FaceUVToChartCoordinates` e `UVToGrid` il blocco ricava chart e cella. Stampa una riga `[D1.2.11][RAYCAST]` quando cambia la combinazione `(face, N, col, row)`, con:

- `camPos`, direzione centrale `camDir` e direzione del raggio puntato `rayDir`;
- `world`, `faceNormal` restituita dal raycast;
- `face`, `uv`, `chart`, `N`, `col`, `row`, `sizeSource`.

Il deduplicatore è composto da quattro variabili statiche diagnostiche. Non viene assegnato a `m_lastRayHit` e non influenza hit, selezione o pulsante spawn. La query e l’assegnazione originale `m_lastRayHit = hit` sono invariate.

### `src/states/PlayState.cpp`

Nel ramo esistente del pulsante spawn del pannello Teletrasporto, il blocco marcato registra, per ogni entità giocatore trovata:

- posizione `RigidBody` precedente;
- `faceIndex`, coordinate locali `localX/localZ`, chart normalizzato e `heightOffset` dello spawn;
- posizione target `worldPos` già calcolata dal codice esistente;
- posizione `RigidBody` successiva;
- delta `next - previous` e `glm::length(delta)`.

Il messaggio ha prefisso `[D1.2.11][TELEPORT]`. Formule e assegnazioni preesistenti restano invariate.

**Nota sul diff preesistente:** `PlayState.cpp` era già modificato prima di D1.2.11 per la patch D1.2 (uso di `FaceChartToDirection` e include `CubeSphereMapping.h`). Non ho rimosso né riformattato quelle modifiche; le sole aggiunte di questa fase nel file sono i blocchi delimitati D1.2.11.

## B — Isolamento

- Eseguibile testato: `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe`.
- Working directory di `--validate-d12`: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`.
- `TEMP` e `TMP` del solo processo di test: `runtime\temp_d12`; al termine la directory non conteneva file.
- La modalità `--validate-d12` esegue `TerrainValidation::RunD12ConventionTests()` e ritorna prima del bootstrap dell’editor e del caricamento dei percorsi di gioco. I file temporanei dei test sono stati comunque confinati con `TEMP/TMP`.
- Verifica degli input originali prima della build/test: `ORIGINAL_INPUT_HASHES_MATCH=40`. Verifica ripetuta dopo il test: `ORIGINAL_INPUT_HASHES_MATCH=40`, exit code 0.
- La build ha scritto gli artefatti Debug previsti in `build_r3z7` e `bin\Debug`; non è stata avviata l’applicazione in modalità interattiva e non sono stati scritti salvataggi runtime.
- La build Visual Studio ha eseguito i suoi post-build `copy_directory` degli asset in `bin\Debug\assets`. Sono output di packaging sotto la directory binaria; non sono state copiate/modificate le directory `saves` originali.

## C — Validazione effettiva

Toolchain esistente: CMake `4.3.1-msvc1`, generatore Visual Studio 18 2026, cache `build_r3z7`, MSBuild `18.10.1`, MSVC `14.51.36231`. Non è stato cambiato generatore o cache.

| Passo | Comando / esecuzione | Risultato |
|---|---|---|
| Hash originali pre-run | `D1_2_8_GUI_SANDBOX\Verify-OriginalInputs.ps1` | **PASS**, 40/40, exit 0 |
| `git diff --check` sui due file sorgente | `git diff --check -- src/states/components/PlanetMapperCamera.cpp src/states/PlayState.cpp` | **PASS**, exit 0. Git ha emesso solo l’avviso di conversione LF→CRLF al prossimo aggiornamento del file; nessun whitespace error. |
| Build Debug completa | `cmake --build build_r3z7 --config Debug --parallel 1 --verbose` (CMake assoluto da `D:\Visual Code\vs\...`) | **PASS**, `ALL_BUILD`, exit 0, 0 warning e 0 errori, durata MSBuild `00:01:55.90`. |
| Test D1.2 | da CWD sandbox: `FairworldEditor.exe --validate-d12` | **PASS**, exit 0, durata misurata `0.1396 s`. Quattro gruppi PASS. |
| Hash originali post-run | `D1_2_8_GUI_SANDBOX\Verify-OriginalInputs.ps1` | **PASS**, `ORIGINAL_INPUT_HASHES_MATCH=40`, exit 0 |
| GUI completa | Non avviata, come richiesto | **NOT RUN** |

Output D1.2 registrato in `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_11_validate_d12.log`:

```text
UV/chart/direction round-trip, faces, edges, corners and ties [PASS]
Six sizes, valid rows, cell centers, endpoints and U/V nextafter boundaries [PASS]
Single row conversion through TerrainSolver and mesh mapping [PASS]
Legacy spawn JSON/binary values and direction preserved [PASS]
D1.2 STATUS: PASS
VALIDATE_D12_EXIT_CODE=0
```

I quattro risultati e lo stato corrispondono all’ultimo log D1.2 PASS già presente (`D1_2_5_validate_d12.log`). `--validate-d12` non crea una finestra né esegue PlanetMapperCamera/PlayState; dimostra la non regressione dei test matematici, ma non costituisce un test runtime dei messaggi diagnostici.

La prima build ha segnalato un tipo incompleto (`fw::WorldProjectManager`) nel nuovo log diagnostico. È stato aggiunto l’include esplicito `WorldProjectManager.h` in `PlanetMapperCamera.cpp`; la build completa successiva è PASS. Log della prima compilazione: `build_r3z7\D1_2_11_full_debug_build.log`; log della build conclusiva: `build_r3z7\D1_2_11_full_debug_build_attempt2.log`.

## D — Costi e limiti

- Raycast: una lettura del componente hit, conversione chart e `UVToGrid` per hit voxel; scrittura stdout soltanto quando cambia faccia, risoluzione o cella. Nessun log per frame per una cella stabile.
- Teleport: un vettore differenza, una lunghezza e una riga stdout solo al click e per ogni entità giocatore presente.
- `hit.faceNormal` viene stampata esattamente come restituita da `RaycastSystem`. Nel percorso voxel è la normale discreta del voxel; non va interpretata automaticamente come normale radiale planetaria. Per valutare la faccia, `faceIndex` e UV derivano dal mapping della posizione mondo.
- Se il chunk non espone `BiomeDataComponent`, il valore N deriva dal primo pianeta (o dal fallback Medium); `sizeSource` permette di rilevarlo. Nel caso di documenti multi-pianeta e fallback, N potrebbe non corrispondere al pianeta selezionato: il log non nasconde questa limitazione.
- Il log di teleport misura la posizione `RigidBody`, non la quota `eyeOffset` della trasformazione camera.
- La GUI non è stata avviata, quindi non esistono ancora evidenze su numero di righe prodotte, acquisizione di sei facce o delta teleport a runtime.

## E — Inventario e rollback

Modifiche sorgente effettuate in questa fase:

1. `src/states/components/PlanetMapperCamera.cpp`: tre include per accesso read-only ai dati planetari/chunk e il blocco diagnostico marcato.
2. `src/states/PlayState.cpp`: due blocchi diagnostici marcati nel ramo del pulsante teleport. Il file conteneva già modifiche D1.2 preesistenti.

Artefatti/log di validazione creati:

- `build_r3z7\D1_2_11_full_debug_build.log` (primo tentativo, compilazione non riuscita per header incompleto);
- `build_r3z7\D1_2_11_full_debug_build_attempt2.log` (build completa PASS);
- `D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_11_validate_d12.log`;
- directory `D1_2_8_GUI_SANDBOX\runtime\temp_d12` (vuota dopo il test);
- questo rapporto.

Rollback autorizzato e delimitato: rimuovere i blocchi compresi tra `D1.2.11 TEMP DIAGNOSTICS BEGIN/END` in entrambi i sorgenti e gli include `GameWorld.h`, `WorldProjectManager.h`, `BiomeComponents.h` aggiunti in `PlanetMapperCamera.cpp`. In `PlayState.cpp` conservare l’include e le conversioni `CubeSphereMapping` già presenti per D1.2. Non ripristinare l’intero file a HEAD perché eliminerebbe modifiche preesistenti non appartenenti a questa strumentazione. Nessun rollback dei salvataggi è necessario o va eseguito.

**Stato:** strumentazione compilata e test D1.2 PASS; pronta per una successiva sessione GUI sandbox, ma non ancora verificata a runtime. D1 non è FROZEN.
