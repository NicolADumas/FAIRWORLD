# FAIRWORLD — D1.2.8 Safe GUI Environment Report

## Decisione

**READY — ambiente isolato preparato per la successiva esecuzione GUI.** Questa fase non ha avviato FAIRWORLD né eseguito test GUI. Le directory sandbox sono copie fisiche, senza symlink/junction/hardlink; i percorsi di scrittura relativi individuati per Planet Mapper e PlayState risolvono nel sandbox.

## A — Audit dei percorsi

FairworldEditor usa percorsi relativi alla working directory del processo. In `main.cpp` non è impostata una working directory alternativa. Verrà avviato con working directory `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`, distinta dal progetto sorgente `D:\FAIRWORLD\FAIRWORLD`.

| Fase/componente | Percorso | Comportamento | Evidenza sorgente |
|---|---|---|---|
| Startup | `assets/definitions/{shapes,blocks,materials,scientific_materials,custom_materials}.json`, `assets/player.json` | Caricamento asset e player; il player viene letto, non scritto allo startup. | `src/core/app/main.cpp:279–323`; `src/core/app/FAIRWORLD.cpp:247–273` |
| Startup/cache GPU | `assets/texture_cache.bin` | `TexturePacker` tenta la lettura; quando crea una cache nuova/rigenerata la apre con `wb`. | `src/render/TexturePacker.cpp:47–59,178–187` |
| Planet Mapper load | `saves/map/world_map.json` e relativo `.fwb` | `WorldProjectManager::LoadProject` legge il JSON/binario e conserva il path come destinazione corrente. Se manca il documento crea il pianeta predefinito in memoria. | `src/core/app/main.cpp:323`; `src/world/WorldProjectManager.cpp:89–106` |
| Planet Mapper usage | `saves/map/world_map.json` e `saves/map/world_map.fwb` | La modifica della dimensione richiede `requestSave`; `PlanetMapperUI::Draw` invoca `DoSave`, che chiama `SaveProject`. Il salvataggio scrive JSON e binario. Il pulsante di apertura PlayState richiede anch'esso il salvataggio. | `src/states/components/PlanetMapperUI.cpp:121–133,501`; `src/states/components/PlanetMapperUI.cpp:15–18,270–280`; `src/world/WorldProjectManager.cpp:110–127`; `src/world/MapDocument.cpp:440–565,866–873` |
| PlayState load | `saves/map/world_map.json` | Carica il documento per spawn e generazione. La mappa passata al test sarà la copia sandbox. | `src/states/PlayState.cpp:100–150,245–280` |
| PlayState usage | `assets/player.json` | Scrittura condizionale da F1, morte o level-up. Il test userà una copia degli asset e non azionerà intenzionalmente F1. | `src/states/PlayState.cpp:401`; `src/core/app/FAIRWORLD.cpp:620,874` |
| PlayState teardown | `saves/world/chunk_<x>_<z>.bin` | `PlayState::~PlayState()` chiama `ClearWorld(true)`; questo salva tutti i chunk nella directory configurata `saves/world`. | `src/states/PlayState.cpp:40–45,245–248`; `src/world/GameWorld.cpp:201–206`; `src/world/WorldChunkManager.cpp:20–85` |
| Engine shutdown | `saves/world/chunk_<x>_<z>.bin` | `FairWorldEngine::Shutdown` salva i chunk di `m_forgeMaster`; `WorldChunkManager` usa per default `saves/world`. La scorciatoia F5 salva nello stesso percorso. | `src/core/app/FAIRWORLD.cpp:479,1955–1961`; `src/world/WorldChunkManager.h:73` |
| ImGui | `imgui.ini` | Il nome predefinito è relativo alla current working directory; viene scritto dopo modifiche UI e alla distruzione del contesto. | `third_party/imgui/imgui.cpp:1635,4488,16928` |
| AI server helper | `../ai_server/server.py` | Avviato da `main`; il codice attuale serve richieste HTTP e non contiene scritture su file. È stata copiata la directory per impedire che il path relativo esca dall'area di test. | `src/core/app/main.cpp:40–58`; `ai_server/server.py` |

Il fallback `projects/default_game.json` è un percorso di lettura PlayState; `projects` è stato copiato. L'audit mirato non ha trovato un percorso assoluto di scrittura per questi due stati. Gli eventuali percorsi `../assets` usati dal resolver sono coperti da una seconda copia `sandbox/assets`. Il processo non chiamerà il browser, il file picker o altri editor che salvano definizioni o layer.

## B — Sandbox preparato

Percorso dedicato:

```text
D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\
  runtime\
    assets\       (copia indipendente completa)
    saves\        (copia indipendente completa)
    projects\     (copia indipendente)
    imgui.ini      (copia)
    inputs\        (scelte iniziali Planet Mapper / PlayState)
    logs\          (destinazione stdout/stderr dei run futuri)
  assets\          (copia per resolver ../assets)
  ai_server\       (copia per ../ai_server/server.py)
  original_input_manifest.csv
  Verify-OriginalInputs.ps1
```

Il binario sarà quello Debug già compilato: `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe`. È caricato dalla sua directory eseguibile per le DLL; i percorsi dati analizzati sono risolti dalla working directory isolata. Nessun file è linkato all'originale. Il clone completo di `assets` include anche la cache texture da 768 MiB per mantenere il percorso di avvio equivalente; ogni sua eventuale rigenerazione avverrà nel clone.

La directory di prova è stata popolata copiando tutto `assets`, tutto `saves`, `projects`, `imgui.ini` e `ai_server`. Sono state preparate le scelte stdin `5` e `1` e le cartelle di log, ma non sono state usate per avviare il runtime. Le copie di `assets`, `saves` e `imgui.ini` corrispondono ai rispettivi SHA-256 originali; copie `projects` e `ai_server` verificate file per file. **0 reparse point** nel sandbox.

## C — Inventario e hash degli input originali

Manifest completo (40 file: tutti i file in `assets`, tutti i file in `saves`, `imgui.ini`) e script read-only di verifica:

- `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\original_input_manifest.csv`
- `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\Verify-OriginalInputs.ps1`

Il manifest è stato creato prima della copia. La verifica eseguita dopo la preparazione ha restituito **`ORIGINAL_INPUT_HASHES_MATCH=40`**.

Inventario `saves` all'inizio della preparazione:

| Percorso relativo al progetto | Byte | SHA-256 |
|---|---:|---|
| `saves/world/chunk_-1_0.bin` | 65,564 | `4500AD8C2159901434F001346DAAFCD0147071DBB4563A6BB5577405A23B60DB` |
| `saves/world/chunk_0_-1.bin` | 65,564 | `181DAE81C73E5037C7D2AA93A49692C743E918E616D598BF889DFF911561F917` |
| `saves/world/chunk_0_0.bin` | 65,564 | `8EEE5105349E428AC0A34F5AB0E08BE14DCEB10F2AB9E50C70D22C92175BC676` |
| `saves/world/chunk_0_1.bin` | 65,564 | `6E7365DAAD6F16D2D2BF0BE3967565335C5D6534CC99DC9FB58351D6B2762656` |
| `saves/world/chunk_1_0.bin` | 65,564 | `6D9018347F0CA5E1A9617021CC3ACA364F2A7F54827032537BC63328DED3424B` |
| `saves/map/world_map.fwb` | 1,301 | `E2E6C701EEC40E662990D1F3EF5787A46FDF055ECD6D200E690FA65B90F18BB0` |
| `saves/map/world_map.json` | 6,492 | `430D02BFC82A85020372D7B1B7C74B14E5CCC1A41CF7988DB8DAE20740F023D7` |

`saves/lab` esistente e vuota. La coppia `saves/map/world_map.json` / `.fwb` era già stata creata durante il precedente controllo Planet Mapper ed è stata esplicitamente **conservata**, copiata e inclusa nel manifest; nessuna cancellazione o rinomina è stata eseguita in questa fase.

Prima del futuro run verrà registrato il comando e il working directory. Dopo ciascun processo, `Verify-OriginalInputs.ps1` ricalcola SHA-256 di tutti i 40 file nell'albero originale e fallisce se un file manca o cambia. I risultati della GUI saranno presi esclusivamente dai log sotto `runtime/logs`; gli output runtime attesi resteranno sotto `runtime/assets` e `runtime/saves`.

## D — Controlli pre-run

| Controllo | Esito |
|---|---|
| Working directory runtime separata dal progetto originale | PASS |
| `assets/`, `saves/`, `projects/`, `imgui.ini` runtime risolti nel sandbox | PASS |
| Fallback `../assets` e AI server `../ai_server` confinati nel sandbox | PASS |
| Copia iniziale dei file originali verificata via SHA-256 | PASS (40 input) |
| Verificatore post-run degli originali preparato e provato | PASS (40 hash invariati) |
| Link/junction/hardlink rilevati nel sandbox | PASS (nessuno) |
| Salvataggi originali rimossi, rinominati o spostati | NO |
| GUI avviata in questa fase | NO, come richiesto |

## E — Limiti e procedura futura

La sicurezza è basata sull'audit delle scritture relative e sull'avvio del processo con working directory sandbox. Il binario resta il file esistente fuori dal sandbox, ma i punti di scrittura esaminati non usano la directory dell'eseguibile. La GUI futura dovrà evitare interazioni con altri editor o dialog di file esterni all'ambiente. La validazione post-run degli hash degli originali è obbligatoria; se fallisce, il gate GUI va fermato e il percorso effettivo investigato.

**Ready per eseguire la GUI solo da `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`.** Questo è unicamente un via libera dell'ambiente; non certifica ancora Planet Mapper o PlayState. Nessuna modifica ai sorgenti o alla configurazione permanente del progetto, nessun commit/push e nessun freeze D1.
