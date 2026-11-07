# FAIRWORLD P03d.2 — Collision & Runner Diagnostic Closure

**Esito:** P03d.2 chiuso come diagnosi parziale; **NO-GO** per P03d.3 e P04.

Nessun comportamento di `PhysicsEngine`, `TerrainSolver`, mesher o renderer è stato modificato. Le aggiunte al runner sono solo diagnostiche e condizionate da variabili d'ambiente.

## 1. Safety e baseline

- `HEAD`: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`, identico a P01.
- Il working tree era già dirty; sono state preservate le modifiche e i file non tracciati. Nessun reset, clean, commit, push o rigenerazione CMake.
- Fixture letto: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`.
- Working directory dei runner: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`.
- Build finali dei runner: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d2-debug-runner-bin` e `p03d2-release-runner-bin`; gli `IntDir` erano sotto i rispettivi progetti copiati nel sandbox. I PDB dei runner non sono stati scritti nel percorso originale.

**Deviazione di isolamento da segnalare:** al primo build diagnostico non avevo verificato gli `OutDir` dei progetti copiati. Questi puntavano a `D:\FAIRWORLD\FAIRWORLD\bin\Debug` e `bin\Release`, aggiornando i due eseguibili generati `FairworldPlanetaryVoxelE2ETests.exe` nelle directory sorgente. Il PDB dell'editor (`FairworldEditor.pdb`, timestamp 20:12) e l'eseguibile dell'editor non sono stati toccati; il PDB Debug del runner originale conserva timestamp 21:02 e non è stato aggiornato. Non ho tentato ripristini. Ho poi corretto esclusivamente i progetti di build copiati nel sandbox e tutte le compilazioni successive sono uscite nei percorsi sandbox sopra indicati.

## 2. Debug body / Transform / camera

**Il FAIL Debug richiesto non si riproduce.** Sia il log baseline P03d.2 sia il rerun finale Debug terminano con exit code `0`; l'asserzione `radial gravity, physical landing, Space jump, relanding and body/Transform camera anchoring` è PASS. Non esiste quindi una prima asserzione Debug fallita o un tick Debug da attribuire a un difetto. Anche la baseline P03d.1 riportava lo stesso PASS.

La prima anomalia comparabile era in Release: `radialSurfaceAgreement=0` nel log precedente, pur con collider e superficie mesh entrambi a raggio `42.5`. La diagnostica usava `dbg_lastLookupCoord`, l'ultima query e non necessariamente il supporto. Il runner ora usa il `dbg_groundVoxelCoord` del contatto e controlla indipendentemente il voxel sotto e quello sopra. Nel rerun Debug e Release il controllo è `radialSurfaceAgreement=1`, e l'asserzione body/Transform/camera passa in entrambi. È un errore dimostrato del runner, non una correzione fisica.

## 3. Locomozione diagonale e fixture Grounded

Con `FAIRWORLD_P03D2_TRACE_DIAGONAL=1`, Debug e Release hanno la stessa sequenza nei primi tick, senza `TerrainPending`, `Missing` o `OutOfBounds`:

| Tick | Grounded | Evidenza | Interpretazione |
|---:|---|---|---|
| 0 | 1 → 1 | contatto radiale solido, support voxel `(24,82,24)` | appoggio iniziale |
| 1 | 1 → 0 | `tangentialSolidBlock=1`, `againstWall=1`, penetrazione `0.000320435` | transizione da contatto radiale a blocco tangenziale |
| 2 | 0 → 1 | contatto radiale solido | riacquisizione appoggio |
| 3 | 1 → 0 | nessun supporto ai piedi e nessun contatto | Grounded viene rimosso |

Le posizioni e velocità stampate ai tick 0–3 coincidono tra configurazioni alla precisione del log. Le transizioni successive sono anch'esse uguali. Il test consente il distacco su rilievo/dislivello; non è dimostrato un errore Grounded specifico di Release né una discrepanza Debug/Release della locomozione. Il runner non prova che ogni distacco sia visivamente corretto: GUI non eseguita.

Fixture upper-body-only: il preflight conferma campioni inferiori tutti `Air` e risolti, un voxel `Solid` upper-body a row 80, senza Missing/Pending/OOB. Ma durante il solver il player arriva al contatto ordinario dei piedi con quel voxel (`footSupport=1`, `radialContact=1`, tick 9) prima che possa verificarsi un contatto solo col busto. L'esito è **NOT VERIFIED**, non PASS e non prova un falso Grounded.

## 4. Divergenza Debug/Release del TerrainSolver

Dump binari ordinati dei 54 chunk, generati separatamente dal runner, confrontati byte per byte. Entrambe le configurazioni usano lo stesso fixture, PlanetSize Tiny, seed `0`, 54 chunk e sole regole base. Debug produce `504877` voxel non-Air; Release `504879`. Esistono esattamente due byte voxel differenti e nessun disaccordo nelle chiavi dei chunk:

| Ordine | Chunk planetario | Voxel locale `(x,y,z)` | Debug | Release |
|---:|---|---|---:|---:|
| 1 | face 3, col 0, row 2, layer 0 | `(13,71,3)` | Air `0` | block `3` |
| 2 | face 5, col 2, row 1, layer 0 | `(12,71,7)` | Air `0` | block `3` |

La trace opt-in ricostruisce la decisione di generazione. Per entrambi i campioni UV e il vettore cubico grezzo `FaceUVToCubeDir` sono identici tra configurazioni. La **prima divergenza numerica osservata è la normalizzazione dentro `CubeSphereMapping::FaceUVToDirection`**: per esempio, face 3 il componente Z del vettore grezzo è identico (`-0x1.d55554p-2`), ma l'output normalizzato Z è `-0x1.89d628p-2` in Debug e `-0x1.89d62ap-2` in Release. La differenza di pochi ULP passa poi in `GetVoxelSpherePos`, nel campionamento OpenSimplex2/FBm e infine attraversa la soglia `depth >= 0` in `TerrainSolver::EvaluateLayers`:

| Campione | Debug depth | Release depth | Conseguenza |
|---|---:|---:|---|
| face 3, `(13,71,3)` | `-0.002105712891` | `+0.0005683898926` | Air / layer block 3 |
| face 5, `(12,71,7)` | `-0.0136089325` | `+0.006324768066` | Air / layer block 3 |

Il progetto runner Debug compila con ottimizzazione disabilitata; Release usa `MaxSpeed`. Questo identifica la prima fase divergente e il trigger discreto, ma **non dimostra ancora** se l'origine precisa sia il codegen MSVC della normalizzazione GLM, le impostazioni FP effettive o un'altra scelta di compilazione. Il modello floating-point XML è vuoto (default del toolset); nessuna opzione fast-math è stata provata. Non sono stati modificati digest o attese. La causa numerica a monte resta da correggere separatamente e richiede un test mirato della normalizzazione deterministica.

Il runner mesh finale: Debug `99,252` triangoli, Release `99,256`; Release fallisce il contratto/hash mesh condiviso perché la sorgente voxel differisce nei due punti. La correzione del runner non maschera né aggiorna tale attesa.

## 5. Regressioni ed esiti

| Gate | Risultato | Exit code / evidenza |
|---|---|---|
| Build runner Debug isolata | PASS | MSBuild `0`; `p03d2-runner-debug-final-build.log` |
| Build runner Release isolata | PASS | MSBuild `0`; `p03d2-runner-release-final-build.log` |
| Full runner Debug finale | PASS | `0`, 21.951 ms; `p03d2-final-debug.log` |
| Full runner Release finale | FAIL | `1`, 841 ms; unico `[FAIL]` è `shared production CPU voxel mesh contract`; `p03d2-final-release.log` |
| Fixture Tiny / chunk identity e solver | PASS entrambi | 54/54 generati; queue 0; Debug 504877 e Release 504879 non-Air |
| P02a–P02d, P03–P03d.1 coperti dal full runner | PASS, salvo gate mesh Release indicato | log finali sopra; non sono stati avviati runner addizionali dedicati |
| P03d.1 FWB v8, compatibilità v7, corruzione e `--validate-d12` | PASS | `FairworldEditor.exe --validate-d12`, exit `0`, 675 ms; `p03d2-validate-d12-retry.log` |
| Prima prova `--validate-d12` | BLOCKED dall'ambiente | `-1073741515` (`0xC0000135`), 83 ms; mancava il Debug CRT nella sessione |
| Ripetizione `--validate-d12` con PATH temporaneo | PASS | aggiunto il Debug CRT MSVC e `bin\Debug` solo al processo; nessuna modifica permanente PATH; i file temporanei FWB sono sotto la working directory sandbox |
| Upper-body-only Grounded | NOT VERIFIED | fixture non isola il contatto upper-only |
| GUI/GPU | NOT RUN | vietato in questa fase |
| `git diff --check` completo | FAIL su modifiche preesistenti | exit `2`; segnala trailing whitespace in `src/states/BlockMakerState.cpp` (righe 4652–4669 e 11563, 11570–11571, 11806, 11828, 11830). Il runner non aggiunge whitespace finale; nessun intervento su quei diff |

La validazione D1.2 ha eseguito e riportato PASS per FWB v8 semantic parity, FWB v7 compatibility/corruption rejection, round-trip Cube-Sphere, sei taglie, righe/bordi e spawn legacy.

## 6. Diagnostica runner modificata

Solo il runner già non tracciato `src/world/PlanetaryVoxelE2ETests.cpp` è stato esteso in questa fase:

- trace per tick attivabile con `FAIRWORLD_P03D2_TRACE_DIAGONAL`;
- verifica supporto Grounded usando il voxel realmente registrato come supporto;
- fixture sintetico con preflight upper/lower;
- snapshot binario per chunk attivabile con `FAIRWORLD_P03D2_VOXEL_DUMP` e hash/testo con `FAIRWORLD_P03D2_TRACE_VOXEL_CHUNKS`;
- replay CPU dei due punti divergenti con campioni UV, direzione cubica, direzione normalizzata, noise, superficie e depth attivabile con `FAIRWORLD_P03D2_TRACE_TERRAIN_DECISIONS`.

I file di dump e i log sono sotto `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`. Nessuna modifica funzionale al solver è stata applicata.

## 7. Decisione

**NO-GO per P03d.3 e P04.** Prima occorre una fase separata per rendere deterministico il mapping/normalizzazione numerica o definire un contratto voxel Debug/Release stabile e testarlo; non va corretto aggiornando i digest attesi. P03d.2 ha chiuso l'errore della diagnostica `radialSurfaceAgreement`, ma il gate Release mesh è ancora FAIL e il fixture upper-body-only non ha esercitato il caso richiesto. La camera/Transform Debug non ha un FAIL riproducibile. Nessuna GUI/GPU è stata verificata e nessun sistema è dichiarato frozen.
