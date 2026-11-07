# D1.2.14c — Interrupted Session Report

**Stato finale: INTERRUPTED — PRE-FLIGHT PASS — VISUAL VALIDATION INCOMPLETE**  
Computer Use è stato interrotto dall’utente tramite Escape. Non è stata ripresa la GUI. Questo documento usa soltanto il baseline e i log già prodotti.

## 1. Preflight

| Controllo | Esito | Evidenza |
|---|---|---|
| Working directory | PASS | `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`, riportata nel file sessione. |
| Eseguibile/argomento | PASS | `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe --disable-twitch-layer`. |
| Inventario SHA-256 originali | PASS | 40 righe registrate in `runtime\logs\D1_2_14C_originals_before.csv`; 40/40 file presenti. |
| Reparse point nel sandbox | PASS | 0 rilevati. |
| Hardlink originali/sandbox | PASS | 0 coppie con lo stesso file ID NTFS. |
| Copie sandbox mancanti | PASS | 0 dei 40 file del manifest. |
| Divergenze baseline | 2, già note | Dettagli sotto; manifest non aggiornato e originali non ripristinati. |

### Le due divergenze già note

| File originale | Valore attuale pre-sessione | Valore manifest |
|---|---|---|
| `assets/texture_cache.bin` | 805,306,460 byte; SHA-256 `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D`; timestamp UTC `2026-11-04T13:14:28Z` | 805,306,376 byte; SHA-256 `BFFDC3DD710108999E3251C349A27F2B27F27923AFBBE4570F21120EE4107851` |
| `assets/test_packaging_artifacts/pkg_roundtrip_03.json` | 28,358 byte; SHA-256 `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089`; timestamp UTC `2026-11-04T13:13:36Z` | 28,358 byte; SHA-256 `0F0FE0DC7F2BEB69BF229092628D8C9159CAB7CD3730C7F97C26B9B839BE36C5` |

Il CSV pre-sessione registra queste due divergenze; le altre 38 corrispondono al manifest.

## 2. SPIR-V e pipeline Vulkan

La selezione del file è ricostruita dal resolver `RenderManager::ReadFile()` e dall’ordine dei candidati, non da una traccia runtime del path aperto (il log non stampa quel path). Per `forge_vert.spv` il candidato relativo alla working directory non esiste; il primo candidato disponibile è quindi:

`D:\FAIRWORLD\FAIRWORLD\bin\Debug\forge_vert.spv`

SHA-256: `301C4C22EE8DF9175A47B8A92B590FB14618D38CFEB23AAAC49CFCA341EE8F7C`. La copia sandbox `runtime\bin\forge_vert.spv` ha lo stesso SHA-256. L’SPIR-V disassemblato dichiara input location 2 come `v2float` (simbolo legacy `inRoughMetal`), coerente come tipo/location con UV `vec2`. La lettura effettiva avviene dall’asset adiacente all’eseguibile, fuori dal runtime sandbox ma in sola lettura; non è stato modificato alcun asset SPIR-V.

Il log conferma `Validation Layers: ATTIVI` e l’impostazione `VK_LOADER_LAYERS_DISABLE=*twitch*`.

- Errori Vulkan Validation Layer: **0**.
- Warning location 7: **6 righe** nel log; tre all’inizializzazione della pipeline e tre dopo la ricreazione pipeline/swapchain. Messaggio: vertex attribute alla location 7 non consumato dallo shader. Il messaggio non riporta un VUID.
- Ownership gate: **PASS**, 6 semafori creati, 6 distrutti, 0 live; log `VULKAN OWNERSHIP: CLEAN (PASS)`.

## 3. Evidenza GUI osservata

Gli screenshot erano mostrati inline durante la sessione, ma non sono stati salvati come file persistenti. Descrizione limitata a ciò che era visibile:

1. Primo frame: finestra FAIRWORLD con area completamente bianca.
2. Schermata successiva Block Maker: pannello con 18 block asset attivi, blocco selezionato `[1] Grass`, pannelli UV/Painter e preview. La barra diagnostica mostrava `Geo:0 Mesh:0 GPU:0`; il log riportava anche preview mesh Block Maker con 36 vertici e 12 triangoli.
3. Schermata successiva PlayState: ambiente uniforme grigio, pannello con spawn `D1.2.12 Spawn +Z` e `D1.2.12 Spawn +X`, giorno 1 e velocità 0 km/h. Non era visibile una mesh planetaria/voxel identificabile.

Il log riporta il caricamento dello spawn custom `+Z` a `(0, 0, 66)` e della mappa sandbox. Riporta inoltre `Active independent LOD mesh count in PlayState: 0` e `Active voxel mesh count: 0`.

## 4. Matrice dei controlli visuali

| Area / controllo | Esito | Evidenza o motivo |
|---|---|---|
| Block Maker si apre e UI appare | PASS limitato all’apertura | Screenshot inline e log `BlockMakerState::Init()` / primo `Render()`; non certifica il contratto GPU visuale. |
| Albedo valida | NOT VERIFIED visivamente | Il log registra `fairworld_Grass_albedo.png` caricato sul layer 1 e aggiornamento texture. Non è stato completato un controllo visivo del campionamento/colore. |
| Texture mancante e fallback | NOT VERIFIED visivamente | Il packer registra fallback per sei albedo mancanti (Water, thesims, rampa, emisfero, granito, carburotitanio). I relativi pixel/colori non sono stati verificati a schermo. |
| Materiale a colore uniforme | NOT VERIFIED | Nessun campione selezionato e verificato. |
| Materiale con ORM | NOT VERIFIED | Nessuna verifica visiva o strumentale dedicata. |
| Material ID 12 | NOT VERIFIED | `[12] PRISMA SIMS` era visibile nella lista, ma non è documentato come selezionato o testato. |
| Sei facce, UV, ripetizione/stretching, normali del blocco voxel | NOT VERIFIED | Nessun controllo completo delle sei facce. La preview Block Maker non è prova equivalente di una mesh voxel reale. |
| Chunk Editor e mesh voxel reale | NOT VERIFIED | L’editor non è stato visitato; in PlayState il contatore voxel era zero. |
| Planet Mapper: globo non dipinto, colorato, testurizzato | NOT VERIFIED | Nessun campione Planet Mapper documentato. |
| Sei facce planetarie, continuità Cube-Sphere, bordi e cambi LOD | NOT VERIFIED | Nessuna osservazione runtime dedicata; in PlayState mesh LOD indipendenti = 0. |
| PlayState: mesh LOD | NOT VERIFIED | Contatore LOD indipendenti pari a 0. |
| PlayState: mesh voxel | NOT VERIFIED | Contatore voxel pari a 0; come richiesto, nessuna equivalenza con la preview LOD o Block Maker. |
| Teletrasporto tra `+Z` e `+X` | NOT VERIFIED | Il controllo non è stato completato prima dell’interruzione. Il log attesta solo lo spawn iniziale `+Z`. |

## 5. Log, scritture e integrità post-sessione

File già prodotti:

- Log principale: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_14C_stdout_20261104_144253.log`.
- Metadata sessione: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_14C_session_20261104_144253.txt`.
- Baseline hash: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_14C_originals_before.csv`.
- Primo avvio senza input standard: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_14C_stdout_20261104_144147.log`; raggiunse il prompt di selezione modalità, senza registrazione dell’exit code.

Il metadata del secondo avvio registra working directory e argomento richiesti, modalità iniziale Block Maker, fine processo alle `2026-11-04T14:46:03+01:00` ed exit code **0**. Questo exit code non viene usato come prova di completamento dei controlli visuali né come prova di shutdown Vulkan privo di errori.

Il log registra scritture/aggiornamenti nel sandbox: `WorldStructureManager` ha salvato `assets/blocks/fairworld_Grass.fwblock`; la texture PBR del layer 1 è stata aggiornata; sono presenti eventi diagnostici di stroke Painter. La barra UI riportava `JSON Saves: 0`. Non è stata verificata l’integrità post-sessione dei file sandbox.

**Hash originali post-sessione: NON VERIFICATI**, come richiesto per la sessione interrotta. Non è possibile concludere che i due originali siano rimasti invariati dopo l’avvio solo sulla base del preflight. Nessun ripristino, aggiornamento del manifest, modifica a sorgenti/shader o commit/push è stato effettuato durante la documentazione offline.

## 6. Esito

**INTERRUPTED — PRE-FLIGHT PASS — VISUAL VALIDATION INCOMPLETE**

RLC-01, RLC-03 ed EXT-10 restano aperti e non sono stati corretti. Non si dichiara PASS per il rendering planetario o voxel e non si dichiara D1 FROZEN. La ripresa della verifica grafica richiede una nuova autorizzazione esplicita dell’utente.
