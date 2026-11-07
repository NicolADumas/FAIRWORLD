# D1.2.12 — Rapporto di verifica funzionale isolata

**Data:** 2026-11-04  
**Esito complessivo:** **INCOMPLETE**  
**Ambiente:** `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`  
**Eseguibile:** `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe`  
**Stato D1:** non dichiarato FROZEN.

## 1. Esecuzione e preflight

Le sessioni sono state avviate dalla working directory richiesta con:

```powershell
cmd.exe /c '"D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe" --disable-twitch-layer < inputs\planetmapper.stdin > logs\D1_2_12_run1.stdout.log 2>&1'
cmd.exe /c '"D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe" --disable-twitch-layer < inputs\planetmapper.stdin > logs\D1_2_12_run2.stdout.log 2>&1'
cmd.exe /c '"D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe" --disable-twitch-layer < inputs\playstate.stdin > logs\D1_2_12_playstate.stdout.log 2>&1'
```

È stata eseguita anche una sessione PlayState di controllo che ha scritto in `logs\D1_2_12_playstate_exitcode.stdout.log`. La chiusura è stata effettuata con il pulsante X della finestra, seguita dallo shutdown registrato dal runtime.

| Preflight | Esito | Evidenza |
|---|---|---|
| Hash dei file originali prima dell'avvio | PASS | Manifest di 40 file |
| Hash degli originali dopo le sessioni | PASS | `Verify-OriginalInputs.ps1`: `ORIGINAL_INPUT_HASHES_MATCH=40` |
| Junction/symlink nel sandbox | PASS | Nessun reparse point trovato; controllo hard-link preliminare: 15 file save, un link per file |
| Shader SPIR-V | PASS | 11 file `.spv` presenti in `runtime\bin` |
| Diagnostica D1.2.11 nel binario | PASS | Stringhe `[D1.2.11][RAYCAST]` e `[D1.2.11][TELEPORT]` presenti nell'eseguibile |
| Vulkan Validation Layer | PASS | Log: `Validation Layers: ATTIVI`; avvio con `--disable-twitch-layer` |

Il report di hash dopo l'esecuzione conferma che tutti i 40 originali inclusi nel manifest sono invariati. Non è stato eseguito alcun ripristino.

## 2. Fixture

Fixture dedicato: `runtime\fixtures\D1_2_12\world_map.json`, creato dal documento sandbox esistente usando il template `Terreno Standard` (`default_terrain`).

- `planetSize = 2` e template `planetSize = 2`; ordinale modello corrispondente a Small, risoluzione N=7.
- Due spawn di prova: `D1.2.12 Spawn +Z` (faceIndex 0) e `D1.2.12 Spawn +X` (faceIndex 2).
- Fixture JSON SHA-256 prima/dopo: `3B6F824C859AEA23BCFCC0847911454F108AAA4E790FA24A9896DADBE8EB27B3`.
- Fixture JSON in `runtime\saves\map\world_map.json` dopo le sessioni: stesso SHA-256.
- Cache binaria sandbox generata: `runtime\saves\map\world_map.fwb`, SHA-256 dopo le sessioni `E0081F607082173856496120BB12B8CBA04258F61E3F3DBF165703ED8E594F3A`.
- Copie del documento sandbox preesistente conservate in `runtime\fixtures\D1_2_12\preserved_existing_map\`; hash registrati nel manifest `pre_run_fixture_manifest.json`.

La prima sessione Planet Mapper ha caricato il JSON (`Caricamento da JSON`) e generato la cache binaria. La seconda sessione Planet Mapper e le sessioni PlayState hanno caricato il FWB (`Caricamento rapido da: saves/map/world_map.fwb`). PlayState ha caricato i due spawn dal documento binario. Questo verifica entrambi i percorsi di caricamento nel runtime. Non è stato eseguito un confronto byte-per-byte tra le strutture decodificate dei due formati.

La GUI ha mostrato il template compatibile e la griglia 7×7. L'etichetta UI mostrava “Medium” per l'ordinale 2; il dato del fixture rimane ordinale 2. Non è stata modificata la nomenclatura.

## 3. Sei facce, raycast e bordi

**Esito per i sei campioni runtime: NOT VERIFIED.** Per nessuna faccia è stato acquisito un campione `RaycastHit` osservabile.

Nei log Planet Mapper il conteggio di `[D1.2.11][RAYCAST]` è zero in entrambe le sessioni. Sono stati tentati click sul globo e sulla zona di preview del terreno, senza produrre un hit valido nel log. Perciò non ci sono valori runtime da riportare per `faceIndex`, `worldPosition`, `faceNormal`, UV, chart, N, colonna/riga o `sizeSource` per +Z, −Z, +X, −X, +Y, −Y. Nessuna faccia è marcata PASS.

| Faccia richiesta | Campione runtime | Esito |
|---|---|---|
| +Z | Nessun log RAYCAST | NOT VERIFIED |
| −Z | Nessun log RAYCAST | NOT VERIFIED |
| +X | Nessun log RAYCAST | NOT VERIFIED |
| −X | Nessun log RAYCAST | NOT VERIFIED |
| +Y | Nessun log RAYCAST | NOT VERIFIED |
| −Y | Nessun log RAYCAST | NOT VERIFIED |

Nel secondo avvio è stata aperta la tabella dei chunk, con sei tab faccia e risoluzione 7×7. È stata cliccata una cella visivamente prossima al bordo della faccia 0; il click ha aggiornato la preview visiva, ma la GUI non ha esposto coordinate numeriche del campione e non è stato registrato un hit raycast. È quindi solo una selezione approssimata della tabella, non una verifica del contratto UV→griglia.

- Centro cella: NOT VERIFIED in GUI.
- Bordo interno: NOT VERIFIED con coordinate numeriche.
- Bordo esterno e angolo: NOT VERIFIED.
- Tie-break esatto / valori `nextafter`: non è una prova GUI; rimane coperto solo dai test matematici D1.2 già eseguiti in precedenza.
- Il documento staged continua a contenere `chunkInstances: []`: non risulta persistita una nuova istanza di terreno. La preview del terreno non equivale a un posizionamento salvato.

## 4. Teletrasporto

PlayState è stato avviato sul documento fixture e ha mostrato i due pulsanti spawn. Il primo spawn è stato caricato all'avvio; il secondo è stato attivato nella GUI. Il log effettivo contiene:

```
[PlayState] Utilizzo Spawn Point custom 'D1.2.12 Spawn +Z' a posizione: 0.00, 0.00, 66.00
[D1.2.11][TELEPORT] previous=(0.0,0.0,66.0) face=2 local=(28.0,-14.0) chart=(0.5,-0.2) heightOffset=20.0 target=(66.3,-16.6,-33.2) next=(66.3,-16.6,-33.2) delta=(66.3,-16.6,-99.2) distance=120.5
```

| Controllo | Esito | Limite |
|---|---|---|
| Spawn iniziale +Z | PASS | Posizione loggata (0, 0, 66.0) |
| Attivazione secondo spawn | PASS | Pulsante +X attivato; log face=2 |
| Posizione precedente, target e successiva | PASS al formato del log | Log arrotondato a un decimale |
| Target uguale alla posizione successiva | PASS al formato del log | Il log mostra gli stessi valori a un decimale; non espone precisione sufficiente a stabilire una tolleranza sub-decimale |
| Delta e distanza | PASS al formato del log | Delta (66.3, −16.6, −99.2), distanza 120.5 |
| Controllo numerico indipendente entro tolleranza | NOT VERIFIED | La diagnostica non stampa precisione completa |

## 5. Vulkan e shutdown

Nei log di tutte le sessioni Planet Mapper e PlayState:

- Errori Vulkan Validation Layer / VUID rilevati dal controllo dei log: **0**.
- Warning Vulkan: **6 per sessione**, relativi all'attributo vertex location 7 non consumato dallo shader; non sono errori di validazione.
- `Runtime shutdown complete YES [PASS]`.
- Semafori: **6 creati, 6 distrutti, 0 live**; 5 appartenenti a RenderManager e 1 a VulkanDmaManager.
- `Vulkan cleanup state COMPLETED [PASS]`.
- Vulkan Ownership Gate: **CLEAN (PASS)**.
- Nessuna menzione di framebuffer con render pass nullo nei log raccolti; il log riporta un render pass creato con handle non nullo. Questo non sostituisce una cattura strumentata di ogni chiamata framebuffer.
- Nessun crash o deadlock osservato durante le chiusure tramite X.

**Exit code del processo: NOT CAPTURED.** Le finestre si sono chiuse e i gate di shutdown sono PASS, ma il wrapper di shell non ha conservato l'exit code numerico. Non lo deduco dai gate.

## 6. Scritture rilevate nel sandbox

Dopo il preflight sono presenti scritture runtime nel solo sandbox:

- Cache mappa `saves\map\world_map.fwb`, generata dal caricamento JSON.
- Log delle sessioni sotto `logs\`.
- File `saves\world\chunk_*.bin` generati da PlayState durante il caricamento/teardown.
- `imgui.ini`.
- Il fixture JSON non è stato modificato dalle sessioni; `chunkInstances` resta vuoto.

Le scritture nei chunk del mondo sono state isolate nel sandbox. La verifica SHA-256 post-run conferma che i 40 file originali del manifest non sono cambiati.

## 7. Matrice finale

| Requisito | Esito |
|---|---|
| Preflight, shader, diagnostica D1.2.11 e isolamento | PASS |
| Fixture Terreno Standard e caricamento JSON/FWB | PASS sul percorso runtime; confronto strutturale indipendente non eseguito |
| Sei campioni hit, uno per faccia | NOT VERIFIED |
| Coordinate runtime UV/chart/griglia e `sizeSource` | NOT VERIFIED |
| Selezione terreno al centro/bordi/angolo | INCOMPLETE |
| Tie-break GUI esatto | NOT VERIFIED |
| Spawn iniziale e attivazione del secondo spawn | PASS |
| Confronto numerico teleport entro tolleranza sub-decimale | NOT VERIFIED |
| Validation Layer, ownership semafori e shutdown | PASS |
| Exit code 0 | NOT VERIFIED |
| Integrità dei 40 originali | PASS |
| Esito complessivo D1.2.12 | **INCOMPLETE** |

### Evidenze operative

- `runtime\logs\D1_2_12_run1.stdout.log`
- `runtime\logs\D1_2_12_run2.stdout.log`
- `runtime\logs\D1_2_12_playstate.stdout.log`
- `runtime\logs\D1_2_12_playstate_exitcode.stdout.log`
- `runtime\fixtures\D1_2_12\pre_run_fixture_manifest.json`

Non sono state modificate le sorgenti, gli algoritmi planetari, i serializer, gli enum o i salvataggi originali. Nessun commit o push è stato effettuato.


