# FAIRWORLD — D1.2 Implementation Report

**Stato:** patch applicata; toolchain recuperata; validazione **FAIL** per errori di compilazione nei test D1.2.  
**Decisione:** fermarsi senza correzioni automatiche; non dichiarare D1.2 completata né D1 FROZEN.  
**Commit/push/migrazioni:** nessuno.

## 1. File modificati e motivazione

| File | Modifica |
|---|---|
| `src/world/CubeSphereMapping.h/.cpp` | Aggiunti `FaceUVToChartCoordinates`, `FaceChartToDirection(CubeFace, ...)` e overload `int`. La conversione di ritorno ricostruisce UV e delega a `FaceUVToDirection`; non introduce switch duplicati. |
| `src/states/components/PlanetMapperUI.cpp` | Il producer raycast salva `chart.x/y` ottenuti dal helper canonico; corregge il segno V per i nuovi spawn. |
| `src/states/PlanetMapperState.cpp` | Il marker usa `FaceChartToDirection` con i valori persistiti invariati. |
| `src/states/PlayState.cpp` | Spawn iniziale e teletrasporto usano il helper comune; rimossi i due switch locali delle facce. |
| `src/world/PlanetGridCompatibility.h` | Nuovo adapter `TryLegacyFaceRowToCanonicalRow`, precondizione controllata `resolution>0` e `0<=row<resolution`, rifiuto `false` senza clamp. |
| `src/world/MapWorldGenerator.cpp` | Converte legacy row una volta nella lambda di generazione, prima di creare/aggiornare il chunk. Input non valido emette diagnostica e viene saltato. Le coordinate flatten legacy non sono cambiate. |
| `src/world/terrain/validation/TerrainValidation.h/.cpp` | Aggiunto `RunD12ConventionTests`, invocato dalla suite, con round-trip, sei facce/taglie, bordi, righe, flusso terrain/mesh e persistenza spawn JSON/binaria. |
| `src/core/app/main.cpp` | Aggiunta opzione `--validate-d12` che esegue solo i test mirati e restituisce exit code senza avviare GUI/Vulkan. |

Non ho modificato i file R3 o altri file già presenti come modificati/non tracciati nel working tree.

## 2. Diff sintetico

- Chart canonica: `cx=2u-1`, `cy=1-2v`; il valore producer `localZ` ora usa `chart.y*R`.
- I consumer continuano a interpretare `localZ/R` come `cy`; la funzione comune calcola `uv=((cx+1)/2,(1-cy)/2)` e delega al mapper canonico. Nessuna migrazione o riscrittura dei documenti.
- Row legacy valida viene tradotta con `canonicalRow=N-1-legacyRow` solo quando `MapWorldGenerator` assegna `PlanetChunkCoord.row`. In caso invalido il chunk non arriva a terrain/mesh e viene scritto un messaggio diagnostico.
- I test mirati sono accessibili con `--validate-d12`; la suite esistente li include anche in `RunAll()`.

## 3. Test introdotti

`RunD12ConventionTests()` raggruppa quattro verifiche:

1. **Chart e spawn producer:** campioni interni, centri, bordi e angoli su tutte le facce; UV→chart→direzione e inverse; simula l’assegnamento ai campi spawn con il helper; tie-break dominante ripetuto.
2. **Griglie e limiti:** Tiny, Micro, Small, Medium, Large e Huge; ogni riga legacy valida e la riflessione risultante; valori legacy `-1` e `N` e risoluzione `0` devono essere rifiutati; round-trip di ogni centro cella ed endpoint/linee interne UV→Grid.
3. **Nessuna doppia inversione:** per tutte le facce e le sei taglie, confronta i centri legacy con `TerrainSolver::GetVoxelSpherePos` e `PlanetaryMeshGenerator::ConvertToPlanetaryPositions` dopo un singolo adapter.
4. **Compatibilità persistente spawn:** round-trip di sei record spawn preesistenti attraverso JSON e binario; controlla che faccia, coordinate e direzione risultante non cambino.

La prova del producer è un test matematico del helper effettivamente chiamato da `PlanetMapperUI`, con UV sintetiche; non è una sessione GUI che genera un vero raycast.

## 4. Risultati effettivi di validazione

| Passo richiesto | Risultato | Evidenza |
|---|---|---|
| 1. Diagnostica toolchain | **PASS** | PowerShell `7.6.5`, progetto `D:\FAIRWORLD\FAIRWORLD`; `where.exe cmake`, `Get-Command cmake`, `where.exe cl/ninja/msbuild` non trovano i comandi nel PATH. CMake è disponibile all’indirizzo assoluto riportato sotto. |
| CMake versione | **PASS** | `D:\Visual Code\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --version` → `cmake version 4.3.1-msvc1`. |
| Toolchain | **PASS** | MSBuild `18.10.1-1.26427.6+3cd27c13e`; MSVC trovato in `D:\Visual Code\vs\VC\Tools\MSVC\14.51.36231\...\cl.exe`; Ninja presente ma non usato. |
| Preset/cache | **PASS** | `CMakePresets.json` prevede `Ninja` + `cl.exe`, preset `x64-Debug`. La cache `build_r3z7` già esistente seleziona invece `Visual Studio 18 2026`, istanza `D:/Visual Code/vs`, source `D:/FAIRWORLD/FAIRWORLD`; è stata rispettata senza cambiare generatore o configurazione. `CMakeUserPresets.json` assente. |
| Controllo ZERO_CHECK | **PASS** | Invocazione diretta del `cmake --check-stamp-file` generato dalla cache: exit code `0`. Il controllo automatico ha aggiornato `generate.stamp`; `CMakeCache.txt` è rimasto invariato. |
| Build Debug completa CMake | **INTERRUPTED / FAIL** | `cmake.exe --build build_r3z7 --config Debug --parallel 8` è stato avviato due volte; entrambe le sessioni sono state interrotte con Ctrl+C, exit code `1`, dopo `Checking File Globs` senza output ulteriore né artefatti aggiornati. |
| Build Debug `FairworldCore` | **FAIL** | MSBuild diretto sul progetto generato, command riportato sotto, exit code `1`; compilazione raggiunge `TerrainValidation.cpp` e segnala errori C++ elencati nella sezione 6. |
| 2. Test mirati D1.2 | **BLOCKED** | Non eseguiti dopo il primo errore di compilazione, come richiesto. |
| 3. Suite automatica esistente | **BLOCKED** | Non eseguita dopo il primo errore di compilazione. |
| 4. Regressioni Planet Mapper/PlayState | **SKIPPED** | Nessuna GUI avviata; build non valida e sequenza fermata sul compile error. |
| `git diff --check` della fase corrente | **SKIPPED** | Fermato dopo l’errore di compilazione; un controllo precedente, prima della recovery, aveva riportato PASS, ma non viene usato come risultato della presente sequenza. |

### Comandi effettivi

```text
where.exe cmake                         -> nessun match
Get-Command cmake                       -> nessun comando
cmake --version                         -> comando non riconosciuto
<percorso assoluto CMake> --version     -> 4.3.1-msvc1
<percorso assoluto CMake> -S... -B... --check-stamp-file ... -> exit 0
<percorso assoluto CMake> --build build_r3z7 --config Debug --parallel 8 -> interrotto, exit 1 (due tentativi)
MSBuild.exe build_r3z7/FairworldCore.vcxproj /p:Configuration=Debug /m:4 /verbosity:minimal /nologo /p:BuildProjectReferences=false -> exit 1
```

Non sono stati installati strumenti e non è stato modificato permanentemente il PATH. Le build dirette hanno riutilizzato il progetto Visual Studio generato dalla cache esistente.

## 5. Compatibilità dei salvataggi

La patch non modifica `SpawnPoint`, JSON, versione FWB o dati utente. I consumer ricevono gli stessi valori numerici e usano la stessa conversione chart→direzione che usavano prima: i vecchi spawn mantengono quindi la loro posizione attualmente interpretata. Non è stata applicata alcuna correzione retroattiva ai vecchi spawn creati dal click; la provenienza non è memorizzata.

`PlanetChunkCoord` non è serializzato dal documento. L’adapter modifica la coordinata runtime `row`; `PlanetChunkInstance.gridY` e gli indici flatten legacy rimangono invariati.

## 6. Errori di compilazione D1.2 originali (storico, corretti in §8)

La build del progetto generato `FairworldCore.vcxproj` si è fermata in `src/world/terrain/validation/TerrainValidation.cpp`:

| Riga | Diagnostica MSVC | Causa probabile |
|---:|---|---|
| 639, 643, 653, 659, 661 e 870 | `error C2065: 'kD4MappingEpsilon': identificatore non dichiarato` | I nuovi test D1.2 usano la costante prima della sua definizione, presente più avanti nello stesso file (linea 1233 nella working tree osservata). Il nome non è dichiarato in precedenza. |
| 841 | `error C3083: 'PlanetType': il simbolo a sinistra di '::' deve essere un tipo`; `error C2039: 'EarthPrime' non è un membro di 'fw'`; `error C2065: 'EarthPrime' identificatore non dichiarato` | `PlanetType` è dichiarato a namespace globale in `src/core/utils/World.h`; la qualifica `fw::PlanetType` nel test non corrisponde alla dichiarazione. |

MSBuild ha inoltre stampato `GLM_GTX_hash requires C++11 standard library support`, ma il messaggio non è stato riportato come errore bloccante nel log osservato.

Queste erano le diagnostiche iniziali della validazione D1.2. Le due cause di compilazione sono state corrette nella sezione 8; la build di `FairworldCore` è successivamente passata. Il test JSON/binario, la protezione per row invalida e i controlli GUI restano non verificati a runtime. Il test del producer esercita l’helper con UV sintetiche, non una sessione GUI/raycast reale. Le modifiche preesistenti non correlate nel working tree sono rimaste intatte.

## 7. Decisione al termine della recovery iniziale (storico, superata da §8)

**FAIL / NON VALIDATA al termine della recovery iniziale.** La decisione aggiornata dopo le correzioni autorizzate è riportata nella sezione 8; lo stato attuale è BLOCKED perché il target editor non ha completato la build.

**Non dichiarare D1 FROZEN.** Il prossimo passo richiede autorizzazione a correggere i due errori di test indicati e poi ripetere la sequenza di validazione.

## 8. D1.2.1 — correzione compile e validazione di recupero

### Correzioni autorizzate

In `src/world/terrain/validation/TerrainValidation.cpp`:

- Spostata la definizione esistente `static const float kD4MappingEpsilon = 1e-5f;` all'inizio del namespace `fw`, prima del primo uso. Valore, tipo e semantica sono invariati; non è stata aggiunta una seconda costante.
- Sostituito `fw::PlanetType::EarthPrime` con `::PlanetType::EarthPrime`. `PlanetType` è dichiarato nel namespace globale in `src/core/utils/World.h`; la definizione del tipo non è stata modificata.

Non sono stati modificati gli algoritmi Cube-Sphere, `MapWorldGenerator`, `PlanetGridCompatibility`, i formati persistenti o i sistemi R3. Nessun commit o push.

### Comandi e risultati

| Verifica | Risultato | Exit code / evidenza |
|---|---|---|
| `git diff --check` | **PASS** | Exit code `0`; solo avvisi Git preesistenti sulla normalizzazione LF/CRLF in altri file modificati. |
| Build Debug `FairworldCore.vcxproj` con MSBuild 18.10.1, cache `build_r3z7` | **PASS** | Exit code `0`; prodotto `build_r3z7/Debug/FairworldCore.lib`. Invocazione: `MSBuild.exe build_r3z7/FairworldCore.vcxproj /p:Configuration=Debug /m:4 /verbosity:minimal /nologo /p:BuildProjectReferences=false`. |
| Build Debug `FairworldEditor.vcxproj` con i riferimenti MSBuild predefiniti | **BLOCKED** | Rimasto su `Checking File Globs` senza diagnostiche; interrotto dopo attesa prolungata, exit code `1` dovuto all'interruzione. |
| Build Debug `FairworldEditor.vcxproj` senza ricostruzione dei riferimenti | **BLOCKED** | Nessuna diagnostica o output applicativo prima dell'interruzione manuale; exit code `1` dovuto all'interruzione. La configurazione e il generatore della cache non sono stati cambiati. |
| Disponibilità dell'eseguibile | **BLOCKED** | Non è stato prodotto `FairworldEditor.exe`; la scansione di `build_r3z7` ha trovato solo eseguibili ausiliari `CompilerIdCXX.exe` e `boo.exe`. |
| `--validate-d12` | **BLOCKED** | Non eseguibile senza il target applicativo appena compilato; nessun risultato o exit code del test disponibile. |
| Suite automatica esistente | **BLOCKED** | Gli eseguibili di test non sono stati prodotti/disponibili nella build Debug; non è stato possibile avviare la suite dopo il blocco del target. |
| Regressioni GUI Planet Mapper / PlayState | **SKIPPED** | Nessuna sessione GUI avviata, perché la build dell'editor non è stata completata. |

### Stato conclusivo D1.2.1

**BLOCKED — validazione incompleta.** Le due correzioni di compilazione sono confermate dalla build Debug PASS di `FairworldCore`. Il target applicativo è rimasto senza output durante i tentativi MSBuild e non ha prodotto l'eseguibile; perciò non sono stati eseguiti `--validate-d12`, la suite automatica o i controlli GUI. Non sono emersi nuovi errori C++: i target applicativi non hanno raggiunto una fase che ne producesse. Non ho applicato ulteriori correzioni. D1 non è FROZEN.
