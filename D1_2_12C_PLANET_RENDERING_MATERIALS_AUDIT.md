# D1.2.12c — Planet Rendering & Materials Failure Audit

**Esito:** audit statico e riproduzione GUI parzialmente completati. Non viene dichiarato D1 FROZEN.

**Ambiente:** eseguibile `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe`, working directory `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`, argomento `--disable-twitch-layer`. La configurazione log riporta `Validation Layers: ATTIVI`.

## Evidenze operative

| Sessione / schermata | Evidenza osservata | Stato |
|---|---|---|
| Planet Mapper, +Z | Globo grigio quasi uniforme; overlay indica faccia NORD (+Z), latitudine 20°, longitudine −45°, griglia 7×7 (Medium). | Riprodotto; screenshot CUA mostrato nella conversazione, non esportato come PNG locale. |
| Planet Mapper, −X | Dopo rotazione nella prima sessione, overlay indicava OVEST (−X); globo ancora grigio. | Riprodotto nella prima sessione; acquisizione CUA inline. |
| Planet Mapper, +Y | Dopo rotazione nella prima sessione, overlay indicava POLO NORD (+Y); globo grigio con piccola caratteristica triangolare verde. | Riprodotto nella prima sessione; acquisizione CUA inline. |
| Planet Mapper, −Z, +X, −Y | Tentati drag nella seconda sessione, ma l'overlay non cambiava; la sessione è stata chiusa normalmente. | NOT VERIFIED a runtime. Le convenzioni statiche sono inventariate sotto. |
| Chunk Editor | Preview con ampie zone grigie, porzioni marrone/verdi e un lato molto puntinato/bianco. | Riprodotto; la schermata non identifica da sola quale materiale o livello LOD causi il puntinato. |
| Block Maker | ID 1 Grass appare verde; ID 3 Stone appare grigio con faccia superiore chiara; ID 18 carburo di titanio appare grigio. | Riprodotto. Il grigio di Stone coincide con il fallback definito; ID 18 ha una texture grayscale presente, ma la schermata non verifica il corretto mapping UV. |
| PlayState | Terreno grigio/bianco molto puntinato e area scura; renderer registra 24 mesh LOD e 0 mesh voxel. Il secondo spawn cambia vista. | Riprodotto; la schermata osserva il percorso LOD, non blocchi voxel. |

Le schermate sono state acquisite nell'interfaccia CUA e restano visibili come immagini inline nella conversazione; non è disponibile un file PNG esportato da allegare a questo rapporto. Questa limitazione è esplicita: non sono presentate come screenshot locali persistenti.

### Log e chiusura

- Sessione Planet Mapper principale: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_12c_planetmapper.stdout.log`; chiusura normale, exit code **0** (session id 89449).
- Sessione supplementare per le facce: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_12c_planetmapper_faces.stdout.log`; chiusura normale, exit code **0** (session id 14252).
- Secondo log: 26,536 byte; validation layer attivo; **0 validation errors**, **6 validation warnings**, tutti `vkCreateGraphicsPipelines(): ... vertex attribute at location 7 not consumed by vertex shader`; **0** framebuffer creati con render pass nullo.
- Gate ownership nel log: **6 semafori creati, 6 distrutti, 0 live**, `VULKAN OWNERSHIP: CLEAN (PASS)`. (Il conteggio testuale grezzo può contare righe di riepilogo; il conteggio del tracker finale è quello riportato.)
- La prima sessione registra il messaggio Vulkan di layer attivo, debug messenger/ownership e lo stesso warning location 7; PlayState registra `Active independent LOD mesh count: 24`, `Active voxel mesh count: 0`.
- Teletrasporto registrato: `previous=(0.0,0.0,66.0)`, `face=2`, `local=(28.0,-14.0)`, `chart=(0.5,-0.2)`, `target=(66.3,-16.6,-33.2)`, `next=(66.3,-16.6,-33.2)`. Il target e la posizione successiva coincidono alla precisione stampata.

Verifica integrità successiva alle sessioni: `Verify-OriginalInputs.ps1` → `ORIGINAL_INPUT_HASHES_MATCH=40`. Nel sandbox risultano 39 file sotto `runtime\saves`, di cui 37 chunk in `runtime\saves\world`; le scritture sono nell'ambiente di prova. Nessun salvataggio originale è risultato modificato.

## B — Audit geometria planetaria

Percorso esaminato: `PlanetMath` → `CubeSphereMapping` → `PlanetMapperState::RebuildPlanetRoots()` → `SphericalLODSystem` → `Mesh` → pipeline Forge/Vulkan.

### Fatti verificati

- `src/states/PlanetMapperState.cpp:76-111` ricostruisce i sei patch root. I quattro corner per faccia sono ordinati in modo che la triangolazione usata dal generatore abbia winding geometrico verso l'esterno.
- `src/world/SphericalLOD.cpp:203-226` genera una griglia 17×17 (16 divisioni), interpola i corner, normalizza la direzione radiale e costruisce la superficie con raggio/noise.
- `src/world/SphericalLOD.cpp:449-450` emette i triangoli `i00,i10,i01` e `i10,i11,i01`, coerenti con il winding del patch verificato per le sei radici. `RenderManager` configura il culling Forge come `VK_CULL_MODE_NONE`; non emerge quindi un caso di facce sparite per back-face culling.
- Il percorso LOD contiene una proiezione faccia/UV/griglia duplicata in `SphericalLOD.cpp` (circa righe 233-249); la correttezza della continuità geometrica ai bordi tra facce e tra livelli LOD non è dimostrata dalla schermata né da questo controllo statico.
- Le tre schermate Planet Mapper/PlayState mostrano superfici continue senza una crepa o un triangolo mancante identificabile con certezza. Questo non dimostra l'assenza di T-junction o discontinuità sub-pixel. Nessun difetto di geometria è confermato in questo audit.

### Matrice orientamento statica

| Faccia | Patch root (segno dominante) | Osservazione runtime |
|---|---|---|
| +Z | Z positivo | NORD (+Z), campione visto |
| −Z | Z negativo | NOT VERIFIED |
| +X | X positivo | NOT VERIFIED nella sessione supplementare; il campione +X della sessione PlayState è uno spawn, non prova la camera Planet Mapper |
| −X | X negativo | OVEST (−X), campione visto |
| +Y | Y positivo | POLO NORD (+Y), campione visto |
| −Y | Y negativo | NOT VERIFIED |

La matrice è riepilogo dell'orientamento statico, non certificazione visiva runtime delle sei facce.

## C — Audit materiali e texture

Percorso verificato: `MaterialRegistry::LoadFromJson` → ID materiale diretto → dati `Vertex`/mesh → attributi vertex Vulkan → shader Forge → texture array e descriptor.

### Causa confermata 1 — Cache PBR non coerente con la definizione materiale

`src/render/TexturePacker.cpp:47-59` accetta `assets/texture_cache.bin` quando il file cache esiste e la data di `materials.json` non è successiva; il controllo di validità non confronta hash/contenuto delle definizioni né timestamp/contenuto di ogni texture referenziata. La cache viene caricata se layer count e risoluzione coincidono.

Nel sandbox il cache header indica 256 layer a 512×512. `materials.json` definisce per materiale ID 12 un fallback blu (`baseColorFallback` circa `(0, 0.02477, 1)`) e un albedo path `textures/blocks/fairworld_thesims_albedo.png` che non esiste. La scansione di tutti i pixel della layer 12 di `texture_cache.bin` ha trovato solo bianco. La build GUI ha dichiarato cache trovata e caricata. Perciò la layer effettivamente usata non corrisponde alla definizione/fallback corrente: è un'incoerenza della cache verificata, anche se non è stato dimostrato che ID 12 fosse il materiale della specifica zona bianca in schermata.

Altri albedo path configurati ma assenti nel sandbox: ID 6, 14, 16 e 17. Per ID 14/16/17 è dichiarato fallback grigio. Gli ID senza path albedo usano il fallback configurato. ID 18 ha un PNG presente (`fairworld_carburotitanio_albedo.png`), che è una texture grayscale con isole UV nere; il corretto campionamento in tutte le mesh non è stato provato. `fairworld_Grass_albedo.png` esiste ma non è assegnata all'ID Grass nel JSON: Grass verde deriva dal fallback e non dimostra un errore.

### Causa confermata 2 — Semantica CPU/GPU dell'attributo location 2 divergente nel percorso LOD

`src/world/SphericalLOD.cpp:441` scrive `roughness/metallic` nel campo `Vertex::roughMetal`; `RenderManager.cpp` associa location 2 a `offsetof(Vertex, roughMetal)` (circa righe 577-582). In `src/render/forge.vert:15,45-61`, lo stesso location 2 è interpretato come coordinate UV e passato al fragment shader; `forge.frag:123-145` usa tali coordinate per campionare albedo, normal e ORM. Il codice commenta esplicitamente l'uso di `inRoughMetal` come UV. Questo è un mismatch semantico verificato tra ciò che produce il generatore LOD e ciò che il vertex shader consuma. Spiega in modo plausibile il campionamento non corretto delle texture LOD; non prova da solo che sia la causa precisa delle macchie bianche osservate.

Il fragment shader costruisce il TBN dalle derivate UV in `forge.frag:89-110`, poi applica la normal map in riga ~145. UV costanti o degeneri possono rendere la base tangente mal condizionata; la derivata/normalizzazione su UV degeneri è un rischio concreto, ma l'origine esatta del puntinato richiede un capture/debug view dei valori al fragment e delle texture campionate.

### Fallback e percorsi divergenti

- `TexturePacker::LoadAndAppendTexture` usa il colore fallback quando il percorso manca; il runtime può quindi renderizzare senza errore Vulkan ma con colore uniforme o dati cache non aggiornati.
- `forge.frag:123-137` campiona direttamente il layer identificato da `materialID` (con clamp al massimo layer), quindi ID errato o layer cache incoerente può produrre contenuto visivamente sbagliato senza invalidare i descriptor.
- `SphericalLOD.cpp:275` assegna ID materiale 0 alle celle senza painting; layer 0 bianco e illuminazione PBR rendono plausibile il globo neutro grigio. Non è dimostrato che il grigio uniforme del Planet Mapper sia un bug: per il blank preview può essere il comportamento intenzionale.
- `ChunkEditorRenderer`, `PlanetMapperRenderer` e `PlayRenderer` usano la pipeline Forge e i descriptor set PBR condivisi inizializzati da `RenderManager`; differiscono però per il tipo di mesh e i dati vertex. Il PlayState osservato conteneva LOD e zero voxel mesh, quindi non è stato confrontato un blocco voxel renderizzato nel PlayState.
- Block Maker mostra fallback/preview del catalogo; non è un confronto equivalente alla superficie LOD o alla mesh voxel di gioco.
- I sei warning location 7 provengono dalla creazione pipeline e segnalano un attributo dichiarato nel vertex input non consumato dallo shader selezionato; sono warning, non errori Vulkan, e non provano un difetto di texture.

## D — Tabella diagnosi e priorità

| Priorità | Sintomo / percorso | Stato della causa | Gravità | Correzione minima proposta (non applicata) | Test di regressione |
|---|---|---|---|---|---|
| P0 | Layer PBR stale: cache accettata senza fingerprint completo; ID12 è bianco benché la definizione richieda fallback blu e l'albedo manchi. | Confermata incoerenza dati/cache; collegamento alla zona GUI non identificato. | Alta: materiali visualmente errati e fallback non affidabile. | Invalidation key della cache su definizioni e asset referenziati (hash/versione); rigenerare solo cache sandbox e confrontare byte/colori attesi. | Cambiare JSON e PNG mantenendo timestamp controllati deve invalidare; verificare pixel noti di ogni layer e fallback missing-file. |
| P0 | Mesh LOD produce roughness/metallic in location 2 mentre Forge interpreta location 2 come UV. | Mismatch producer/consumer confermato; causa esatta delle macchie non isolata. | Alta per albedo/normal/ORM LOD. | Allineare il layout del solo producer LOD al contratto vertex condiviso (UV in location 2; rough/metal nel campo/layout previsto), senza cambiare mapping planetario. | Render/capture di LOD con UV noti e materiali texturizzati e senza texture; confrontare mesh voxel e LOD; verificare valori interpolati in shader. |
| P1 | Possibile normal TBN instabile su UV degeneri. | Rischio supportato dall'implementazione; sintomo causale non provato. | Media/alta, dipende dalla copertura normal map. | Protezione numerica locale nel calcolo TBN, solo dopo test di input UV degeneri. | Shader test/debug output finito per UV costanti, collineari e texture flat. |
| P1 | Preview pianeta grigio uniforme. | Possibile ID0 non dipinto + albedo bianco + luce; non confermato come bug. | Bassa se preview neutra intenzionale; altrimenti alta UX. | Chiarire il materiale previsto per superficie non dipinta; non cambiare fallback finché il contratto non è deciso. | Screenshot baseline blank planet e planet con painting per sei facce. |
| P2 | Texture configurate mancanti; texture Grass presente ma non referenziata. | Presenza/assenza verificata; intenzionalità delle assegnazioni non nota. | Media, per materiali interessati. | Verificare packaging/catalogo prima di riassegnare o creare asset. | Audit manifest asset e test apertura da working directory sandbox/installata. |
| P2 | Possibili seam LOD/cross-face. | Non dimostrati né esclusi. | Da determinare. | Solo dopo capture geometrico ai bordi, correggere la giunzione strettamente interessata. | Wireframe e misure di posizione/normale ai bordi tra facce e livelli LOD. |

## E — Ordine di correzione e test di accettazione

1. **Cache correctness:** definire una chiave cache derivata da `materials.json` e file effettivamente usati; rigenerare la cache nel sandbox; controllare layer 0–255 e fallback attesi. Non riutilizzare o sovrascrivere la cache/asset originale in fase di audit.
2. **Vertex contract LOD:** documentare il layout CPU condiviso e correggere solo l'assegnazione LOD che popola location 2 con roughness/metallic, più l'eventuale campo rough/metal dedicato se previsto; testare UV visibili e sampling.
3. **Normal mapping:** testare input UV degeneri; introdurre un fallback TBN solo se il test dimostra valori non finiti o instabilità.
4. **Asset resolution:** rendere espliciti gli albedo assenti e il loro fallback; verificare il percorso relativo alla working directory e il packaging. Non associare automaticamente la texture Grass orfana.
5. **Validazione differenziale:** stesso fixture/material ID reso in Chunk Editor, Planet Mapper e PlayState, separando mesh voxel da mesh LOD. Salvare screenshot prima/dopo, acquisire texture layer e UV/material ID usati, controllare sei direzioni e giunzioni LOD.

Gate finale per una correzione: rendering corretto per texture presenti, fallback intenzionali e file mancanti; UV e layer verificati; nessun triangolo o seam mancante alle sei facce/LOD; screenshot comparabili; Validation Layer con 0 errori e Ownership Gate con 0 semafori live.

## Limiti e decisione

- Le cause cache ID12 e semantica location 2 sono provate dai contenuti/cache e dal percorso CPU→input Vulkan→shader; la relazione con ogni pixel anomalo delle schermate non è ancora provata senza capture GPU o debug overlay.
- Geometria: winding root coerente e nessun difetto visivo concreto isolato; continuità ai bordi LOD resta aperta.
- Sei orientamenti: solo +Z, −X, +Y osservati esplicitamente a runtime Planet Mapper; gli altri tre sono NOT VERIFIED.
- Blocchi: il controllo Block Maker copre preview fallback; il PlayState osservato non aveva mesh voxel. Rendering di un blocco voxel in runtime resta NOT VERIFIED.
- Nessun sorgente, shader, materiale o salvataggio originale è stato modificato durante questo audit. Il rapporto stesso è il solo nuovo artifact nel repository prodotto da questa fase.
- Non viene proposta né applicata alcuna patch in questa fase.
