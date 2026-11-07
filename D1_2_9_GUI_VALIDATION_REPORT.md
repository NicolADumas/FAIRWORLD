# FAIRWORLD — D1.2.9 Isolated GUI Validation Report

## Esito

**INCOMPLETE.** L'isolamento è riuscito e Planet Mapper / PlayState hanno completato i controlli effettivamente eseguibili. Non sono però state verificate in GUI tutte le sei orientazioni dinamiche, la coerenza UV/chart ottenuta dal raycast e la selezione del terreno ai bordi: il catalogo non esponeva modelli compatibili e l'interazione col raycast non ha prodotto una misura osservabile. Questi controlli restano **NOT VERIFIED**; non si dichiara D1 FROZEN.

Nessun sorgente o test è stato modificato. Non sono stati modificati gli originali, né cancellati file. Nessun commit o push.

## A — Comandi, eseguibile e isolamento

- Eseguibile Debug: `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe`.
- Working directory obbligatoria: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`.
- Avvio effettuato con `--disable-twitch-layer`; i log riportano `Validation Layers: ATTIVI`.
- I processi sono stati avviati dalla working directory sandbox con redirezione stdout/stderr verso `runtime\logs`. La prima invocazione Planet Mapper ha usato il redirect stdin da `inputs` e ha terminato con exit code 1; le esecuzioni successive hanno terminato con exit code 0.
- La prima invocazione non trovava `glb_vert.spv` e `glb_frag.spv` perché l'eseguibile risiede nella directory originale mentre la CWD è il sandbox. Sono stati copiati nel sandbox tutti gli 11 shader SPIR-V già compilati (`runtime\bin`); gli hash delle copie corrispondono ai file sorgente. Non è stato cambiato codice o build.
- Il controllo reparse point del preflight D1.2.8 aveva dato zero link/junction. I percorsi relativi di scrittura identificati da quell'audit risolvono sotto `runtime`.

Comando di avvio (forma usata per ciascuna sessione; cambia l'input/log per modalità e run):

```powershell
Set-Location 'D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime'
cmd.exe /c '"D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe" --disable-twitch-layer < inputs\planetmapper.stdin > logs\D1_2_9_planetmapper_2.log 2>&1'
```

Per PlayState è stato usato l'input `inputs\playstate.stdin` e il log `logs\D1_2_9_playstate.log`. I log completi sono conservati nel sandbox.

## B — Planet Mapper

| Controllo | Evidenza / risultato |
|---|---|
| Apertura e rendering | **PASS.** Planet Mapper si è aperto e ha renderizzato il pianeta dopo aver reso disponibili gli shader nel sandbox. |
| Validation Layer | **PASS.** I log delle sessioni riuscite riportano layer attivi, zero errori di validation e Debug Messenger registrato. |
| Dimensioni e risoluzioni | **PASS.** La GUI ha mostrato: Tiny `N=3`, Small `N=5`, Medium `N=7`, Large `N=11`, Huge `N=21`, Gigantic `N=41`. I totali mostrati/derivati per sei facce sono rispettivamente `54`, `150`, `294`, `726`, `2646`, `10086` celle/chunk. |
| Orientamento dinamico delle facce | **INCOMPLETE.** È stata osservata la faccia camera `NORD (+Z)`; il selettore spawn mostrava tutte le sei facce (`+Z`, `-Z`, `+X`, `-X`, `+Y`, `-Y`). Le altre cinque orientazioni della camera non sono state verificate singolarmente. |
| Debug orientamento | **PASS parziale.** Il toggle “Show Orientation Debug” ha visualizzato gli assi colorati. Questo non dimostra da solo l'orientamento di tutte le facce. |
| Coerenza UV/chart/celle | **NOT VERIFIED.** I valori UV/chart non erano esposti nella GUI. Il click sul globo e il raycast non hanno fornito un valore numerico acquisibile; il passaggio del puntatore ai controlli UI azzerava l'hit corrente. |
| Terreno vicino ai bordi | **NOT VERIFIED / non disponibile.** Il catalogo mostrava “Nessun modello compatibile”; non era possibile selezionare un template di terreno ai bordi della griglia. |
| Spawn | **PASS.** Creato `Spawn 1` nel documento sandbox. UI e JSON riportano faccia `+Z` (face index 0), `localX=0`, `localZ=0`, `heightOffset=10`. |
| Salvataggio e riapertura | **PASS.** La sessione successiva ha ricaricato `saves/map/world_map.fwb` e `.json`; la dimensione salvata era Small e `Spawn 1` conservava faccia e offset. |
| Chiusura | **PASS.** Le due sessioni Planet Mapper sono terminate normalmente, exit code 0. |

Il documento di prova risultante in `runtime\saves\map` contiene `planetSize=1` (Small) e lo spawn descritto sopra. È stato scritto soltanto nella copia isolata.

## C — PlayState

| Controllo | Evidenza / risultato |
|---|---|
| Caricamento della mappa sandbox | **PASS.** Il log conferma caricamento del `.fwb` e del JSON da `saves/map` relativo alla CWD sandbox. |
| Spawn iniziale | **PASS.** Log: posizione custom `0.00, 0.00, 50.00`. Per Small (raggio 40), faccia +Z e offset 10, il punto è coerente con la direzione +Z. Il terreno e l'HUD sono stati renderizzati. |
| Teletrasporto allo spawn | **PASS parziale.** Il pannello mostrava il pulsante `Spawn 1`; è stato attivato e la vista è cambiata. La posizione iniziale coincideva già con lo spawn e lo spostamento non è stato misurato numericamente: il delta esatto è **NOT VERIFIED**. |
| Teletrasporto tramite portali | **NOT VERIFIED / non disponibile.** L'interazione di viaggio tramite `PortalSystem` non era esposta nel percorso GUI provato. |
| Direzione rispetto alla faccia | **PASS per lo spawn provato.** La faccia +Z produce posizione +Z. Altre facce non sono state provate nel PlayState. |
| Teardown | **PASS.** Chiusura normale, exit code 0; log finale indica runtime shutdown completato e ownership Vulkan pulita. |

Log PlayState: `saves/map/world_map.fwb` e `saves/map/world_map.json` caricati; raggio impostato per `size: 1`; `Spawn 1` usato a `(0,0,50)`.

## D — Vulkan, exit code e log

| Sessione | Exit code | Validation Layer | Errori Vulkan | Semafori / ownership | Shutdown |
|---|---:|---|---:|---|---|
| Planet Mapper, tentativo iniziale senza shader nel sandbox | 1 | Attivo | **2** errori, entrambi `VUID-VkPipelineShaderStageCreateInfo-stage-08771`, causati da moduli shader vuoti dopo il mancato caricamento di `glb_vert.spv` / `glb_frag.spv` | La sessione è fallita prima del gate finale | Avvio fallito |
| Planet Mapper, dopo copia shader | 0 | Attivo | 0 | 6 creati, 6 distrutti, 0 live; `VULKAN OWNERSHIP: CLEAN (PASS)` | `Runtime shutdown complete YES` |
| Planet Mapper, riapertura | 0 | Attivo | 0 | 6 creati, 6 distrutti, 0 live; `VULKAN OWNERSHIP: CLEAN (PASS)` | `Runtime shutdown complete YES` |
| PlayState | 0 | Attivo | 0 | 6 creati, 6 distrutti, 0 live; `VULKAN OWNERSHIP: CLEAN (PASS)` | `Runtime shutdown complete YES` |

Le due righe di validation del tentativo iniziale hanno lo stesso VUID e identificano shader stage non validi; non sono errori riprodotti nei run riusciti. La copia degli shader ha rimosso la condizione ambientale; non si è modificato il renderer.

Log integrali:

- `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_9_planetmapper.log`
- `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_9_planetmapper_2.log`
- `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_9_planetmapper_reopen.log`
- `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_9_playstate.log`

## E — Integrità e scritture

Il verificatore previsto dal preflight è stato eseguito al termine:

```text
ORIGINAL_INPUT_HASHES_MATCH=40
VERIFY_EXIT=0
```

Tutti i 40 file originali del manifest (asset, salvataggi e `imgui.ini`) mantengono lo SHA-256 iniziale.

Il confronto tra i 40 hash iniziali e le copie del sandbox rileva otto file sandbox cambiati: cinque chunk preesistenti sotto `runtime\saves\world`, i due file di prova `runtime\saves\map\world_map.json/.fwb` e `runtime\imgui.ini`. Sono scritture previste delle sessioni isolate e non alterazioni degli originali. Dopo PlayState la directory sandbox `runtime\saves\world` contiene 13 chunk: gli otto nuovi nomi generati sono `chunk_-1_-1.bin`, `chunk_-1_1.bin`, `chunk_-2_0.bin`, `chunk_0_-2.bin`, `chunk_0_2.bin`, `chunk_1_-1.bin`, `chunk_1_1.bin`, `chunk_2_0.bin`; i cinque chunk già copiati sono stati riscritti dal runtime. `runtime\saves\map` contiene i due documenti di prova aggiornati. I log, gli input e gli 11 shader copiati sono anch'essi nel sandbox. Nessun file di salvataggio è stato cancellato.

I file `saves/map` originali, inclusi i due file già esistenti prima di questa validazione, sono rimasti intatti secondo il manifest. Nessun ripristino è stato tentato o necessario.

## Matrice di chiusura

- Isolamento e integrità originali: **PASS**.
- Planet Mapper apertura/rendering, sei taglie, creazione e persistenza spawn: **PASS**.
- PlayState caricamento, spawn +Z, interazione UI spawn e teardown: **PASS**, con delta teletrasporto non misurato.
- Validation Layer e ownership/semafori nei tre run riusciti: **PASS**.
- Sei orientamenti visuali, lettura GUI UV/chart/raycast e selezione terreno ai bordi: **NOT VERIFIED**.
- Teletrasporto tra portali: **NOT VERIFIED**.

**Raccomandazione: INCOMPLETE**, perché alcune verifiche GUI richieste non sono state realmente dimostrate. Per completare il gate restano necessari controlli interattivi osservabili per le sei facce, UV/chart e selezione ai bordi con terreno compatibile; misurare anche la posizione prima/dopo il teletrasporto allo spawn. Questo rapporto non dichiara D1 FROZEN.
