# FAIRWORLD P03d — Procedural Round-Trip & Release Diagnostics

**Esito:** round-trip JSON PASS; FWB/WorldProjectManager semantic FAIL; full runner Debug PASS; full runner Release FAIL. Il primo punto della divergenza geometrica Debug/Release è localizzato al più tardi nell'output del TerrainSolver, ma il primo chunk/voxel Release non è stato acquisito. Il FAIL Grounded Release è causato da un controllo diagnostico del runner che usa un campione non rappresentativo del contatto. **NO-GO per P04.** Nessuna correzione di produzione è stata applicata.

## 1. Safety e provenienza

- HEAD: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- Working tree: 154 voci prima e dopo le prove; le modifiche preesistenti sono state preservate. L'unico nuovo file nel repository è questo rapporto.
- Fixture Tiny originale: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`; SHA-256 prima e dopo `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`.
- Working directory dei runner: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`.
- Fixture round-trip, eseguibile diagnostico, log e trace sono tutti sotto `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`. Nessuna GUI/GPU, salvataggio originale, reset, clean, commit, push o riconfigurazione CMake.
- Hash fixture generati: `p03d_roundtrip.json` `34A4251F10C7933900DD18DED0203ECACE55ECABF94B12C1EE723CEBC98E66E0`; `p03d_roundtrip.fwb` `B3533DFF60A81F09065B68E4CC0A1BA1F867AB369FBA9D096055C9526C887E03`; `p03d_from_fwb_reload.json` `E51597A507D425383F151A7D0212A56DCF353F29925750B5A13C2B1DB07CEC38`.

## 2. Procedural template JSON/FWB round-trip

Runner diagnostico temporaneo: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d-roundtrip\P03DRoundTrip.cpp`; build Debug MSBuild exit 0. Il fixture di prova è una copia derivata dal Tiny sandbox, con template univoco `p03d_unique_template_7f29`, nome `P03D Unique Basalt Shelf`, PlanetSize Tiny, seed `0x00D03A71`, layer `fairworld:grass` 0–1.25, `fairworld:sand` 1.25–3.5, core `fairworld:stone`, e sottoregione con override `fairworld:dirt` 0–2.

| Percorso reale | Esito | Evidenza |
|---|---|---|
| MapDocument SaveJSON → LoadJSON | **PASS** | Identità, seed, size, layer, profondità, core, sottoregione, override e istanza verificati semanticamente. |
| MapDocument SaveBinary → LoadBinary (FWB v7) | **FAIL** | Caricamento riuscito, ma `subRegions[0].overrides.layers` assente dopo il load. |
| WorldProjectManager SaveProject → LoadProject | **FAIL semantico** | Save e load restituiscono successo; il log conferma `LoadSmart` sceglie il companion `.fwb`, perdendo lo stesso override. |

Il runner round-trip temporaneo è stato compilato Debug con MSBuild, exit 0. Esecuzione con input fixture Tiny e output directory `runtime\p03d-roundtrip` ha exit 9, il codice di esito previsto dal test quando una delle verifiche semantiche fallisce; non è un crash. I due runner di generazione custom (`p03d_roundtrip.json` e `p03d_from_fwb_reload.json`) hanno exit 1 perché i loro dati differiscono dai riferimenti del runner standard; i conteggi e la prima differenza sono riportati sotto.

La causa è nel contratto FWB attuale: `SaveJSON` scrive `r.overrides` per le sottoregioni (`MapDocument.cpp:463–479`), mentre `writeRegion`/`readRegion` (`MapDocument.cpp:818–851`) non serializzano `influence` né `overrides`. `MapDocument::LoadSmart` preferisce il binario quando il suo timestamp è almeno quello del JSON (`MapDocument.cpp:1080–1105`); `WorldProjectManager::LoadProject` lo chiama direttamente (`WorldProjectManager.cpp:89–105`). Quindi un salvataggio riuscito può ricaricare una ricetta semanticamente diversa.

La serializzazione FWB ha preservato l'identità e i campi dell'istanza: `templateId=p03d_unique_template_7f29`, `isActive=true`, face 0, grid (1,1), `isGridAligned=true`. Il fallimento semantico specifico è l'override della sottoregione. Il test diagnostico ha anche escluso dal confronto i campi grid della `MapRegion`, perché il serializer JSON non li scrive; non li considera parte del round-trip verificato.

## 3. Planet Mapper → runtime generation

Il percorso di generazione è stato eseguito con il runner isolato e i documenti derivati:

| Documento in ingresso | Chunk ECS / lookup | TerrainSolver | Voxel non-Air e BlockID | Mesh CPU |
|---|---:|---:|---|---:|
| Tiny originale | 54/54 | 54/54 | 504.877, ID 3 | 99.252 triangoli |
| JSON con template P03d | 54/54 | 54/54 | 496.822: ID 2 = 2.048, ID 3 = 494.774 | 99.576 triangoli |
| JSON risalvato dal FWB caricato | 54/54 | 54/54 | 496.822: ID 3 = 496.822; nessun ID 2 | 99.576 triangoli |

Gli output runtime dei due round-trip non sono equivalenti. Nei trace runner-only il primo chunk divergente, in ordine face/col/row, è `(face=0,col=1,row=1)`: entrambi hanno 8.745 voxel solidi, ma hash voxel `0x5fbd07459448cd30` (JSON) contro `0x5249c14b5fbcef34` (FWB); il primo voxel diverso è `(x=0,y=65,z=11)`, BlockID 2 dal JSON e BlockID 3 dal FWB. Quattro chunk hanno hash voxel/mesh diversi. Questo è coerente con l'override `fairworld:dirt` perso nel binario.

I due runner custom ritornano exit 1 perché la suite completa contiene baseline e probe costruiti sul fixture `default_terrain`; il mesher rileva inoltre mismatch rispetto al suo hash/numero triangoli di riferimento e la probe di contatto non trova il medesimo campione solido. I conteggi di generazione, identità chunk, solver, BlockID e mesh sopra sono stati effettivamente prodotti; l'exit 1 non viene presentato come un PASS della suite custom.

Il catalogo UI Planet Mapper filtra i template per size (`PlanetMapperUI.cpp:180–183`) e crea l'istanza con template ID e coordinate (`:343–369`). `MapWorldGenerator` risolve per ID (`MapWorldGenerator.cpp:41–58`) ma non confronta `template.planetSize` con `planet.planetSize`. Pertanto il filtro UI non equivale a una validazione runtime: l'istanza documentale conserva i suoi campi, ma un mismatch di PlanetSize non è rigettato dal generatore osservato. **Mismatch guard: FAIL/staticamente assente.** Non è stata cambiata la logica.

## 4. Divergenza mesher Debug/Release

Comandi effettivi (stesso fixture e working directory):

```powershell
& 'D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03a-runner-bin\FairworldPlanetaryVoxelE2ETests.exe' --fixture 'D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json'
& 'D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03a-release-runner-bin\FairworldPlanetaryVoxelE2ETests.exe' --fixture 'D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json'
```

| Build | Exit | TerrainSolver | Mesh CPU | Esito |
|---|---:|---|---|---|
| Debug | 0 (22,692 s) | 54 chunk, 504.877 non-Air, ID 3 | 54 mesh; 297.756 vertici; 99.252 triangoli; invalid indices/posizioni/UV/normali/winding = 0; hash legacy `0x20c0d276200e0e20`, 135.820 triangoli | **PASS** |
| Release | 1 (0,883 s) | 54 chunk, 504.879 non-Air, ID 3 | 54 mesh; 297.768 vertici; 99.256 triangoli; stessi contatori di invalidità a zero; hash legacy `0x43bcceb03f81581c`, 135.824 triangoli | **FAIL** gate hash/conteggi |

Questi eseguibili hanno prodotto la direzione opposta ai valori riportati nel rapporto P03b (che riportava Debug 99.256 e Release 99.252). I valori qui sopra provengono dai binari e log eseguiti in questa fase e sono quelli da considerare per questa baseline. Le coordinate e i test P02a/P02b/P02c/P02d non segnalano divergenze nei rispettivi gate.

La prima divergenza **osservabile** è già dopo TerrainSolver: Release ha due voxel non-Air in più. Il mesher riceve quindi input diversi; non è dimostrato che la differenza di quattro triangoli sia una decisione float/culling del mesher. L'identità del primo chunk/voxel differente tra questi due build e la prima decisione di culling restano **NOT VERIFIED**.

È stato costruito e usato un runner Debug runner-only che salva per chunk hash voxel, blocchi e hash/vertici mesh; non modifica il mesher né le aspettative. La compilazione della variante Release è stata **BLOCKED**, exit 1, da `LNK1127: libreria danneggiata` su `p03a-release-core-bin\FairworldCore.lib`, dimensione osservata esattamente 1.073.741.824 byte. Nessuna rigenerazione o riparazione della libreria è stata tentata. I dati per-chunk Debug/Release necessari alla root cause non sono quindi disponibili.

## 5. Divergenza Grounded Debug/Release

Il runner completo Debug è exit 0; Release è exit 1. La scena controllata usa per entrambi collision surface radius 42.5, `Grounded=true`, un contatto solido e 120/120 tick di contatto stabile. La caduta radiale, il salto e il relanding riportano valori validi in entrambi.

La prima asserzione divergente è però un controllo **runner-only** in `TestPlanetaryGravityAnchoring`:

- codice: `PlanetaryVoxelE2ETests.cpp:2054–2057` calcola `contactMeshTopRadius` da `dbg_lastLookupCoord.y` e impone `radialSurfaceAgreement`;
- Debug: ultimo lookup y=82, `lastSolid=1`, radius calcolato 42.5, agreement true;
- Release: ultimo lookup y=84, `lastSolid=0`, sample status 2, radius calcolato 44.5, agreement false, benché il collision surface radius resti 42.5, `contacts=1` e `Grounded=true`;
- il test include `radialSurfaceAgreement` in `fallAndAnchorPass` (`:2149`), causando il FAIL aggregato.

`dbg_lastLookupCoord` è l'ultimo campione diagnostico, non l'identità del voxel di supporto. La differenza dimostra un difetto dell'asserzione diagnostica, non una perdita di contatto nel caso idle. Il gate Grounded del runner Release resta FAIL finché il runner non misura la superficie con il voxel di supporto/una query indipendente. Non è stata modificata la fisica.

Il caso diagonale del test locomozione ha gli stessi contatori osservati nei due log: 2 tick Grounded, 2 perdite, 7 blocchi tangenziali, clearance finale 2.9188; il sottocaso non basta da solo a spiegare la differenza di exit. Il primo FAIL Release della prova completa è il gate `radialSurfaceAgreement` appena descritto. Un trace per tick Debug/Release come richiesto in P03d non è stato aggiunto: servirebbe una strumentazione opt-in più mirata al test diagonale.

## 6. Upper-body-only e Missing/Pending

- **Upper-body-only fixture: NOT VERIFIED.** Il fixture P03a/P03b denominato `overhead` non è valido per questa conclusione: la verifica indipendente ha trovato `footSupport=1`. Non è stato creato un nuovo fixture in questa fase; nessuna prova dimostra o esclude il falso Grounded upper-body-only. Prima del solver il fixture successivo dovrà provare tutti i campioni inferiori Air, almeno un campione superiore Solid e zero Missing/Pending/OOB.
- **Missing/Pending: contratto ancora aperto, nessuna API cambiata.** `WorldChunkManager` conserva la mappa chiave→entity ma non l'intenzione/richiesta di streaming; per un chunk assente non sa distinguere Missing da Pending. Un entity registrato ma non generato è Pending. QueryBlockCartesian collassa più stati indisponibili; Raycast ha già Missing/Pending distinti.
- Proposta esistente da mantenere: separare `GeometryState` da `ChunkAvailability` (`Available`, `Missing`, `Pending`, `Invalid`) in un risultato tipizzato; Missing/Pending non diventano mai Solid/Grounded. Mantenere `QueryBlockCartesian` come wrapper legacy durante la migrazione. Per distinguere un chunk assente non richiesto da uno richiesto serve anche ownership/stato di request nello streaming manager.

## 7. Regression gate

Il full runner Tiny esegue al suo interno le prove P02a/P02b/P02c/P02d, P03/P03a e i controlli D1.2.19d.5/d6 pertinenti. Nei due log, i gate P02a, P02c, P02d, P03 e P03a sono PASS; Debug completa l'E2E. Release fallisce su mesh baseline e il gate aggregato radiale diagnostico sopra. Non sono stati eseguiti binari autonomi separati per ogni vecchio test D1.

| Verifica | Esito |
|---|---|
| Full runner Tiny Debug | **PASS**, exit 0, 22,692 s |
| Full runner Tiny Release | **FAIL**, exit 1, 0,883 s |
| Round-trip JSON + campi testati | **PASS** |
| Round-trip FWB semantic | **FAIL**, override sottoregione perso |
| WorldProjectManager Save/Load semantic | **FAIL**, usa FWB e perde lo stesso override |
| Runtime 54 chunk dal JSON custom | Generazione/solver PASS; suite custom **FAIL** sui riferimenti fixture-specific |
| Runtime 54 chunk dal FWB ricaricato | Generazione/solver PASS; dati differenti dal JSON, suite custom **FAIL** |
| Release first voxel/culling trace | **NOT VERIFIED**, link trace bloccato da archive Release danneggiata |
| Upper-body-only | **NOT VERIFIED** |
| GUI/GPU | **NOT RUN**, non autorizzate in questa fase |

Log disponibili: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\P03D_final_debug.log`, `P03D_final_release.log`, trace custom sotto `runtime\p03d-trace`, round-trip sotto `runtime\p03d-roundtrip`.

## 8. Correzioni minime proposte e ordine

1. **P03d.1 — FWB semantic parity:** estendere il record binario `MapRegion` per `influence` e `overrides`, versionare il formato (nuova versione) e mantenere il reader v7. Aggiungere test JSON↔FWB su sottoregioni e un test WorldProjectManager che verifica il documento effettivamente ricaricato dal percorso preferito. Non salvare automaticamente il JSON sopra l'originale durante la correzione.
2. **P03d.2 — Grounded test correction:** cambiare soltanto la diagnostica runner affinché misuri il supporto con `dbg_groundVoxelCoord` o query indipendente validata, mai con `dbg_lastLookupCoord`. Mantenere i criteri fisici esistenti.
3. **P03d.3 — Release first divergence:** ripristinare una libreria Release valida in un output completamente isolato tramite il progetto già generato, quindi produrre dump voxel per chunk Debug/Release. Individuare primo voxel differente; se gli input sono identici, confrontare prima decisione per voxel/face del builder. Non cambiare hash attesi.
4. **P03d.4 — Upper-body fixture:** costruire il fixture sintetico e validarlo tramite query indipendenti prima di chiamare PhysicsEngine. Se il mapping non consente la scena senza pending/out-of-bounds, documentare il limite invece di alterare Grounded.
5. **Missing/Pending API:** progettazione separata, con request ownership nello streaming manager e wrapper legacy; dipende dalla decisione sul contratto condiviso e non va accorpata alla correzione FWB.

## 9. Decisione

**NO-GO per chiudere P03d e NO-GO per P04.** Il round-trip ha trovato una perdita dati FWB riproducibile; l'uscita Release è negativa, anche se il suo FAIL Grounded proviene da una diagnostica runner difettosa; la root cause della divergenza Release voxel/mesh e il fixture upper-body-only restano aperti. Nessun freeze dichiarato.
