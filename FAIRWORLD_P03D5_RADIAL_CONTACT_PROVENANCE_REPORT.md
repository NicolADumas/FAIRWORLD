# FAIRWORLD P03d.5 — Radial Contact Provenance & Grounded Contract Closure

## Esito

**NO-GO per la chiusura del contratto Grounded CPU.** Il falso Grounded radiale richiesto non è stato riprodotto perché il runner non ha trovato un fixture valido con partenza libera, contatto superiore e footprint inferiore Air. Questo risultato è **NOT VERIFIED**, non un PASS e non prova che il caso sia impossibile.

Le regressioni P03d.4, Tiny e Debug/Release sono PASS. Nessuna modifica è stata apportata a `PhysicsEngine` o ad altri sistemi di produzione. Nessuna GUI/GPU è stata avviata.

## 1. Safety e provenienza

- HEAD prima/dopo: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- Working tree già dirty e con file untracked; preservato, senza reset, clean, commit o push.
- L'unico sorgente modificato in questa fase è il runner diagnostico già untracked `src/world/PlanetaryVoxelE2ETests.cpp`. `src/systems/PhysicsEngine.cpp` era una modifica preesistente e non è stato modificato in P03d.5.
- Build e run conclusivi sono stati eseguiti nel runtime sandbox: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`.
- Fixture Tiny utilizzato: `p03d4-fixture\world_map.json`, SHA-256 `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`.
- OutDir, IntDir ed eseguibili dei runner sono sotto il sandbox. Il Debug runner dipende dalla libreria Debug isolata e il Release runner dalla libreria Release isolata; i percorsi risultano nel progetto MSBuild del sandbox.

### Anomalia di isolamento da dichiarare

Una prima invocazione completa è partita con working directory `D:\FAIRWORLD`, non con quella sandbox. Il test FWB usa `std::filesystem::current_path()` per creare la propria directory. Ha quindi creato fuori sandbox le directory:

- `D:\FAIRWORLD\p03d1_fwb_test_23102167365500`
- `D:\FAIRWORLD\p03d1_fwb_test_23102724810900`

Contengono soltanto artefatti del test FWB (JSON/FWB temporanei). Sono state lasciate intatte e non ripulite. In questa invocazione il runner si è fermato al test FWB e ha restituito exit code 1 in entrambe le configurazioni. Non si attribuisce quel FAIL alla fisica: il test cerca il fixture v7 relativo alla working directory. Le successive esecuzioni valide hanno usato esplicitamente la working directory sandbox e il fixture v7 sandbox.

## 2. Audit del percorso radiale

In `PhysicsEngine.cpp`, `ResolveSphericalCollisions` definisce `isCylinderBlocked` (circa riga 791). La funzione campiona quattro altezze del collider, `0.0, 0.1, 0.9, 1.7` per altezza 1.8, e gli offset radiali/tangenziali della sezione cilindrica. Quando una query trova un voxel solido, il risultato aggregato è `SolidVoxel`: non conserva riga della probe, offset o coordinata del voxel responsabile.

Durante lo sweep radiale, se il movimento è verso l'interno (`dy < 0`) e `tryMove` restituisce `SolidVoxel`, il codice:
1. marca `dbg_radialGroundContact`;
2. copia `dbg_lastLookupCoord` in `dbg_groundVoxelCoord`;
3. usa `localUp` come normale diagnostica;
4. raffina la posizione con altre chiamate aggregate a `isCylinderBlocked`;
5. imposta `rb.isGrounded = true`.

Riferimenti: `PhysicsEngine.cpp:791`, `:955–978`. La normale diagnostica è la direzione radiale usata dal solver, non una normale calcolata dal voxel. Il dato perso è la provenienza della prima probe che ha bloccato il passo; `dbg_lastLookupCoord` non identifica necessariamente un supporto inferiore.

Questa è una **lacuna statica di provenienza**. Da sola non dimostra un falso Grounded runtime.

## 3. Fixture radiale e ricerca

Il runner-only `TestP03d5RadialUpperOnlyContactSearch` è in `PlanetaryVoxelE2ETests.cpp:2458`. Per ogni fixture:
- crea il mondo Tiny vuoto e verifica che tutti i 54 chunk siano queryabili;
- colloca un solo voxel solido;
- usa le quattro altezze delle probe e gli offset di collisione effettivi;
- richiede partenza senza alcuna probe solida, movimento strettamente radiale verso l'interno, una probe superiore solida all'impatto e tutte le probe inferiori Air;
- non considera Missing/Pending/OOB come contatto.

La ricerca copre 54 testimoni: sei facce × centro, quattro bordi e quattro angoli; raggi iniziali campionati in una finestra di 1,21 m a passi di 0,01 m e passi radiali `0.01, 0.025, 0.05, 0.075, 0.1 m`.

Risultati identici Debug/Release:
```
faces=6 uvWitnessesPerFace=9 targetCells=54
clearStarts=540
upperSolidPredictions=59670
upperPredictionsWithPenetratingStart=54270
preblockedStartRows=4158,2700,29970,22842
upperPredictionsWithLowerSolid=5400
lowerAirAndUpperSolidCandidates=0
unavailableQueries=0
witnessExercised=0 falseGrounded=0
```

Il witness diagnostico al centro della faccia 0 ha `baseRadius=38.5`: la row superiore (indice 3) del target era già solida prima del passo (`preexistingVoxel=24,80,24`). È una partenza in overlap e il fixture viene respinto. Nessun candidato valido ha raggiunto il solver come contatto upper-only radiale.

Il fatto che le altezze delle probe differiscano al massimo di 0,8 m mentre il voxel è spesso 1 m lungo l'asse locale rende plausibile che molti contatti superiori inizino con una probe già sovrapposta; la ricerca osservata è però discreta e limitata, quindi non prova l'impossibilità geometrica generale.

## 4. Grounded e provenienza osservati

- **Caso richiesto: radiale inward, upper-only, footprint inferiore Air:** NOT VERIFIED; fixture valido non trovato.
- **False Grounded in tale caso:** non riprodotto, ma non può essere marcato PASS senza aver esercitato il witness.
- **Scenari P03d.4:** PASS in Debug e Release:
  - foot support: Grounded e contatto radiale validi;
  - upper-body-only tangenziale: hit upper, foot support assente, Grounded false;
  - side wall: bloccata, Grounded false;
  - ceiling: movimento verso l'alto bloccato, Grounded false;
  - caduta senza contatto: Grounded false;
  - rimozione del supporto: Grounded passa da true a false.
- Il caso tangenziale P03d.4 non sostituisce quello radiale richiesto da P03d.5.
- Le query nel fixture P03d.5 erano tutte risolte; contatore unavailable = 0.
- Il solver non fornisce una normale geometrica per ogni blocco. I campi `contactNormal=NA(radialGroundNormalOnly)` sono lasciati come tali; non sono stati inventati valori.

## 5. Build e regressioni

### Build runner

| Configurazione | Comando | Risultato |
|---|---|---|
| Debug | `& 'D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe' 'D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d2-debug-runner-project\FairworldPlanetaryVoxelE2ETests.vcxproj' /p:Configuration=Debug /p:Platform=x64 /m:1` | PASS; exe in `p03d2-debug-runner-bin` |
| Release | `& 'D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe' 'D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d2-release-runner-project\FairworldPlanetaryVoxelE2ETests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m:1` | PASS; exe in `p03d2-release-runner-bin` |

Log: `p03d5-runner-debug-build.log`, `p03d5-runner-release-build.log`.

### Run completo nel sandbox

Comando effettivo, da working directory `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`:

```powershell
$env:FAIRWORLD_P03D1_V7_FIXTURE = '.\p03d-roundtrip\p03d_roundtrip.fwb'
$env:FAIRWORLD_P03D2_VOXEL_DUMP = '.\p03d5-debug-isolated-voxels.bin' # Release: p03d5-release-isolated-voxels.bin
$env:FAIRWORLD_P03D3_MESH_DUMP = '.\p03d5-debug-isolated-mesh.bin'    # Release: p03d5-release-isolated-mesh.bin
.\p03d2-debug-runner-bin\FairworldPlanetaryVoxelE2ETests.exe --fixture .\p03d4-fixture\world_map.json
.\p03d2-release-runner-bin\FairworldPlanetaryVoxelE2ETests.exe --fixture .\p03d4-fixture\world_map.json
```

| Verifica | Debug | Release |
|---|---:|---:|
| Runner completo, inclusi P02/P03, P03d.4 e FWB v8/v7 | PASS, exit 0 | PASS, exit 0 |
| P03d.4 sei scenari | PASS | PASS |
| P03d.5 bounded search | NOT VERIFIED, 0 witness; runner exit 0 | NOT VERIFIED, 0 witness; runner exit 0 |
| Tiny chunk/TerrainSolver | 54/54, 504.877 voxel non-Air | 54/54, 504.877 voxel non-Air |
| Mesher CPU condiviso | 54 risultati associati, 99.252 triangoli, 0 winding mismatch | stessi valori |
| Voxel dump | 1.770.560 byte; SHA-256 `F3D0B98BBCEDDE8CC64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503` | byte-identico; SHA-256 `F3D0B98BBCEDDE8CC64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503` |
| Mesh dump | 10.720.304 byte; SHA-256 `267B0E8D6CA28A5A07B5BEFB97AA48D7EE366DCFA1E74965F37E510B13B7B8C6` | byte-identico; SHA-256 `267B0E8D6CA28A5A07B5BEFB97AA48D7EE366DCFA1E74965F37E510B13B7B8C6` |
| FWB semantic parity v8, compatibilità v7 e corruzione | PASS | PASS |

Log completi: `p03d5-debug-isolated.log`, `p03d5-release-isolated.log`. Dump e log sono sotto il runtime sandbox. I risultati CPU del mesher non attestano MeshComponent pubblicate o upload GPU.

## 6. Root cause e decisione

**Root cause runtime del falso Grounded radiale: non dimostrata.**  
**Rischio statico:** la decisione Grounded considera `SolidVoxel` aggregato senza la riga/offset/voxel della probe che ha causato il blocco; il marker `dbg_groundVoxelCoord` è basato sull'ultima lookup, non su un supporto inferiore attestato.

### Passo minimo proposto

Prima di cambiare la fisica, ampliare il runner CPU per costruire/esplorare fixture radiali in coordinate world subvoxel e con più configurazioni della sfera locale, mantenendo i vincoli iniziale-libero / superiore-solido / inferiore-Air. Se resta non costruibile, registrare una dimostrazione geometrica basata sul mapping reale e sulla discretizzazione.

Solo dopo aver ottenuto un witness, valutare una patch separata: restituire dalla query di collisione una provenienza tipizzata `{reason, probeRow, probeOffset, voxelCoord}`; impostare Grounded esclusivamente quando la provenienza identifica un supporto inferiore compatibile con `localUp`. Questa patch è una proposta, non applicata.

## 7. Limiti

- Nessuna GUI/GPU eseguita.
- Nessuna prova di impossibilità per tutte le posizioni, facce o forme di terreno.
- Nessun upload GPU o draw testato.
- Il runner attuale può terminare con exit 0 mentre la specifica P03d.5 rimane `NOT VERIFIED`: l'exit code certifica il runner/regressioni CPU, non l'esistenza del witness.
- Le due directory temporanee create accidentalmente fuori sandbox sono state conservate e non ripulite.

## Decisione

**P03d.5: NOT VERIFIED sul caso radiale richiesto; NO-GO per chiudere Grounded CPU.**  
Le regressioni P03d.4, Tiny CPU e la parità Debug/Release restano PASS. Nessuna patch PhysicsEngine applicata. Fermarsi qui; nessun P04 né freeze.

