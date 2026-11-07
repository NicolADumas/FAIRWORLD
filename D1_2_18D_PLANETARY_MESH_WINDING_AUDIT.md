# D1.2.18d — Planetary Mesh Winding & Geometry Integrity

**Esito:** CPU GEOMETRY GATE PASS; GPU publication NOT VERIFIED. Nessun codice di produzione del mesher è stato modificato.

## Ambito e metodo

- Fixture: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json` (Tiny, risoluzione facce 3, sferico).
- Working directory dei runner: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`.
- Strumentazione circoscritta a `src/world/PlanetaryVoxelE2ETests.cpp`: test dei sei orientamenti voxel, deformazione sulle sei facce Cube-Sphere, edge/corner round-trip, misure geometriche e replay temporizzato del predicato di winding.
- `src/world/VoxelChunkMesher.cpp`, `PlanetaryMeshGenerator.cpp`, renderer, shader, serializer e dati persistenti sono rimasti invariati.
- Build: `build_r3z7`, Visual Studio/MSVC esistente, configurazione Debug, parallelismo 1.

## A — Audit del winding

Il mesher emette due triangoli per ciascuna faccia nell'ordine legacy `(a,b,c)` e `(a,c,d)`. Il test ricostruisce il primo triangolo per ciascuna faccia e calcola `cross(b-a,c-a) · normale_attesa`:

| Faccia voxel | Normale attesa | Segno del prodotto legacy | Esito |
|---|---:|---:|---|
| Top | +Y | negativo | winding opposto |
| Bottom | -Y | negativo | winding opposto |
| Left | -X | negativo | winding opposto |
| Right | +X | negativo | winding opposto |
| Front | +Z | positivo | coerente |
| Back | -Z | positivo | coerente |

I quattro casi incoerenti sono quindi Top, Bottom, Left e Right. Ogni quadrilatero contiene due triangoli: la correzione scambia i record vertex 1 e 2 dei rispettivi triangoli, producendo 8 correzioni su 12 nel cubo isolato. Il test conferma che gli scambi mantengono associati UV e material ID 3.

### Deformazione Cube-Sphere

`PlanetaryMeshGenerator::ConvertToPlanetaryPositions` trasforma le posizioni e ricostruisce normali geometriche. Per conservare l'orientamento atteso rispetto al normale voxel originario, inverte la normale ricostruita se `oldGeoNormal · oldNormal < 0`; non riordina i vertici. La correzione in `VoxelChunkMesher::Build` viene eseguita dopo la conversione e scambia i vertici 1/2 quando il prodotto tra normale geometrica deformata e somma delle normali interpolate è negativo.

Il test diagnostico applica il convertitore a tutti i sei orientamenti voxel su ciascuna delle sei facce planetarie: **48/72** triangoli risultano opposti alle normali prima del pass di correzione, **48/72** scambi sono eseguiti dal medesimo predicato, **0/72** degeneri. Dopo gli scambi tutti i prodotti di allineamento sono positivi. Questo mostra che, nei campioni provati, la deformazione mantiene la classificazione dei quattro errori d'ordine originari; non è la deformazione a riordinare la topologia.

## B — Geometria Tiny e Cube-Sphere

Entrambi i run completi hanno prodotto gli stessi risultati:

- 54/54 chunk ECS e coordinate planetarie; TerrainSolver completato su 54/54.
- 504.877 voxel non-Air, tutti ID 3.
- 54 mesh CPU non vuote associate ai rispettivi chunk.
- 407.460 vertici, 407.460 indici sequenziali, 135.820 triangoli.
- 0 indici invalidi, 0 posizioni non finite, 0 UV non valide, 0 normali non finite/non unitarie, 0 winding opposti.
- 0 triangoli con area doppia `<= 1e-7` o non finita.
- Hash deterministico di posizione, UV, normali e material ID in entrambi i run: `0x20c0d276200e0e20`.

Distribuzione dei triangoli per faccia planetaria (indici `CubeFace`):

| Face | Triangoli | Degeneri | Winding errato | Radial outward | Radial inward | Indeterminati |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 23.042 | 0 | 0 | 11.283 | 10.910 | 849 |
| 1 | 21.492 | 0 | 0 | 10.467 | 10.307 | 718 |
| 2 | 22.910 | 0 | 0 | 11.189 | 11.080 | 641 |
| 3 | 23.746 | 0 | 0 | 11.598 | 11.442 | 706 |
| 4 | 20.794 | 0 | 0 | 10.076 | 9.996 | 722 |
| 5 | 23.836 | 0 | 0 | 11.597 | 11.493 | 746 |
| **Totale** | **135.820** | **0** | **0** | **66.210** | **65.228** | **4.382** |

La classificazione radiale usa il segno `centroid · (n0+n1+n2)` rispetto al centro planetario; i triangoli inward e indeterminati sono riportati, non classificati come difetti automaticamente, perché le superfici voxel includono pareti e facce interne/di fondo. Il controllo di winding usa invece direttamente `cross(b-a,c-a) · (n0+n1+n2)` e non ha eccezioni nel campione Tiny.

### Bordi, angoli e continuità

Su tutte le sei facce sono stati verificati 72 campioni di bordo e 24 campioni d'angolo: round-trip `FaceUVToDirection → DirectionToFaceUV → FaceUVToDirection`, determinismo ripetuto del tie-break e massimo errore direzionale misurato pari a 0 nella precisione del test.

Questo controlla la continuità/determinismo della mappatura direzionale ai campioni scelti. **Non** verifica la condivisione di vertici o la topologia di adiacenza dei chunk tra facce, né la continuità visiva delle mesh sui seam Cube-Sphere: tale parte resta NOT VERIFIED (topologia cross-face D6 fuori da questo gate).

## C — Costo della correzione

La build Debug ha misurato il tempo complessivo del builder Tiny e un replay runner-only del predicato dinamico, fuori dal tempo di copia dei buffer. Risultati dei due run:

| Run | Build CPU complessiva | Replay già allineato | Replay con ogni triangolo invertito |
|---|---:|---:|---:|
| 1 | 603,1 ms | 145,473 ns/triangolo, 0 scambi | 188,877 ns/triangolo, 135.820 scambi |
| 2 | 257,786 ms | 136,749 ns/triangolo, 0 scambi | 164,093 ns/triangolo, 135.820 scambi |

La variabilità tra run è elevata e riflette una misura Debug non isolata da scheduling/cache; questi numeri sono una stima osservata del predicato e non un benchmark Release. Non è stata proposta né applicata una tabella statica per orientamento: la correzione deve essere valutata sulle coordinate deformate e i dati misurati non dimostrano ancora che una decisione fissa sia equivalente per tutti i casi.

## D — Worker e draw path

Il worker di `GameWorld::Update` chiama `VoxelChunkMesher::Build`, poi usa il flusso `vertices` per allocazione VRAM/upload; il builder continua a restituire anche indici sequenziali. Il render path voxel ispezionato emette `vkCmdDraw` non indicizzato, quindi la correzione scambia i record vertex nel medesimo ordine consumato dal worker senza richiedere un cambio del draw contract. La compatibilità del codice condiviso è verificata dall'ispezione del callsite e dal CPU runner; scheduling del worker, pubblicazione di `MeshComponent`, DMA e GPU non sono stati esercitati.

## E — Build, test e integrità

| Verifica | Comando/ambiente | Risultato | Exit code |
|---|---|---|---:|
| Build Debug runner E2E + Forge | `cmake --build build_r3z7 --config Debug --target FairworldPlanetaryVoxelE2ETests FairworldForgeVertexContractTests -- /m:1` | PASS | 0 |
| `git diff --check` sul runner diagnostico | `git diff --check -- src/world/PlanetaryVoxelE2ETests.cpp` | PASS | 0 |
| E2E Tiny run 1 | `FairworldPlanetaryVoxelE2ETests.exe --fixture <fixture Tiny>` dalla working directory sandbox | PASS, 54 mesh CPU | 0 |
| E2E Tiny run 2 | stesso comando | PASS, hash mesh uguale al run 1 | 0 |
| Forge Vertex Contract | `FairworldForgeVertexContractTests.exe` dalla working directory sandbox | PASS (orientamenti voxel, Cube-Sphere, producer mesh) | 0 |

La prima esecuzione del campione deformato è stata interrotta da un'asserzione perché il fixture diagnostico usava layer radiale 0, non valido per i vertici sintetici rispetto al core. Il test è stato corretto usando layer 1, il valore valido verificato dal mapping radiale; la successiva compilazione e i due run sono PASS. Nessuna modifica al comportamento di produzione ne è derivata.

Gli hash SHA-256 e le dimensioni di tutti i **10 file** elencati nel baseline `d1_2_18c_hashes_before.csv` corrispondono dopo i test (`d1_2_18d_hashes_after.csv`); nessun originale o fixture è cambiato.

Il `git diff --check` limitato al runner è pulito. Un controllo precedente sull'intera working tree segnala spazi finali preesistenti in `src/states/BlockMakerState.cpp`; non sono stati modificati in questa attività.

## F — Conclusione

**PASS** per l'integrità geometrica CPU misurata: la topologia legacy aveva quattro orientamenti errati, la correzione per triangolo li riallinea dopo deformazione; Tiny dà 0 mismatch, 0 degeneri e risultati deterministici in due run. La correzione dinamica non è stata sostituita o semplificata.

**NOT VERIFIED** per mesh pubblicate via worker, upload DMA/GPU e continuità topologica tra chunk/facce: il runner è headless e non simula questi contratti.

Nessun commit/push. Nessun freeze D1.

## Log ed evidenze

- `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18d_tiny_run1.log`
- `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18d_tiny_run2.log`
- `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18d_forge_vertex_sandbox.log`
- `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18d_hashes_after.csv`

