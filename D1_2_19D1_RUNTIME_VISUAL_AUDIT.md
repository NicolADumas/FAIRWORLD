# FAIRWORLD — D1.2.19d.1 Runtime Visual Failure Audit

## Esito

**AUDIT STATICO COMPLETATO — diagnosi parziale, nessuna patch applicata.** Nessuna GUI è stata avviata. Il log persistente più vicino ai sintomi documenta un avvio diverso da quello riferito nel nuovo test manuale; i dati di quella sessione non sono quindi intercambiabili.

### Classificazione sintetica

| Sintomo | Stato | Conclusione supportata |
|---|---|---|
| `Grounded: YES` con `solid voxel contacts: 0` | **CAUSA DEL CONTATORE CONFERMATA** | Nel percorso sferico il contatore non è aggiornato. `Grounded` può essere impostato dal rifiuto di uno spostamento radiale; il guard per `OutOfBounds` può fungere da superficie di arresto senza contatto con voxel solido. |
| Mesh con VRAM valida respinta dal frustum | **RIFIUTO CONFERMATO, CAUSA GEOMETRICA NON DETERMINATA** | Un log persistente mostra bounds e rifiuto prima della draw, ma non registra camera basis, distanze dai piani o proiezione dei vertici. |
| Camera inizialmente tangenziale | **COMPORTAMENTO CONFERMATO DAL CODICE** | La direzione iniziale è costruita nel piano tangente alla normale radiale. È l’heading iniziale implementato, non prova di un errore di trasformazione. |
| Framebuffer con `renderPass` nullo | **CALLSITE DELL’APP ESCLUSO PER I FRAMEBUFFER TRACCIATI; PERCORSO ESTERNO PROBABILE** | I framebuffer di `RenderManager` sono creati solo dopo un RenderPass valido. Nei log storici gli errori avvengono durante `VulkanCore::InitSwapchain` e lo stack attraversa hook Twitch. Il log della nuova sessione non è disponibile per correlare l’evento. |
| Sei semafori residui | **NON IDENTIFICABILI CON LE EVIDENZE DISPONIBILI** | Il tracker applicativo copre le creazioni che passano dai suoi wrapper, non ogni chiamata Vulkan nel loader/backend. Le sei handle della sessione riferita non sono nel repository. |
| Lettura mappa e scrittura in `saves/world` | **RISCHIO DI PERCORSI RELATIVI CONFERMATO** | `main` e `PlayState` usano percorsi relativi; il teardown chiama `ClearWorld(true)`. La sicurezza dipende dalla working directory effettiva e dall’assenza di un percorso assoluto esterno. |

## Evidenze e limiti della sessione

Il repository contiene sorgenti e log R3 precedenti, ma non un nuovo log della sessione manuale che ha prodotto i sei semafori e i nove chunk. Le osservazioni del brief sono riportate come **evidenza riferita dall’utente**, non come misure indipendentemente rilette. Nessuna traccia di runtime è stata prodotta in questo audit.

Il log persistente [Editor_R3Final_PersistenceReopen.log](D:/FAIRWORLD/FAIRWORLD/R3_validation_logs/Editor_R3Final_PersistenceReopen.log) documenta un caso precedente differente:

- `WorldProjectManager` riporta il file mappa non trovato/non valido e crea `Fairworld Prime`; il log registra `Caricamento da JSON: saves/map/world_map.json`, non il caricamento FWB riferito ora.
- Il valore `planetSize: 4` corrisponde a `Large` nell’enum corrente, non a Tiny.
- Il diagnostic mesh registra **20.628 vertici**, face 4, col 9, row 9, handle VRAM valido, AABB locale/world e `FRUSTUM RESULT = REJECT`.
- La mesh diagnostic non raggiunge la draw (`reason = Frustum rejected`).
- Il teardown registra `Salvati 21 chunk su disco (saves/world)`, non nove.
- A distanza radiale `192.9`, la fisica registra ripetutamente `Grounded: YES`, zero solid contacts e lookup debug `(0,0,0) EMPTY`.

Questi dati dimostrano che sintomi analoghi erano già presenti in un’esecuzione precedente, ma non identificano la mappa o il numero di chunk della nuova sessione. Il numero 7.614 vertici, i nove chunk e il caricamento di `world_map.fwb` restano **non corroborati da log persistenti reperiti**.

## 1. Mappa effettiva e percorsi di persistenza

### Percorso nel codice

1. `src/core/app/main.cpp:323` chiama `projectManager.LoadProject("saves/map/world_map.json", ...)` durante l’avvio.
2. In modalità gioco `src/core/app/main.cpp:344` imposta `context.targetGameJsonPath` allo stesso percorso relativo.
3. `PlayState::Init` copia il valore in `configPath` (`src/states/PlayState.cpp:100`) e carica il documento tramite `WorldProjectManager` (`:147`, `:266`).
4. `WorldProjectManager::LoadProject` delega a `MapDocument::LoadSmart` (`src/world/WorldProjectManager.cpp:89–95`). `LoadSmart` preferisce il `.fwb` se esiste senza JSON o ha timestamp non più vecchio; altrimenti carica il JSON e rigenera il binario (`src/world/MapDocument.cpp:1080–1105`).
5. PlayState imposta la directory chunk a `saves/world` (`src/states/PlayState.cpp:253`). Il distruttore chiama `ClearWorld(true)` (`:45`), che salva tutti i chunk generati (`src/world/GameWorld.cpp:258–260`; `src/world/WorldChunkManager.cpp:77–85`).

### Conclusione

Il codice **non impone un root sandbox** né canonicalizza i percorsi contro una root approvata. La mappa e i chunk sono relativi alla current working directory; un avvio con working directory del repository può leggere/scrivere `D:\FAIRWORLD\FAIRWORLD\saves`, mentre la medesima build lanciata da una directory sandbox con copie indipendenti usa quella directory relativa. Un `targetGameJsonPath` assoluto fornito da altro chiamante potrebbe inoltre uscire dal sandbox. La sola esistenza di una cartella chiamata `runtime` non dimostra l’isolamento.

**Rischio confermato; destinazione concreta della nuova sessione non verificata.** Prima di una futura GUI servono comando di avvio con working directory registrata, path assoluti canonici risolti e verifica che nessun path, junction/symlink/hardlink raggiunga gli originali. Non eseguire la sessione finché questi dati non sono disponibili.

## 2. Grounded, contatti e voxel solidi

### Flusso confermato

- `PhysicsEngine::ResolveCollisions` azzera `dbg_voxelContacts` (`src/systems/PhysicsEngine.cpp:152`).
- Quando il mondo è sferico, chiama `ResolveSphericalCollisions(...)` e ritorna subito (`:170–173`). Il contatore e i campi `dbg_lastLookup*` del percorso cartesiano non sono aggiornati dai campioni radiali sferici.
- `Systems.cpp` stampa gli stessi campi con etichetta generica `Number of solid voxel contacts` (`src/systems/Systems.cpp:279–282`). In modalità sferica lo zero è quindi **un contatore non strumentato**, non prova di assenza di campionamenti o contatti.
- La collisione sferica campiona il cilindro del player tramite `world.GetBlockCartesian(samplePos)`. Un blocco solido blocca la posizione. Per `OutOfBounds`, il codice usa un guard radiale: ritorna bloccato quando `distance - PlanetRadius < 25` (`PhysicsEngine.cpp`, `ResolveSphericalCollisions`, lambda `isCylinderBlocked`).
- Se uno step radiale verso il basso è bloccato, `dy < 0` basta a impostare `isGrounded = true` (`:830–837`), senza registrare quale campione o quale tipo di blocco l’abbia bloccato.

Nel caso storico, l’indice dimensione 4 è `Large`: `N=21`, raggio `21 * 16 / 2 = 168`; a distanza `192.9`, `distance - radius = 24.9`, dentro il margine `OutOfBounds` di 25. È una spiegazione numerica coerente col `Grounded: YES`, ma il log non conserva il valore del campione `BlockType`; quindi **non dimostra da solo** che il singolo tick storico si sia fermato proprio su quel ramo. La soglia e l’impostazione permissiva di `Grounded` sono comunque confermate dalla sorgente.

### Diagnosi

**Difetto diagnostico confermato:** il contatore presentato come “contatti voxel solidi” è sempre azzerato per il ramo sferico e non conta le collisioni radiali. Anche `Last voxel lookup ... EMPTY` è un campo non aggiornato dai campioni sferici e non rappresenta l’ultima query della collisione planetaria. Non può essere usato per dedurre il tipo del blocco che ha bloccato il movimento.

**Ipotesi funzionale:** `Grounded` potrebbe derivare dal margine di sicurezza `OutOfBounds`, anche senza voxel solido sotto i piedi. È coerente col ramo in codice e con i numeri del caso storico; per la sessione nuova va registrato il risultato reale dei singoli campioni, la loro distanza dal centro e la causa del blocco.

## 3. Mesh: CPU → ECS → VRAM → frustum → draw

Il log storico mostra un handle VRAM valido e 20.628 vertici per la mesh diagnostic; il riepilogo nella stessa esecuzione mostra generation, DMA completato, deferred queue e commit ECS. Nella sequenza aggregata, i contatori sono rispettivamente 5, 4, 4 e 3; tre MeshComponent risultano valide/in compartimenti, una mesh target viene respinta dal frustum e zero draw raggiungono il punto diagnostico. Queste fasi non provano che la mesh specifica con handle 2 sia stata disegnata.

### Bounds e culling

- `MeshComponent::bounds()` calcola l’AABB dai `vertex.position` della mesh (`src/components/ForgeComponents.h:98–117`).
- `RenderFairworld` calcola `model` da `TransformComponent::computeGlobalMatrix`, trasforma il centro e gli extents dell’AABB, quindi testa `frustum.containsAABB` (`src/render/RenderManager.cpp:1254–1276`). Se respinta, salta la registrazione draw.
- La diagnostica logga una translation locale della trasformazione, non l’intera matrice globale, la camera, i sei piani, i signed distances o i vertici clip-space. L’AABB del log storico è locale e world identica (min `(-26.6,101.4,-26.6)`, max `(-5.2,186.0,-5.2)`), ma questa coincidenza non valida la matrice per tutti i chunk o chiarisce la direzione della camera.
- Il frustum di culling è estratto da una `perspective(m_fov, ..., 0.1, 2000)` e `viewMatrix` (`RenderManager.cpp:1150–1156`); la proiezione effettivamente passata al renderer proviene dalla camera attiva. La differenza di far plane è più permissiva nel culling; non spiega da sola un falso reject.

La causa del rifiuto non è dimostrabile con l’evidenza attuale. Possibili cause ancora aperte: camera rivolta altrove (coerente con l’heading tangent iniziale), trasformazione gerarchica/bounds che non descrive il buffer renderizzato, o errore nell’estrazione/test dei piani. Non disabilitare il culling. La verifica minima deve registrare per **la stessa entity e lo stesso frame**: camera world position/front/up, view, projection attiva e culling, global model matrix, local/world AABB, distanza signed da ciascun piano, e test dei punti/vertici del buffer nello spazio clip. Un test CPU isolato deve confrontare l’AABB con il volume generato dalla stessa matrice GPU.

## 4. Orientamento camera

PlayState inizializza `initialUp` dalla posizione di spawn radiale e chiama `PlanetaryCameraFrame::MakeInitial` (`src/states/PlayState.cpp:196–216`). `MakeInitial` sceglie un riferimento globale o `+X` vicino ai poli e costruisce `forward = normalize(cross(up, reference))` (`src/systems/PlanetaryCameraFrame.h:40–51`). Per definizione `forward · up = 0`: l’heading yaw-zero iniziale è tangenziale alla superficie. La camera è poi costruita con `lookAt` dalla rotazione interpolata (`PlayState.cpp:588–618`).

**Fatto confermato:** lo sguardo iniziale è tangenziale; il codice non mira automaticamente verso il terreno né verso il centro planetario. **Non dimostrato:** che questa scelta causi il rifiuto frustum o sia indesiderata per il gameplay. La proposta minima è acquisire front/up e un’immagine/telemetria della direzione del chunk prima di modificare l’orientamento.

## 5. Framebuffer con renderPass nullo

### Callsite dell’applicazione

- Startup in `RenderManager::Init`: `VulkanCore::InitSwapchain` viene prima di `CreateRenderPass`; il RenderPass viene creato prima del depth resource e di `CreateFramebuffers` (`src/render/RenderManager.cpp:110–154`).
- `CreateFramebuffers` rifiuta esplicitamente `m_renderPass == VK_NULL_HANDLE` (`:325–360`).
- In resize, la swapchain viene ricreata, il RenderPass è ricreato se invalido/cambiato, quindi depth e framebuffer (`:3690–3766`).
- `InitImGui` riceve `m_renderPass` dopo la creazione dei framebuffer applicativi (`:2614–2630`); nel codice trovato non sono abilitate le viewport ImGui multi-window.

Quindi i framebuffer creati dai callsite applicativi intercettati in `RenderManager::CreateFramebuffers` non sono la spiegazione degli errori null: il gate e l’ordine impediscono quel percorso.

### Evidenza Vulkan storica

Nel log [Editor_TwitchEnabled.log](D:/FAIRWORLD/FAIRWORLD/R3_validation_logs/Editor_TwitchEnabled.log), le prime segnalazioni `UNASSIGNED-GeneralParameterError-RequiredHandle` e `VUID-VkFramebufferCreateInfo-renderPass-parameter` sono in Startup; lo stack entra in `fw::VulkanCore::InitSwapchain` e attraversa `Twitch_GetDeviceProcAddr` / `TwitchMessageProc`. Altre tre coppie di messaggi compaiono in Resize. Sono **sei tentativi/callsite osservati come coppie di due messaggi**, dodici messaggi validation complessivi; il RenderManager applicativo registra invece i propri framebuffer con renderPass non nullo prima e dopo resize. Nel run Twitch-disabled storico il log non mostra questi errori. Questo è forte indizio di una chiamata nel percorso di overlay/hook, non prova conclusiva che Twitch possieda tutti i framebuffer né che il nuovo run abbia la stessa configurazione.

Per il nuovo report manuale non è presente un log con gli stack dei sei errori riferiti. La correzione minima proposta è acquisire, in una sessione autorizzata, messaggio completo, stack simbolizzato, handle device/renderpass e fase per ciascuna chiamata, mantenendo i layer abilitati. Non ignorare o sopprimere gli errori.

## 6. Semafori residui

Nel log storico Twitch-enabled, il tracker applicativo registra cinque semafori propri, tutti distrutti (`5 created / 5 destroyed / 0 live`), mentre `vkDestroyDevice` segnala quattro handle ulteriori: `0x1a000000001a`, `0x1b000000001b`, `0xce00000000ce`, `0xcf00000000cf`. La stessa traccia non contiene le creazioni di quegli handle. Le stack di creazione non possono essere ricostruite dai soli handle del messaggio di distruzione.

Il tracker corrente (`src/render/vulkan/VulkanForensicAudit.cpp:174–237`) registra solo `InterceptCreateSemaphore` / `InterceptDestroySemaphore`; i callsite applicativi trovati usano questi wrapper per la timeline DMA e i sync object del RenderManager. Non intercetta automaticamente chiamate effettuate direttamente da loader, layer, backend ImGui o altre librerie. Perciò `0 live` nel tracker non equivale a “nessun semaphore child vivo sul device”. La nuova segnalazione di **sei** residui non include qui handle, object name o create trace: ownership e motivi di mancata distruzione restano **NOT VERIFIED**. I quattro handle storici non vanno riattribuiti ai sei nuovi senza dati.

**Correzione diagnostica minima proposta:** tracciare al confine Vulkan/loader ogni `vkCreateSemaphore` riuscita e `vkDestroySemaphore`, con device, nome debug object, layer/caller e stack; al teardown fare diff con le handle riportate dal Validation Layer. Prima della patch verificare se l’ambiente offre Vulkan API dump o strumentazione layer sufficiente. Non falsare i contatori applicativi.

## 7. Matrice delle cause

| Sintomo | Stato | Evidenza | Causa confermata / residua |
|---|---|---|---|
| Zero “solid contacts” | **CONFIRMED** | Azzeramento seguito da early return sferico; nessun incremento nel resolver sferico | Contatore non rappresenta collisioni planetarie. |
| Grounded sì senza voxel contact | **CAUSE DI SEMANTICA CONFERMATA; SINGOLO CAMPIONE NON VERIFICATO** | `Grounded` impostato quando inward step è bloccato; OOB guard definito a raggio + 25; log storico a `R=168`, `d=192.9` | Può segnalare il margine OOB, non un contatto voxel solido. Manca il block type del campione. |
| VRAM valida ma frustum reject | **REJECT CONFIRMED; ROOT CAUSE NOT VERIFIED** | AABB, allocation, coordinate mesh e `FRUSTUM RESULT = REJECT`; draw non raggiunta | Camera/bounds/piani per lo stesso frame mancanti. |
| Orientamento iniziale tangenziale | **CONFIRMED** | `MakeInitial` proietta heading nel piano tangente | Comportamento implementato; difetto funzionale da confermare rispetto al requisito di vista. |
| Framebuffer RP null | **APP CALLSITE EXCLUDED FOR TRACKED PATH; EXTERNAL HOOK STRONGLY SUSPECTED** | Guard e ordine RenderManager; stack storico Twitch → `VulkanCore::InitSwapchain` | Nuova sessione senza stack completo; ownership finale non assegnata. |
| Sei semafori residui | **NOT VERIFIED** | Nessun log/handle di nuova sessione; tracker copre wrapper soltanto | Serve traccia creazione/distruzione end-to-end. |
| Mappa e nove chunk | **USER-REPORTED; NOT CORROBORATED** | Nuovo log assente; precedente log registra fallback/default e 21 salvataggi | Working directory e path canonici della sessione non documentati. |

## 8. Correzioni minime proposte, in ordine

1. **Prima la sicurezza dati:** usare una working directory sandbox con copie complete di `saves/map`, `saves/world`, asset/config necessari; risolvere e riportare i path assoluti prima dell’avvio. Rifiutare path al di fuori della root sandbox per la sessione di test. Non eseguire ancora la GUI.
2. **Telemetria fisica senza cambiare il comportamento:** nel ramo sferico contare campioni e contatti solidi distinti; registrare per il blocco l’esito (`Solid`, `OutOfBounds guard`, altro), posizione del campione, distanza/raggio e direzione di step. Rendere esplicito che `dbg_lastLookup*` è riferito al resolver usato. Non modificare gravità/collisione fino a questa misura.
3. **Frustum:** acquisire i valori mancanti per la stessa mesh/frame e riprodurre il test AABB CPU. Correggere solo il primo errore dimostrato tra bounds, matrice o estrazione piani; mantenere culling attivo.
4. **Framebuffer:** ottenere stack e create info del nuovo run. Se l’evento resta nella catena Twitch/hook, fare A/B controllato con layer attivo/disattivo mantenendo Khronos validation attiva; correggere/configurare il componente identificato solo dopo attribuzione supportata.
5. **Semaphore ownership:** associare i sei handle a create/destroy e object names all’intero device. Poi correggere il proprietario individuato e ripetere il confronto; non intervenire su contatori come workaround.
6. **Camera:** documentare il vettore `cameraFront` iniziale rispetto a `localUp`, target visivo e AABB mesh; cambiare yaw/target iniziale soltanto se il comportamento atteso richiede una visuale iniziale diversa e la telemetria conferma la discrepanza.

## 9. Prossima prova operativa richiesta

L’audit statico non autorizza né richiede un nuovo avvio. Per la successiva sessione esplicitamente autorizzata servono: file di log completo con timestamp e working directory, hash fixture mappa/FBW prima-dopo, numero e coordinate dei chunk, handle e nomi dei sei semafori con trace create/destroy, trace framebuffer per fase, telemetria camera/AABB/frustum, e campioni di collisione sferica. Fino a quel momento:

- Cause confermate: contatore collisioni non applicabile al ramo sferico; `Grounded` non è prova di contatto con voxel; heading iniziale tangenziale; rischio di accesso ai dati determinato dalla working directory relativa.
- Ipotesi forti ma non conclusive per il nuovo run: framebuffer creati dal percorso overlay/hook; OOB guard responsabile del grounded osservato.
- Bloccanti non risolti: causa del frustum reject specifico, ownership dei sei semafori, path effettivi e identità della mappa nella nuova sessione.

**Nessun codice è stato cambiato. Nessuna GUI/processo è stato avviato. Nessun commit, push, reset o rebaseline è stato eseguito. P0.3C3/D1 non viene dichiarato FROZEN.**
