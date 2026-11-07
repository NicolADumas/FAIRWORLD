# D1.2.18a — Missing Planetary Chunk Root-Cause Isolation

## Conclusione

**Causa radice confermata:** la generazione sferica cerca chunk nel ChunkManager usando la chiave logica globale (global_cx, global_cz), mentre GameWorld::CreateChunkEntity(name, position) registra la nuova entità usando coordinate ricavate dalla posizione sferica:

    int cx = (int)position.x / 16;
    int cz = (int)position.z / 16;
    m_chunkManager.RegisterChunkEntity(cx, cz, entity);

Le coordinate globali della griglia planetaria e quelle ottenute quantizzando la posizione sferica non appartengono allo stesso spazio. Sul pianeta Tiny molte posizioni ricadono in sole cinque chiavi manager. Una registrazione successiva sovrascrive un'entità già associata a una coordinata globale valida; il generatore la riutilizza e sovrascrive il suo PlanetChunkCoord. Questo spiega deterministamente i 53 chunk ECS distinti invece di 54.

Non sono stati i return anticipati: in entrambe le esecuzioni risultano 54 candidati, 0 esclusioni, 54 risultati della lambda e 54 tentativi di coordinata, ma soltanto 53 entità ECS distinte per un riuso. Il limite di raggio era disattivato (limitRadius=0).

## Evidenza del candidato mancante

Per face=0, col=2, row=2, il generatore calcola global (2,0) e posizione sferica (11.6417,-11.6417,17.4626). Il lookup globale iniziale è assente. Viene creata entity 3, con VoxelChunkComponent.cx/cz=(0,1), e registrata sotto (0,1), mentre la lookup della chiave richiesta (2,0) resta assente.

Il candidato successivo con global (0,1) trova entity 3 nel manager e la riutilizza. La traccia documenta che prima del riuso essa conteneva ancora PlanetChunkCoord(face=0,col=2,row=2). Il generatore poi assegna alla stessa entità le coordinate del candidato (0,1), facendo sparire l'identità ECS per face=0,col=2,row=2. Il runner verifica poi che tale coordinata manca e che il lookup manager (2,0) è assente.

## Percorso e verifiche

- Generazione candidati: MapWorldGenerator::Generate, loop sferico e lambda generateChunk in src/world/MapWorldGenerator.cpp.
- Riuso lookup: targetWorld.GetChunkManager().GetChunkEntity(global_cx, global_cz).
- Creazione/registrazione: GameWorld::CreateChunkEntity in src/world/GameWorld.cpp.
- Indice: WorldChunkManager::RegisterChunkEntity / GetChunkEntity in src/world/WorldChunkManager.h.
- GameWorld::CreateChunkEntity ignora la chiave globale ricevuta dal chiamante e ricava cx/cz dalla posizione; il chiamante della mappa si aspetta invece la griglia globale.
- PlanetChunkCoord.row viene assegnato correttamente dal convertitore legacy/canonico: non è la causa dell'entità mancante.
- Fixture: Tiny, N=3, sferico, default_terrain, singola istanza; hash e dimensione identici al baseline.
- 54 coppie globali candidate e ID WorldChunk_x_z univoci. Nessuna collisione fra gli ID logici; collisione/alias avviene nel mapping manager → entità.
- Tutte le entità create risultano registrate sotto il proprio VoxelChunkComponent.cx/cz; la discrepanza è che tali componenti sono inizializzati dalla posizione e non dalla chiave globale richiesta.
- Le due esecuzioni producono log byte-identici, 54 candidati, 0 esclusioni, 54 risultati, 53 entity ID distinti e un riuso.
- Nel fixture limitRadius=0: nessun candidato è tagliato dal raggio.
- La tabella seguente è la traccia completa della prima esecuzione. La seconda è identica.

Comando di build usato con il preset/toolchain Debug esistente:

    & 'D:\Visual Code\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build '.\FAIRWORLD\build_r3z7' --config Debug --target FairworldPlanetaryVoxelE2ETests -- /m:1 /verbosity:minimal

Exit code build finale dopo la rimozione della strumentazione: 0. Un tentativo ravvicinato durante il rebuild ha incontrato LNK1104 sul file FairworldCore.lib mentre una build precedente stava ancora terminando; dopo aver atteso e serializzato le build, il target è stato compilato con successo.

Comando usato due volte, con working directory `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`:

    & 'D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldPlanetaryVoxelE2ETests.exe' --fixture 'D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json'

## Tabella completa candidati e registrazione
| Face | Col,row canoniche | Global | ID logico | Posizione sferica | Manager prima | ECS entity | Registrazione (chunk.cx,cz) | Key globale risolve entity | Esito |
|---:|---:|---:|---|---|---|---:|---:|---|---|
| 0 | 0,2 | 0,0 | WorldChunk_0_0 | (-11.6417,-11.6417,17.4626) | missing | 1 | 0,1 | no | creata |
| 0 | 1,2 | 1,0 | WorldChunk_1_0 | (0,-13.3128,19.9692) | missing | 2 | 0,1 | no | creata |
| 0 | 2,2 | 2,0 | WorldChunk_2_0 | (11.6417,-11.6417,17.4626) | missing | 3 | 0,1 | no | creata |
| 0 | 0,1 | 0,1 | WorldChunk_0_1 | (-13.3128,0,19.9692) | 3 | 3 | 0,1 | yes | riuso; precedente entity/coord: 3; chunk=(0,1); planet=(face=0,col=2,row=2) |
| 0 | 1,1 | 1,1 | WorldChunk_1_1 | (0,0,24) | missing | 4 | 0,1 | no | creata |
| 0 | 2,1 | 2,1 | WorldChunk_2_1 | (13.3128,0,19.9692) | missing | 5 | 0,1 | no | creata |
| 0 | 0,0 | 0,2 | WorldChunk_0_2 | (-11.6417,11.6417,17.4626) | missing | 6 | 0,1 | no | creata |
| 0 | 1,0 | 1,2 | WorldChunk_1_2 | (0,13.3128,19.9692) | missing | 7 | 0,1 | no | creata |
| 0 | 2,0 | 2,2 | WorldChunk_2_2 | (11.6417,11.6417,17.4626) | missing | 8 | 0,1 | no | creata |
| 1 | 0,2 | 3,0 | WorldChunk_3_0 | (11.6417,-11.6417,-17.4626) | missing | 9 | 0,-1 | no | creata |
| 1 | 1,2 | 4,0 | WorldChunk_4_0 | (0,-13.3128,-19.9692) | missing | 10 | 0,-1 | no | creata |
| 1 | 2,2 | 5,0 | WorldChunk_5_0 | (-11.6417,-11.6417,-17.4626) | missing | 11 | 0,-1 | no | creata |
| 1 | 0,1 | 3,1 | WorldChunk_3_1 | (13.3128,0,-19.9692) | missing | 12 | 0,-1 | no | creata |
| 1 | 1,1 | 4,1 | WorldChunk_4_1 | (0,0,-24) | missing | 13 | 0,-1 | no | creata |
| 1 | 2,1 | 5,1 | WorldChunk_5_1 | (-13.3128,0,-19.9692) | missing | 14 | 0,-1 | no | creata |
| 1 | 0,0 | 3,2 | WorldChunk_3_2 | (11.6417,11.6417,-17.4626) | missing | 15 | 0,-1 | no | creata |
| 1 | 1,0 | 4,2 | WorldChunk_4_2 | (0,13.3128,-19.9692) | missing | 16 | 0,-1 | no | creata |
| 1 | 2,0 | 5,2 | WorldChunk_5_2 | (-11.6417,11.6417,-17.4626) | missing | 17 | 0,-1 | no | creata |
| 2 | 0,2 | 6,0 | WorldChunk_6_0 | (17.4626,-11.6417,11.6417) | missing | 18 | 1,0 | no | creata |
| 2 | 1,2 | 7,0 | WorldChunk_7_0 | (19.9692,-13.3128,0) | missing | 19 | 1,0 | no | creata |
| 2 | 2,2 | 8,0 | WorldChunk_8_0 | (17.4626,-11.6417,-11.6417) | missing | 20 | 1,0 | no | creata |
| 2 | 0,1 | 6,1 | WorldChunk_6_1 | (19.9692,0,13.3128) | missing | 21 | 1,0 | no | creata |
| 2 | 1,1 | 7,1 | WorldChunk_7_1 | (24,0,0) | missing | 22 | 1,0 | no | creata |
| 2 | 2,1 | 8,1 | WorldChunk_8_1 | (19.9692,0,-13.3128) | missing | 23 | 1,0 | no | creata |
| 2 | 0,0 | 6,2 | WorldChunk_6_2 | (17.4626,11.6417,11.6417) | missing | 24 | 1,0 | no | creata |
| 2 | 1,0 | 7,2 | WorldChunk_7_2 | (19.9692,13.3128,0) | missing | 25 | 1,0 | no | creata |
| 2 | 2,0 | 8,2 | WorldChunk_8_2 | (17.4626,11.6417,-11.6417) | missing | 26 | 1,0 | no | creata |
| 3 | 0,2 | 0,3 | WorldChunk_0_3 | (-17.4626,-11.6417,-11.6417) | missing | 27 | -1,0 | no | creata |
| 3 | 1,2 | 1,3 | WorldChunk_1_3 | (-19.9692,-13.3128,0) | missing | 28 | -1,0 | no | creata |
| 3 | 2,2 | 2,3 | WorldChunk_2_3 | (-17.4626,-11.6417,11.6417) | missing | 29 | -1,0 | no | creata |
| 3 | 0,1 | 0,4 | WorldChunk_0_4 | (-19.9692,0,-13.3128) | missing | 30 | -1,0 | no | creata |
| 3 | 1,1 | 1,4 | WorldChunk_1_4 | (-24,0,0) | missing | 31 | -1,0 | no | creata |
| 3 | 2,1 | 2,4 | WorldChunk_2_4 | (-19.9692,0,13.3128) | missing | 32 | -1,0 | no | creata |
| 3 | 0,0 | 0,5 | WorldChunk_0_5 | (-17.4626,11.6417,-11.6417) | missing | 33 | -1,0 | no | creata |
| 3 | 1,0 | 1,5 | WorldChunk_1_5 | (-19.9692,13.3128,0) | missing | 34 | -1,0 | no | creata |
| 3 | 2,0 | 2,5 | WorldChunk_2_5 | (-17.4626,11.6417,11.6417) | missing | 35 | -1,0 | no | creata |
| 4 | 0,2 | 3,3 | WorldChunk_3_3 | (-11.6417,17.4626,11.6417) | missing | 36 | 0,0 | no | creata |
| 4 | 1,2 | 4,3 | WorldChunk_4_3 | (0,19.9692,13.3128) | missing | 37 | 0,0 | no | creata |
| 4 | 2,2 | 5,3 | WorldChunk_5_3 | (11.6417,17.4626,11.6417) | missing | 38 | 0,0 | no | creata |
| 4 | 0,1 | 3,4 | WorldChunk_3_4 | (-13.3128,19.9692,0) | missing | 39 | 0,0 | no | creata |
| 4 | 1,1 | 4,4 | WorldChunk_4_4 | (0,24,0) | missing | 40 | 0,0 | no | creata |
| 4 | 2,1 | 5,4 | WorldChunk_5_4 | (13.3128,19.9692,0) | missing | 41 | 0,0 | no | creata |
| 4 | 0,0 | 3,5 | WorldChunk_3_5 | (-11.6417,17.4626,-11.6417) | missing | 42 | 0,0 | no | creata |
| 4 | 1,0 | 4,5 | WorldChunk_4_5 | (0,19.9692,-13.3128) | missing | 43 | 0,0 | no | creata |
| 4 | 2,0 | 5,5 | WorldChunk_5_5 | (11.6417,17.4626,-11.6417) | missing | 44 | 0,0 | no | creata |
| 5 | 0,2 | 6,3 | WorldChunk_6_3 | (-11.6417,-17.4626,-11.6417) | missing | 45 | 0,0 | no | creata |
| 5 | 1,2 | 7,3 | WorldChunk_7_3 | (0,-19.9692,-13.3128) | missing | 46 | 0,0 | no | creata |
| 5 | 2,2 | 8,3 | WorldChunk_8_3 | (11.6417,-17.4626,-11.6417) | missing | 47 | 0,0 | no | creata |
| 5 | 0,1 | 6,4 | WorldChunk_6_4 | (-13.3128,-19.9692,0) | missing | 48 | 0,0 | no | creata |
| 5 | 1,1 | 7,4 | WorldChunk_7_4 | (0,-24,0) | missing | 49 | 0,0 | no | creata |
| 5 | 2,1 | 8,4 | WorldChunk_8_4 | (13.3128,-19.9692,0) | missing | 50 | 0,0 | no | creata |
| 5 | 0,0 | 6,5 | WorldChunk_6_5 | (-11.6417,-17.4626,11.6417) | missing | 51 | 0,0 | no | creata |
| 5 | 1,0 | 7,5 | WorldChunk_7_5 | (0,-19.9692,13.3128) | missing | 52 | 0,0 | no | creata |
| 5 | 2,0 | 8,5 | WorldChunk_8_5 | (11.6417,-17.4626,11.6417) | missing | 53 | 0,0 | no | creata |

## Risultati operativi

| Controllo | Risultato |
|---|---|
| Build Debug FairworldPlanetaryVoxelE2ETests | PASS, exit code 0 |
| Esecuzione 1, working directory sandbox | FAIL, exit code 1, 345 ms |
| Esecuzione 2, working directory sandbox | FAIL, exit code 1, 299 ms |
| Determinismo log 1/2 | PASS, SHA-256 identico: DAC49D525C0EE985B7EC76A9E2B08CF1FC31A7380734BCDC187FA9BB0B53F892 |
| Candidati globali univoci | 54/54 |
| Esclusioni per trasformazione, raggio, legacy row, OuterBoundary | 0 |
| Risultati della lambda generateChunk | 54/54 |
| Entità ECS distinte | 53 |
| Riusi di entità con precedente PlanetChunkCoord diverso | 1 |
| Dimensione finale indice manager | 5 chiavi osservate |
| Hash originali e fixture prima/dopo | 10/10 invariati |

Log delle esecuzioni:
- D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18a_run_1.log
- D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18a_run_2.log

Manifest D1.2.18 originale lasciato intatto. Snapshot pre/post di questo audit:
- D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18a_hashes_before.csv
- D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18a_hashes_after.csv

## Patch minima proposta — non applicata

Aggiungere un percorso esplicito per creare/registrare un chunk con la sua chiave logica, per esempio un overload di GameWorld::CreateChunkEntity che riceva global_cx/global_cz e usi tali valori per VoxelChunkComponent.cx/cz e WorldChunkManager::RegisterChunkEntity. Il percorso legacy può conservare la registrazione derivata dalla posizione per i caller flat già esistenti.

MapWorldGenerator dovrebbe chiamare l'overload esplicito nella generazione sferica. Evitare la soluzione “crea con la vecchia API e poi registra anche la chiave logica”: lascerebbe l'alias spaziale nel manager e potrebbe consentire ulteriori riusi errati. Il test di regressione dovrà verificare, per tutte le 54 coordinate, lookup globale → entità unica, corrispondenza VoxelChunkComponent/PlanetChunkCoord e stabilità dopo il completamento di tutti i candidati.

La patch non è stata applicata. I log sono stati conservati nel sandbox. La strumentazione temporanea è stata rimossa da MapWorldGenerator.cpp; la modifica preesistente D1.2 relativa a PlanetGridCompatibility è rimasta intatta. Nessun fixture o salvataggio è stato modificato; nessun commit/push; nessun freeze D1.
