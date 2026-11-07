# FAIRWORLD — D1.2.5 Deterministic UV Grid Boundary Fix

**Esito:** fix UV/grid e test mirati **PASS**. La suite automatica è **FAIL / interrotta** per un errore in `FairworldScientificTests`; i controlli GUI non sono stati avviati. D1 non è FROZEN.

## 1. Analisi numerica prima della modifica

`CubeSphereMapping::UVToGrid()` riceve `glm::vec2` con componenti `float` e `resolution` come `int`. L’implementazione precedente calcolava `uv.component * resolution`, convertiva il prodotto a `int` (troncamento verso zero) e poi applicava `std::clamp` all’intervallo `[0, N-1]`.

Per i sei casi Huge (`N=41`), il float prodotto dalla stessa espressione usata dai chiamanti, `float(k)/float(41)`, è sotto il razionale matematico esatto. Anche il prodotto `uv*41` arrotonda sotto `k`; il cast intero sceglie quindi la cella precedente.

| k | float `k/41` | bit binary32 | `uv*41` | bit del prodotto | differenza float-rationale esatto |
|---:|---:|---|---:|---|---:|
| 1 | `0.024390243` | `0x3CC7CE0C` | `0.99999994` | `0x3F7FFFFF` | `-9.086073907149661e-10` |
| 2 | `0.048780486` | `0x3D47CE0C` | `1.9999999` | `0x3FFFFFFF` | `-1.8172147814299322e-9` |
| 4 | `0.09756097` | `0x3DC7CE0C` | `3.9999998` | `0x407FFFFF` | `-3.6344295628598644e-9` |
| 8 | `0.19512194` | `0x3E47CE0C` | `7.9999995` | `0x40FFFFFF` | `-7.268859125719729e-9` |
| 16 | `0.3902439` | `0x3EC7CE0C` | `15.999999` | `0x417FFFFF` | `-1.4537718251439458e-8` |
| 32 | `0.7804878` | `0x3F47CE0C` | `31.999998` | `0x41FFFFFF` | `-2.9075436502878915e-8` |

La verifica read-only con `std::nextafter` ha mostrato che il predecessore float ha prodotto un indice `k-1`; il confine rappresentabile e il successore, usando il vecchio `uv*N` e troncamento, hanno prodotto rispettivamente `k-1` e `k`. Il log completo dei valori float è conservato nella cronologia della sessione; i bit e i delta sono riportati qui.

## 2. Contratto adottato

Per ciascun asse e risoluzione positiva `N`, i confini sono i float rappresentabili calcolati con la stessa formula `float(k)/float(N)`, per `k=1..N-1`:

- `nextafter(boundary, 0)` appartiene alla cella `k-1`.
- `boundary` appartiene alla cella `k`.
- `nextafter(boundary, 1)` appartiene alla cella `k`.
- Gli endpoint UV restano clampati alle celle `0` e `N-1`.
- U e V sono discretizzati indipendentemente. Nessun epsilon o offset agli UV viene applicato.

Questo contratto risolve il tie-break rispetto al confine **rappresentabile** `float(k)/float(N)`; non tenta di trattare tutti i float vicini al razionale matematico come se fossero esattamente uguali a esso.

## 3. Modifiche effettive

### `src/world/CubeSphereMapping.cpp`

Solo `UVToGrid()` è stato modificato. Per ogni asse usa una ricerca binaria delle soglie rappresentabili `float(k)/float(N)`: se `coordinate >= boundary`, la ricerca continua nella cella successiva; il valore restituito è l’ultima soglia non superiore alla coordinata, limitato a `[0,N-1]`.

La ricerca è `O(log N)`, senza epsilon, offset globale o conversione del prodotto `uv*N` a intero. Gli assi X/U e Y/V seguono lo stesso helper locale. Orientamenti, chart, righe legacy/canoniche, coordinate planetarie e formati persistenti non sono stati cambiati.

### `src/world/terrain/validation/TerrainValidation.cpp`

Estesi i regressioni di `TestD12GridRowsAndBounds()` mantenendo le asserzioni D1.2 esistenti. Per ogni `N=3,5,7,11,21,41` verificano:

- Tutti i confini interni `k/N`.
- `std::nextafter` immediatamente sotto e sopra ogni confine, separatamente per U e V.
- Centro di ogni cella e invarianza dell’asse ortogonale nei casi di bordo.
- Endpoint U e V a zero e uno, oltre ai controlli diagonali preesistenti.
- Limiti delle righe, adapter legacy/canonico e rifiuto degli input invalidi.

Le diagnostiche di errore stampano UV con precisione round-trip e bit esadecimali, dimensione, risoluzione, asse, indice atteso/effettivo e differenze discrete.

## 4. Validazione

### `git diff --check`

**PASS**, exit code `0`. Git ha emesso solo avvisi di normalizzazione LF/CRLF su altri file già modificati nel working tree.

### Build Debug completa

Comando:

```powershell
& 'D:\Visual Code\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build 'build_r3z7' --config Debug --parallel 1 --verbose
```

**PASS**, exit code `0`, durata MSBuild `00:01:45.75` (wall clock PowerShell `00:01:46.838`), zero warning e zero errori. Il target `ALL_BUILD` e i target editor/test/moduli sono terminati. Log completo: [D1_2_5_full_debug_build.log](/D:/FAIRWORLD/FAIRWORLD/build_r3z7/D1_2_5_full_debug_build.log).

### Test D1.2

Comando eseguito dalla root del progetto:

```powershell
& '.\bin\Debug\FairworldEditor.exe' --validate-d12
```

**PASS**, exit code `0`, durata `0,099 s`:

```text
UV/chart/direction round-trip, faces, edges, corners and ties [PASS]
Six sizes, valid rows, cell centers, endpoints and U/V nextafter boundaries [PASS]
Single row conversion through TerrainSolver and mesh mapping [PASS]
Legacy spawn JSON/binary values and direction preserved [PASS]
D1.2 STATUS: PASS
```

Log: [D1_2_5_validate_d12.log](/D:/FAIRWORLD/FAIRWORLD/D1_2_5_validate_d12.log).

## 5. Suite automatica

Il progetto abilita CTest in `CMakeLists.txt` e registra 15 test; è stato usato il runner CTest configurato nella stessa distribuzione CMake:

```powershell
& 'D:\Visual Code\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' `
  --test-dir 'build_r3z7' --build-config Debug --output-on-failure --parallel 1
```

Il test `FairworldScientificTests` è fallito: **37/38** test interni, fallimento `EXT-10 — Serializzazione deterministica JSON v3`, con `okSave1=1, okLoad=1, okSave2=1, identical=0`. CTest aveva già riportato PASS per i test 1–2 (`FairworldUVTests`, `FairworldSculptTests`) e 4–9 (`FairworldEventTests`, `FairworldNoiseTests`, `FairworldAsyncInputTests`, `FairworldMemoryTests`, `FairworldGameplayStressTests`, `FairworldDiagnostics`). Aveva iniziato il test 10 (`FairworldLifecycleTests`) quando il runner è stato interrotto. Perciò CTest è **FAIL / interrotto**, exit code osservato `1`; non è disponibile un riepilogo finale per gli altri test.

Il fallimento è in una suite del Scientific Material Registry, fuori dai due file modificati in questa patch. Non è stato investigato né corretto; non vi sono evidenze sufficienti per classificarlo come preesistente o come regressione di questa build. Log CTest fino all’interruzione: [D1_2_5_automatic_suite.log](/D:/FAIRWORLD/FAIRWORLD/D1_2_5_automatic_suite.log).

## 6. GUI e stato

GUI Planet Mapper / PlayState: **non eseguite**, perché la suite automatica ha fallito e la sequenza si è fermata. Non sono stati modificati salvataggi, effettuati commit o push, né toccati D2–D8.

**Raccomandazione:** fix matematico UV/grid e test mirati **PASS**; gate complessivo **FAIL** per `EXT-10` e suite interrotta. Prima della revisione finale restano da isolare il test JSON v3, completare la suite automatica e svolgere i controlli GUI richiesti. Nessuna correzione ulteriore è stata applicata. **D1 non è FROZEN.**
