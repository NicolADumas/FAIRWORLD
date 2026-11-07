# FAIRWORLD P03d.4 — Grounded Upper-Body Contact Validation

**Esito:** PASS CPU/headless per gli scenari isolati; **NO-GO per chiudere integralmente P03d**. Nessuna modifica alla fisica di produzione. GUI/GPU non eseguite.

## 1. Baseline e sicurezza

- HEAD: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- Il working tree era già ampiamente dirty; `src/systems/PhysicsEngine.cpp` e il runner non tracciato `src/world/PlanetaryVoxelE2ETests.cpp` risultavano già modificati/non tracciati. Non sono stati ripristinati né puliti.
- Le sole modifiche di questa fase sono diagnostiche/test in `src/world/PlanetaryVoxelE2ETests.cpp` e questo rapporto. `PhysicsEngine.cpp`, solver, CMake, fixture sorgente e dati originali non sono stati modificati.
- Build e output: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`. OutDir, IntDir e PDB Debug/Release verificati dentro il sandbox. Fixture TinySphere copiato nel sandbox; SHA-256 sorgente e copia: `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`.
- I runner sono stati avviati con working directory sandbox e `TEMP`/`TMP` indirizzati al runtime sandbox.
- Un primo tentativo di link Release con il progetto Debug ha fallito (`LNK1181`, dipendenza relativa `_deps\joltphysics-build\Release\Jolt.lib`). Non era un errore di sorgente. È stato usato il progetto Release isolato già presente, con dipendenza Jolt sandbox valida; il build finale Release è PASS.

## 2. Contratto Grounded osservato nel codice

In `PhysicsEngine::ResolveSphericalCollisions`, `isGrounded` viene azzerato all’inizio del tick. Nel percorso sferico è impostato a `true` quando:

1. lo spostamento radiale è verso l’interno (`dy < 0`);
2. `tryMove` è bloccato da `SolidVoxel`;
3. viene quindi registrato `dbg_radialGroundContact` e la normale diagnostica vale `localUp`.

Il blocco `isCylinderBlocked` interroga 17 offset in quattro righe verticali (`0`, `0.1`, `height/2`, `height-0.1`) e restituisce soltanto una ragione (`SolidVoxel`, `Air`, ecc.), non l’indice della riga che ha trovato il voxel. Il chiamante radiale non riceve quindi una prova esplicita che il Solid responsabile sia sotto i piedi. Questa è una lacuna del contratto diagnostico e un rischio statico; non è stata riprodotta come falso Grounded nei fixture validi di questa fase.

`dbg_groundContactNormal` è la normale radiale del contatto Grounded. Il codice non espone una normale generica per pareti o soffitti: per quei contatti il report usa `NA`, senza dedurla dal solo flag di blocco.

## 3. Fixture upper-body e scenari

Il fixture precedente era inconcludente: il precheck descriveva una posa upper-only, ma il test avviava il player due metri più in alto. In caduta il player raggiungeva il voxel con i piedi (`Grounded=1`, foot support presente). Quel risultato non era un falso Grounded e non provava il caso richiesto.

Il fixture sostitutivo usa un singolo voxel Solid sulla riga superiore, a distanza tangenziale; prima della fisica tutti i campioni del cilindro di produzione sono Air e risolti, e i campioni inferiori sono Air. Il player, in modalità volante solo per isolare la componente tangenziale, raggiunge quel voxel senza sweep radiale. Dopo l’urto, i campioni dei piedi restano Air; `dbg_lastLookupCoord` coincide con l’unico voxel fixture e `Grounded` resta falso. Il test non simula contatto Grounded né altera la logica di volo.

| Scenario | Fixture/query prima e dopo | Risultato osservato | Debug / Release |
|---|---|---|---|
| Supporto valido dei piedi | Footprint trova il voxel `24,80,24`; nessun Pending | `Grounded=1`, contatto radiale, allineamento normale=1, velocità radiale finale=0 | PASS / PASS |
| Upper-body-only tangenziale | Tutti i probe iniziali Air; unico ostacolo Solid `22,80,24`; campioni inferiori Air anche dopo | Blocco tangenziale sul voxel corretto; nessun supporto ai piedi; `Grounded=0` | PASS / PASS |
| Parete laterale | Footprint inferiore Air; unico ostacolo Solid `22,79,24` | Hit parete e blocco tangenziale; `Grounded=0`; voxel hit coincide | PASS / PASS |
| Soffitto | Footprint inferiore Air; ostacolo Solid `24,79,24`; salita radiale bloccata | `Grounded=0`; voxel hit coincide | PASS / PASS |
| Caduta senza contatto | Chunk generati e vuoti; campioni Air, zero contatti | `Grounded=0`; velocità radiale `-0.0814819` dopo un tick | PASS / PASS |
| Supporto rimosso | Grounded iniziale e voxel supporto `24,80,24`; footprint Air dopo rimozione | `Grounded` passa da 1 a 0 al tick successivo | PASS / PASS |

Query `TerrainPending`, `OutOfBounds` e `InvalidInput` non sono state usate come Air nei precheck: fixture iniziali sono state accettate solo se i campioni richiesti risultavano Air/Solid come previsto. Nei test di parete/soffitto, `dbg_terrainPending=0` e `dbg_outOfBoundsSamples=0`.

### Debug/Release

I risultati dei sei scenari, le coordinate dei voxel e gli stati Grounded sono identici in Debug e Release. Il runner di caratterizzazione `--p03a-characterize` è terminato con exit code 0 in entrambe le configurazioni.

## 4. Test e regressioni

Comandi eseguiti con MSBuild 18.10.1 e i progetti runner sandbox preesistenti:

- Build runner Debug: exit code 0, 0 warning, 0 errori.
- Build runner Release: exit code 0, 0 warning, 0 errori.
- `FairworldPlanetaryVoxelE2ETests.exe --p03a-characterize`: exit code 0 Debug e 0 Release.
- Full runner Tiny `--fixture D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d4-fixture\world_map.json`: exit code 0 Debug e 0 Release.

Il full runner finale ha rieseguito i controlli integrati P02a–P02d e P03–P03d.3 presenti nel runner. Evidenze principali in entrambe le configurazioni:

- FWB v8 semantic parity / compatibilità v7: PASS.
- Mapping D1.2.19d.5, camera/movimento, identity e TerrainSolver: PASS.
- P02a: 13.956 round-trip; 6/6 dimensioni; query ai sei orientamenti e raycast 6/6.
- P02c: 2.112 relazioni reciproche; 72/72 vicini diretti consegnati al mesher.
- P02d: 324/324 probe planetari validi; slab/DDA PASS.
- Tiny: 54 chunk ECS/solver/mesh CPU associati; 504.877 voxel non-Air.
- Mesh CPU: 54 risultati, 297.756 vertici, 99.252 triangoli; indici/posizioni/UV/normali validi; zero winding mismatch e zero degenerazioni.
- Digest mapping: `uvDigest=0x93ad78385e744797`, `directionDigest=0xd1d27989d363c027` in entrambe.
- Hash mesh diagnostico: `0xaa8de4f86721f55c` in entrambe.
- Nessun `[FAIL]` nel full runner finale; GPU publication resta esplicitamente NOT VERIFIED.

Dump confrontabili, generati dal runner finale in file nuovi sotto il sandbox:

| Artefatto | Debug SHA-256 | Release SHA-256 | Esito |
|---|---|---|---|
| Dump voxel ordinato per 54 coordinate | `F3D0B98BBCEDDE8CC64D21DEA3588E866FA24F9AB2138658215FD68E601F3615` | identico | byte-per-byte PASS |
| Dump mesh CPU condiviso | `267B0E8D6CA28A5A07B5BEFB97AA48D7EE366DCFA1E74965F37E510B13B7B8C6` | identico | byte-per-byte PASS |

## 5. Root cause, limite e correzione proposta

**Falso Grounded riproducibile:** nessuno nei fixture validi. I contatti upper-body tangenziali, laterali e di soffitto non producono Grounded; il supporto valido lo produce e la perdita del supporto lo cancella.

**Lacuna confermata nel codice:** il motivo `SolidVoxel` non conserva quale altezza della capsula ha trovato il voxel. La condizione Grounded radiale accetta il risultato aggregato della capsula. La prova runtime non ha costruito un caso di sweep radiale upper-row-only che mantenga i piedi Air: il precedente fixture radiale arrivava al contatto ordinario dei piedi, perciò è stato escluso come evidenza. Il nuovo fixture verifica contatto superiore tangenziale, non pretende di certificare quel sottocaso radiale.

Correzione minima proposta, non applicata: far restituire alla query di collisione anche la riga/probe e il voxel responsabile; permettere Grounded solo con supporto inferiore compatibile con `localUp`. Aggiungere prima un fixture radiale upper-row-only che mantenga il footprint inferiore Air, oppure documentare formalmente che tale configurazione è irraggiungibile con il collider e la discretizzazione correnti. Non cambiare il solver finché questa distinzione non è verificata.

## 6. Integrità, artefatti e decisione

- Fixture sandbox finale SHA-256: `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`.
- Log completi: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d4-final-debug.log`, `...\p03d4-final-release.log`; log scenari isolati `...\p03d4-contacts-debug-final.log`, `...\p03d4-contacts-release-final.log`.
- Dump voxel/mesh, PDB e intermedi sono sotto `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`. Nessun salvataggio originale è stato usato come destinazione.
- Nessun sorgente di produzione, serializer, fixture originale o configurazione CMake è stato cambiato per questa fase.

**GO:** fixture e scenari P03d.4 CPU/headless passano identicamente in Debug/Release; regressioni Tiny e parità 54/54 preservate.

**NO-GO:** non chiudere ancora integralmente P03d sulla sola base di questo gate. Rimangono non verificati il caso di sweep radiale bloccato esclusivamente da una probe superiore e i gate GUI/GPU; inoltre il solver non espone una normale di contatto generica per pareti/soffitti. Nessuna patch alla fisica è stata applicata.
