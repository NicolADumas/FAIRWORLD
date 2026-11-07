# D1.2.19d.4 — Planet Surface Contact & Radial Velocity Audit

**Modalità:** audit statico e riproduzione CPU/headless deterministica. Nessuna GUI avviata; nessuna correzione funzionale applicata; nessun commit, push o rebaseline.

## Esito sintetico

- **Grounded intermittente:** riprodotto nel test CPU di riposo. Nel caso deterministico, il primo passaggio `Grounded YES → NO` avviene al **tick 0**, senza input di salto e senza `TerrainPending`. Al campione di perdita il solver trova Air e zero contatti solidi; la gravità è diretta verso il centro e la velocità radiale dopo la collisione è negativa.
- **Velocità verso l’esterno causata da `ResolveSphericalCollisions`:** non riprodotta nei tracciati idle e walk. Ai tick 17 e 23 la correzione di posizione produce un delta radiale positivo, ma cancella soltanto la velocità integrata verso l’interno: la velocità radiale finale è zero. Nessun valore positivo finale è stato osservato in questi tracciati.
- **Salto:** il singolo tick Space genera intenzionalmente velocità radiale positiva tramite l’impulso normalizzato D1.2.19d.3. La traccia misura `1.60897` dopo il movimento e `1.59485` dopo gravità/collisione; non è un lancio generato dalla correzione collisioni.
- **Discrepanza collider/mesh CPU:** confermata per il voxel di superficie del fixture Tiny. Le formule attuali collocano il limite di collisione a raggio `82`, mentre la faccia superiore del voxel mesh corrispondente è a raggio `42.5`: differenza `39.5` unità. Questo supporta la barriera invisibile osservata, ma la posizione effettiva della mesh visualizzata sulla GPU resta **NOT VERIFIED**.
- **Tick GUI del sintomo originale:** non determinabile senza log per-tick della sessione GUI; la GUI non è stata avviata come richiesto.

## Percorso fisico e diagnostica

`PhysicsEngine::StepSimulation` applica gravità, drag, integrazione e poi collisioni (`src/systems/PhysicsEngine.cpp:12-61`). Per la sfera, `ResolveCollisions` azzera `isGrounded` e i contatori all’inizio di ogni tick (`PhysicsEngine.cpp:146-179`). `ResolveSphericalCollisions` separa il movimento in componenti radiale e tangenziali, campiona voxel solidi e può impostare Grounded solo quando un movimento radiale discendente è bloccato da `SolidVoxel` (`PhysicsEngine.cpp:737-876`).

Al termine il solver ricostruisce la velocità da `(posizione finale − posizione pre-collisione) / dt` (`PhysicsEngine.cpp:908-910`). Di conseguenza una correzione radiale della posizione può diventare una velocità apparente. Le due correzioni radiali potenziali sono la depenetrazione iniziale (`PhysicsEngine.cpp:854-862`) e lo step-up tangenziale fino a `0.6` unità (`PhysicsEngine.cpp:837-847`). I tracciati eseguiti non hanno attivato una correzione che producesse velocità radiale finale positiva; i due percorsi rimangono da verificare con un caso mirato di ostacolo/step-up.

Il movimento Survival modifica la componente tangenziale, conserva la componente radiale e applica Space solo quando Grounded; il salto è scalato con `sqrt(localGravity / G_EARTH)` (`src/systems/Systems.cpp:197-215`). In modalità Creative Space aggiunge invece velocità lungo Up e la gravità è esclusa dal percorso previsto; lo stato della modalità della GUI originaria non è disponibile. Il fixture CPU è in Survival.

## Riproduzione deterministica

Fixture: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\saves\map\world_map.json` — Tiny, risoluzione 3, sferico, un’istanza attiva, TerrainSolver completato. Eseguibile: `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldPlanetaryVoxelE2ETests.exe`. La build diagnostica del target è terminata con exit code `0`; il runner è terminato con exit code `1` perché la nuova verifica di stabilità idle rileva la perdita di Grounded (fallimento diagnostico intenzionale, non PASS). Log: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\logs\d19d4_contact_audit_cpu.log`.

### Primo tick di perdita a riposo

Stato all’inizio: `Grounded=1`, body `(0,0,82.0399)`, velocità radiale `0`, ultimo contatto solido voxel `(24,82,24)`, un contatto, nessun pending.

Al **tick 0** senza salto:

| Misura | Valore |
| --- | ---: |
| Body prima → dopo collisione | `(0,0,82.0399)` → `(0,0,82.0397)` |
| Camera dopo sincronizzazione | `(0,0,83.6397)` |
| Gravità | `(0,0,-0.839539)` |
| Velocità radiale integrata → finale | `-0.0139923` → `-0.0141907` |
| Correzione radiale collisione | `-0.000198348` (verso l’interno) |
| Sample al momento della perdita | Air, voxel `(24,84,23)` |
| Contatti solidi / TerrainPending | `0 / 0` |
| Clearance / penetrazione diagnostica | `0.0396805 / 0` |

Pertanto questa perdita di Grounded **non** è prodotta da una velocità radiale positiva né da TerrainPending: il flag viene ricalcolato da zero e il tick corrente non incontra un voxel solido durante la discesa. Il body continua inizialmente a cadere verso il pianeta.

### Correzioni radiali collisioni

In 120 tick idle, i log mostrano un massimo delta radiale positivo collisione di `0.255217` al tick 17 e `0.0849508` al tick 23. In entrambi i casi la velocità integrata era rispettivamente `-0.255217` e `-0.0849508`; la velocità radiale dopo la collisione è `0` e la posizione radiale non cambia. `maxPositivePostCollisionRadialVelocity=0`, `maxPenetration=0`.

Un tracciato walk di massimo 600 tick registra `firstOutwardVelocityTick=-1`, velocità radiale positiva massima circa `9.67e-18` (rumore numerico), Grounded finale e zero TerrainPending. Non è quindi stata dimostrata una spinta verso l’esterno da parte del solver nei casi riprodotti.

### Space

Space viene consumuto al tick 0 mentre Grounded è vero. Il movimento assegna velocità radiale `1.60897`; dopo integrazione, gravità e collisione risulta `1.59485`. La correzione radiale collisione è soltanto `+0.0000595`; il sample è Air e il flag Grounded torna falso, coerentemente con l’inizio del salto.

Il test D3 precedente documentava inverse-square gravity e salto normalizzato con apice `1.53961`, successivo rientro e atterraggio CPU. La presente traccia non invalida quel risultato: aggiunge il caso di contatto idle, nel quale il flag non rimane stabile tra tick. Non è stata riprodotta qui una traiettoria di salto completa diversa dal test D3.

## Coerenza voxel di collisione e mesh

Il generatore di coordinate usato da query collisione assegna `out_localY = (distance − R) + 25` (`src/world/MapWorldGenerator.cpp:349-365`). Anche il resolver spawn interpreta la cella `y` con limite esterno `R + (y + 1 − 25)` (`src/world/GameWorld.cpp:1077-1081`). Per Tiny `R=24` e cella solida `y=82`, questo dà raggio di collisione `24 + (83 − 25) = 82`.

Il solver terreno e la conversione mesh usano invece `PlanetRadialMapping::ReferenceVoxelOffset = VoxelsY/2 = 64` (`src/world/PlanetRadialMapping.h:16-24`); la mappatura è `g = layer*VoxelsY + localY − 64`, distanza `R+g` (`src/world/PlanetRadialMapping.cpp:6-18`). Il mesher costruisce la faccia superiore a `localY=82.5` per il voxel centrato a `y=82` (`src/world/VoxelChunkMesher.cpp`, faccia Top) e la mesh planetaria applica questa mappatura (`src/world/PlanetaryMeshGenerator.cpp:21-35`). Ne segue `24 + (82.5 − 64) = 42.5`. Il log diagnostico riporta la differenza `82 − 42.5 = 39.5`.

Questa è una discrepanza di contratto tra coordinate collisione/spawn e coordinate radiali mesh/terrain, verificata nel codice e nel fixture CPU. È una causa forte e concreta della barriera apparente. L’eventuale alterazione aggiuntiva dovuta a mesh upload, trasformazioni o frame effettivamente presentato non è misurata; non viene dichiarata una verifica visuale.

## TerrainPending e caricamento

Nel tick di perdita di contatto della riproduzione non compare `TerrainPending`. Il percorso di query distingue `SolidVoxel`, `Air`, `OutOfBounds` e `TerrainPending`; i due stati non-solid possono impedire un movimento ma non impostano Grounded. Quando una query è pending e la posizione resta ferma, la ricostruzione della velocità da delta posizione non crea da sola una spinta radiale positiva. Una sessione con pending intermittente può quindi contribuire a Grounded instabile, ma non è la causa del tick CPU identificato e non spiega da sola il moto verso l’esterno.

La durata di caricamento superiore a 10 secondi non è stata misurata in questo audit. La riproduzione headless del fixture già risolto ha richiesto circa 2.6–3.1 secondi; questo dato non rappresenta il caricamento GUI/streaming completo.

## Modifiche e test

- Nessun sorgente di produzione è stato modificato per questo audit.
- È stata mantenuta la strumentazione diagnostica già aggiunta al test `src/world/PlanetaryVoxelE2ETests.cpp`; registra stato iniziale, perdita idle, delta radiali, Space e walk senza cambiare il solver.
- Build Debug del target `FairworldPlanetaryVoxelE2ETests`: **PASS**, exit code `0`; log `D1_2_19D4_diagnostic_build.log`.
- Runner Tiny: **FAIL diagnostico riproducibile**, exit code `1`, perché la condizione di stabilità del contatto idle fallisce al tick 0. I gruppi precedenti stampati dal runner risultano PASS; il test integrato finale è FAIL.
- `git diff --check`: **FAIL** (exit code `1`) sull'intera working tree preesistente; segnala trailing whitespace in righe aggiunte di `src/states/BlockMakerState.cpp` (linee 4652-4658, 4668-4669, 11563, 11570-11571, 11806, 11828, 11830). Il rapporto D1.2.19d.4 ha zero righe con whitespace finale. Queste segnalazioni non sono state modificate nell'audit.
- GUI/GPU, posizione della mesh effettivamente presentata, tick esatto della sessione GUI e casi di step-up/depenetration con spinta outward: **NOT VERIFIED**.
- Confronto SHA-256 degli originali della lista D1.2.19d.3: **40/40 invariati** nel workspace rispetto a `D1_2_19D2_SANDBOX\logs\originals_post_d19d3.csv`; le due divergenze storiche rispetto al manifest originario restano quelle già documentate. Nessun ripristino o rebaseline.

## Unica correzione minima proposta

Centralizzare in un unico helper la conversione tra raggio planetario e indice/limite radiale voxel, quindi far usare lo stesso riferimento sia alle query/spawn collisione (`25` attuale) sia al solver e alla mesh (`ReferenceVoxelOffset=64` attuale), dopo aver fissato con test quale mapping rappresenta il contratto fisico corretto. La patch deve preservare una corrispondenza uno-a-uno tra voxel solido, superficie collisionale e faccia mesh; non aggiungere correzioni radiali artificiali né alterare il salto o la gravità. Non applicata in questo audit.

**Conclusione:** la perdita di Grounded è riprodotta come mancata riconferma del contatto al tick successivo; il solver non ha generato velocità outward nei tracciati senza salto. La discrepanza radiale di `39.5` unità è confermata e giustifica una correzione minima coordinata del mapping. Il comportamento della GUI originale e il rendering GPU restano da verificare in una sessione autorizzata. **D1 non è dichiarato FROZEN.**
