# D1.2.19d.2 — Runtime Grounding & Visibility Diagnostics

**Esito:** CPU PASS per grounding, query voxel e contratti automatici; sandbox isolato pronto. Verifica GUI/GPU **NOT VERIFIED** (la GUI non è stata avviata dall'agente, come richiesto). D1 resta **NON FROZEN**.

## Modifiche mirate

- `src/world/GameWorld.h/.cpp`: introdotto il risultato tipizzato `WorldVoxelQueryResult` con stati `SolidVoxel`, `Air`, `PassableVoxel`, `OutOfBounds`, `TerrainPending` e `InvalidInput`. La query sferica distingue lo spazio sopra la colonna di terreno (`Air`) dai dati non disponibili (`TerrainPending`) e dai campioni fuori dominio (`OutOfBounds`).
- `src/systems/PhysicsEngine.h/.cpp`: il percorso sferico conteggia coordinate di voxel solidi distinti; mantiene contatori separati per Air/passabili, OutOfBounds e TerrainPending. Solo un campione `SolidVoxel` durante una discesa può porre `Grounded=true`. OutOfBounds e TerrainPending bloccano prudentemente il movimento senza essere presentati come contatti solidi.
- `src/systems/Systems.cpp`: la diagnostica separa contatti solidi, campioni Air/passabili, OutOfBounds, TerrainPending e ultima classificazione.
- `src/render/RenderManager.cpp`: aggiunta diagnostica a emissione limitata per conteggi chunk/voxel/mesh CPU/ECS/GPU, camera, matrici, AABB locale/globale, risultato e distanze dai piani del frustum. `DRAW REACHED` ora conta soltanto i `vkCmdDraw` registrati nel command buffer e il log dichiara che non prova submit o completamento fence. Il frustum non è stato disabilitato.
- `src/world/PlanetaryVoxelE2ETests.cpp`: regressioni per TerrainPending, OutOfBounds, contatore contatti solidi, Air sopra il terreno e grounding su voxel solido.

La prima build ha evidenziato errori di scope nei nuovi log renderer; corretti localmente nel blocco diagnostico. Il primo run Tiny ha poi rivelato una costruzione errata del campione OOB nel test: Tiny ha raggio 24, perciò `raggio - 30` attraversava il centro e tornava a distanza 6, producendo correttamente `TerrainPending`. Il test ora crea un `localY < 0` valido nel dominio radiale Medium, ripristinando immediatamente la dimensione Tiny prima del solver.

## Validazione CPU/headless

Comandi eseguiti dalla toolchain già configurata in `build_r3z7`:

```powershell
& 'D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe' 'build_r3z7\ALL_BUILD.vcxproj' /p:Configuration=Debug /p:Platform=x64 /m:1 /v:minimal /nologo
```

Esito: **PASS, exit code 0** (ALL_BUILD Debug x64 completo; 0 errori, 0 warning MSBuild).

Il runner Tiny è stato eseguito con working directory sandbox e fixture copiato:

```powershell
& 'D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldPlanetaryVoxelE2ETests.exe' --fixture 'D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\saves\map\world_map.json'
```

Esito finale: **PASS, exit code 0, 2,21 s**. Evidenze principali dal log:

- 54/54 coordinate candidate e chunk Tiny; 54 lookup logici.
- `TerrainPending`: stato 4, due campioni; nessun grounding e nessun contatto solido.
- `OutOfBounds`: stato 3, due campioni; nessun grounding e nessun contatto solido.
- Voxel solido: 1 contatto distinto, grounding corretto; campioni solid/air coerenti.
- Camera, movement math, gravità radiale, collisioni, salto/atterraggio e attraversamento griglia PASS.
- TerrainSolver PASS; 504.877 voxel non-Air; builder CPU condiviso PASS, 54 mesh non vuote e 135.820 triangoli.
- Pubblicazione ECS asincrona e upload/draw GPU non sono simulati dal runner e rimangono NOT VERIFIED.

Il primo run, prima della correzione del solo caso di test, è registrato in `D1_2_19D2_SANDBOX/logs/tiny_cpu_e2e.log` con exit 1 e diagnostica che ha identificato il punto. Il run finale è in `D1_2_19D2_SANDBOX/logs/tiny_cpu_e2e_run2.log` con exit 0.

| Contratto | Risultato | Exit code |
| --- | --- | ---: |
| JobSystem | PASS, 10/10 scenari | 0 |
| DMA result | PASS, 14/14 | 0 |
| VRAM allocator | PASS, 7/7 | 0 |
| Transfer/Graphics Sync | PASS, 6/6 | 0 |
| Forge Vertex | PASS | 0 |
| `--validate-d12` | PASS | 0 |
| Vulkan recovery policy | PASS, 3/3 | 0 |
| Tiny CPU E2E incl. grounding/spawn/camera/movement | PASS | 0 |

I log completi e file `.exitcode` per suite e durata si trovano in `D1_2_19D2_SANDBOX/logs/`. Non è stato avviato CTest dalla root sorgente.

## Sandbox e integrità

- Working directory: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`.
- Il fixture JSON Tiny è stato copiato in `saves\map\world_map.json`; `MapDocument::LoadJSON` lo ha caricato nel runner CPU. Il file GUI `.fwb` **non esiste ancora**; `WorldProjectManager::LoadSmart` può generarne uno nel sandbox al primo avvio. Un caricamento GUI effettivo non è stato osservato.
- Destinazioni relative di scrittura: `saves\map\world_map.json` e relativo `.fwb`, `saves\world\chunk_*.bin`, cache sotto `assets\`, `imgui.ini` e log nel runtime sandbox. La prova headless ha lasciato `saves\world` senza file.
- Asset copiati: 32 file, 805.789.452 byte. Shader SPIR-V richiesti dai percorsi `ReadFile` sono stati copiati nel runtime: 10 file alla radice e 2 copie sky nella sottodirectory `bin\` prevista dal renderer. File e hash: `D1_2_19D2_SANDBOX/baseline/sandbox_root_shaders.csv`.
- Nessun reparse point nella directory runtime. Gli ID file NTFS sono distinti dalle controparti sorgenti per tutti i file asset confrontati: 0 hard link condivisi rilevati. Le directory di scrittura sandbox sono separate e non collegate ai salvataggi originali.
- Manifest originale: 40/40 presenti; confronto pre/post: 40 invariati, 0 modificati, 0 mancanti. Restano documentate, senza rebaseline, le due divergenze già esistenti: `assets\texture_cache.bin` (corrente `D1D8655D…`, manifest `BFFDC3DD…`) e `assets\test_packaging_artifacts\pkg_roundtrip_03.json` (corrente `9C91BBA3…`, manifest `0F0FE0DC…`). Hash completi nei file `baseline\originals_before.csv` e `baseline\originals_after.csv`.

## Matrice finale

| Requisito | Stato | Evidenza |
| --- | --- | --- |
| Contatti voxel solidi e falso Grounded su OOB | **PASS CPU** | Tiny E2E: stati query 3/4 e test contatto solido |
| Gestione sicura di TerrainPending | **PASS CPU** | Movimento fermo, nessun contatto/grounding dichiarato |
| Chunk/voxel/mesh CPU Tiny | **PASS CPU** | Runner Tiny, 54 chunk e 54 mesh CPU |
| Diagnostica frustum/AABB/matrici/camera/draw | **NOT VERIFIED** | Instrumentazione compilata; nessuna sessione GUI |
| Percorso GPU e messaggi Vulkan | **NOT VERIFIED** | Nessun device/runtime GUI avviato |
| Fixture caricato in GUI e `.fwb` | **NOT VERIFIED** | JSON verificato dal runner; `.fwb` assente fino al primo avvio |
| Integrità degli originali | **PASS** | 40 hash invariati rispetto al baseline raccolto prima dei test |

## Avvio manuale richiesto all’utente

In **CMD**, il comando seguente imposta prima la working directory isolata, poi avvia l’eseguibile Debug esistente. Tutti i percorsi relativi di scrittura del runtime restano sotto il sandbox:

```cmd
cd /d "D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime" && "D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe" --disable-twitch-layer
```

Il comando è verificato rispetto ai percorsi esistenti. La prova GUI dovrà controllare il caricamento effettivo del JSON/`.fwb`, Grounded e contatti, conteggi mesh, AABB/frustum, draw submit/fence, log Validation Layer e shutdown. L’agente non ha avviato la GUI.

**Stato complessivo: PARTIAL PASS — CPU e sandbox PASS; GUI/GPU NOT VERIFIED. D1 NON FROZEN.**
