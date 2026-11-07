# FAIRWORLD — D1.2.12a / Raycast Failure Isolation

**Data:** 2026-11-04  
**Classificazione:** **SCENE NOT QUERYABLE**  
**Stato D1:** INCOMPLETE; nessun freeze dichiarato.  
**Modalità:** strumentazione temporanea, validazione e una sessione GUI sandbox.

## 1. Sintesi

Planet Mapper invia effettivamente query raycast mentre il puntatore è nel viewport. In questa sessione sono state inviate 1.572 query, tutte in modalità Voxel; tutte hanno restituito miss. La fotografia della scena registrata alla prima query mostra zero entità con la coppia di componenti richiesta dal raycast Voxel (VoxelChunkComponent + TransformComponent) e zero chunkInstances nel documento.

Quindi il mancato log D1.2.11 non è causato principalmente dai gate di input né da un filtro che scarta hit esistenti: il raycast non ha bersagli voxel interrogabili nella scena osservata. La preview LOD visibile è una mesh procedurale; non è, di per sé, una VoxelChunkComponent registrata per CastVoxel.

## 2. Audit statico del percorso

### PlanetMapperCamera::Update

File: src/states/components/PlanetMapperCamera.cpp.

1. Se SharedContext è null, Update termina prima di leggere ImGui o costruire la query.
2. Il gate camera richiede !io.WantCaptureMouse e MousePos.x >= windowWidth * 0.45. Il commento parla del 65% destro, ma la soglia effettiva del controllo è 45%.
3. Anche superato il gate camera, la query è bloccata quando ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) è true.
4. Solo dopo questi gate la funzione costruisce l’origine dalla posizione camera, calcola la direzione dal puntatore e chiama RaycastSystem::Cast.
5. La modalità è assegnata esplicitamente a RaycastMode::Voxel, maxDistance=10000. Auto e Sphere non vengono selezionati da questo chiamante.
6. Il ramo che salva m_lastRayHit viene eseguito dopo Cast. Se i gate falliscono, il codice imposta solo m_lastRayHit.hit=false.

### RaycastSystem::Cast e CastVoxel

File: src/systems/RaycastSystem.cpp; definizioni in src/systems/RaycastSystem.h.

- Cast inoltra la query Voxel a CastVoxel.
- La modalità Auto avrebbe selezionato Sphere durante PlanetMapper, ma non è usata da PlanetMapperCamera perché il chiamante imposta Voxel.
- CastVoxel restituisce subito un miss se forgeWorld è null.
- Altrimenti interroga soltanto registry.view<VoxelChunkComponent, TransformComponent>().
- Per ciascun candidato prova prima il limite AABB e poi la traversata DDA. Un voxel conta come superficie valida solo se il blocco non è Air, Water o OutOfBounds.
- Se la coppia di componenti non esiste, la view è vuota e la query restituisce miss senza poter colpire la preview mesh.
- CastVoxel imposta type=Voxel anche sul risultato miss. Per questo il conteggio dei tipi hit è stato effettuato solo quando hit=true.

### Blocco D1.2.11

Il blocco di conversione face/UV/chart/grid è dentro la condizione hit.hit && hit.type == Voxel. La riga RAYCAST viene poi emessa solo quando cambia faccia, risoluzione, colonna o riga. Un miss non può entrare nel blocco; un hit di tipo Sphere/Physics non lo supererebbe.

## 3. Diagnostica temporanea

Strumentazione delimitata in:

- src/states/components/PlanetMapperCamera.cpp
- src/states/components/PlanetMapperCamera.h

Sono stati aggiunti contatori in memoria, tre esempi massimi di miss e un riepilogo alla distruzione della camera. Non viene stampata una riga per frame. La query, i gate, la selezione e l’assegnazione a m_lastRayHit non sono stati cambiati.

Il primo tentativo di build ha segnalato che registry.alive() non esiste in questa versione EnTT. La sola lettura diagnostica è stata corretta usando GameWorld::GetLiveEntityCount(); non è stato toccato il raycast di produzione.

## 4. Esecuzione e risultati

### Gate prima della GUI

Comandi usati:

~~~powershell
git diff --check
& 'D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe' 'build_r3z7\ALL_BUILD.vcxproj' '/p:Configuration=Debug' '/m:1' '/v:minimal' '/nologo'
& 'D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe' --validate-d12
& 'D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\Verify-OriginalInputs.ps1'
~~~

| Gate | Risultato |
|---|---|
| git diff --check | PASS, exit code 0; soltanto avvisi Git sulla conversione LF/CRLF |
| Build Debug completa, ALL_BUILD, MSBuild /m:1 | PASS, exit code 0 |
| FairworldEditor.exe --validate-d12 | PASS, exit code 0; i quattro gruppi D1.2 PASS |
| Hash pre-GUI degli originali | PASS: ORIGINAL_INPUT_HASHES_MATCH=40 |
| Reparse point nel sandbox | PASS: nessuno |
| Shader SPIR-V | PASS: 11 disponibili |

### Sessione Planet Mapper

Working directory obbligatoria: D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime  
Eseguibile: D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe  
Comando effettivo:

~~~powershell
cmd.exe /c '"D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe" --disable-twitch-layer < inputs\planetmapper.stdin > logs\D1_2_12a_planetmapper.stdout.log 2>&1'
~~~

Exit code processo: **0**, rilevato dalla sessione di esecuzione dopo la chiusura normale della finestra. Il puntatore è stato portato e cliccato più volte nell’area visibile del globo. La GUI non ha modificato i dati del fixture.

### Contatori aggregati

Evidenza: runtime/logs/D1_2_12a_planetmapper.stdout.log.

| Contatore | Valore |
|---|---:|
| Update camera | 2.009 |
| Update senza SharedContext | 0 |
| Mouse nell’area viewport (x >= 35% finestra) | 2.009 |
| Mouse oltre il gate camera (x >= 45%) | 1.572 |
| Blocchi WantCaptureMouse | 437 |
| Fuori dal gate x >= 45% | 0 |
| Blocchi ImGui AnyWindow hovered | 0 |
| Query inviate | 1.572 |
| Query modalità Voxel | 1.572 |
| Hit totali | 0 |
| Miss totali | 1.572 |
| Hit Voxel / Sphere / Physics / altri | 0 / 0 / 0 / 0 |
| Log D1.2.11 emessi | 0 |

I 437 blocchi WantCaptureMouse non spiegano l’assenza completa dei log: nelle altre 1.572 chiamate la query è stata inviata, ma nessuna ha colpito.

### Campioni miss

I tre campioni conservati riportano tutti:

- mode=Voxel
- origin=(-166.1, 85.5, 166.1)
- direction=(0.9, -0.1, 0.4)
- maxDistance=10000
- voxelTargets=0
- reason=no-voxel-transform-targets

Le coordinate sono formattate dal logger a una cifra decimale.

## 5. Stato della scena

Alla prima query è stata acquisita una fotografia esatta dei dati disponibili al raycast:

| Dato | Conteggio |
|---|---:|
| Pianeti nel documento | 1 |
| ChunkInstances nel documento | 0 |
| Entità vive nel registry del preview world | 1 |
| Entità MeshComponent + TransformComponent | 0 |
| Entità VoxelChunkComponent + TransformComponent | 0 |

Il fixture contiene un pianeta, ma chunkInstances è vuoto. PlanetMapperState crea e assegna un preview GameWorld; aggiorna poi SphericalLODSystem. SphericalLOD genera una mesh procedurale e la accoda con EnqueueDeferredMesh. Quel percorso genera MeshComponent, non popola VoxelChunkComponent. Il raycast Voxel non interroga la geometria MeshComponent.

La fotografia del registry è presa alla prima query. Le successive query hanno tutte dato miss; i tre campioni iniziali hanno tutti zero bersagli. Il codice del preview LOD conferma comunque che quel percorso costruisce mesh, non chunk voxel interrogabili.

## 6. Classificazione causale

- **INPUT BLOCKED:** causa secondaria presente in 437 update per WantCaptureMouse; non è la causa determinante, perché 1.572 query sono state inviate.
- **SCENE NOT QUERYABLE:** causa principale dimostrata. Il documento non ha chunkInstances e la view richiesta da CastVoxel contiene zero candidati nella fotografia della scena.
- **RAYCAST MISS:** risultato osservato per tutte le query, ma non attribuito a un raggio che manca geometria voxel presente: i candidati voxel sono assenti.
- **HIT FILTERED:** non supportato; sono stati osservati zero hit di qualsiasi tipo.
- **RAYCAST VERIFIED:** no; nessun hit voxel.
- **Difetto di formula/raycast:** non dimostrato da questa sessione. Il problema isolato è la discrepanza fra modalità/capacità di query e contenuto della scena.

## 7. Shutdown e integrità

- Vulkan Validation Layer: attivo secondo il log di inizializzazione.
- Errori Validation Layer/VUID: 0.
- Shutdown runtime: PASS.
- Semafori: 6 creati, 6 distrutti, 0 live; Ownership Gate CLEAN.
- Exit code: 0.
- Hash originali dopo la GUI: ORIGINAL_INPUT_HASHES_MATCH=40.
- Hash fixture JSON prima/dopo: 3B6F824C859AEA23BCFCC0847911454F108AAA4E790FA24A9896DADBE8EB27B3, invariato.
- Hash FWB sandbox: E0081F607082173856496120BB12B8CBA04258F61E3F3DBF165703ED8E594F3A, invariato.
- Scrittura osservata nel runtime sandbox: aggiornamento di imgui.ini; log della sessione. Nessuna scrittura agli originali.

## 8. Correzione minima proposta — non applicata

1. Per la preview planetaria sferica, valutare una query Sphere (o Auto, che in PlanetMapper si risolve a Sphere) e verificare che il risultato sia coerente col terreno visualizzato e con le coordinate D1.2. Questo è il percorso minimo per ottenere face/UV sulla superficie procedurale corrente.
2. Se il requisito è invece selezionare blocchi voxel esatti, fornire al raycast dati voxel con VoxelChunkComponent + TransformComponent oppure introdurre un’intersezione dedicata alla mesh LOD. Non trattare una mesh di preview come chunk voxel senza un contratto esplicito.
3. Aggiungere test d’integrazione distinti per query Sphere sulla preview e Voxel su chunk reali. Confermare inoltre che l’input viewport non venga catturato da ImGui nelle coordinate di test.

La scelta tra selezione della superficie planetaria e selezione del blocco richiede revisione funzionale. Nessuna correzione è stata applicata.

## 9. Rollback della strumentazione

Le sole modifiche sorgente di questa fase sono quelle diagnostiche in PlanetMapperCamera.cpp/.h. Per il rollback, rimuovere i contatori, i campioni, SnapshotPlanetMapperScene e il riepilogo nel distruttore; ripristinare il distruttore default. Non revertire file o modifiche preesistenti di altre fasi. I log e il report resteranno come evidenze sandbox/progetto.

**Nessun commit o push. Nessun freeze D1.**

