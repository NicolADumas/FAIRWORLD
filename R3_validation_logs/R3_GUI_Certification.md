# FAIRWORLD — R3 Final GUI Certification

**Modalità:** validation only. Nessun sorgente è stato modificato, nessun commit/push e nessuna operazione di cleanup è stata eseguita.

## Build e configurazione

- Eseguibile: `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe`
- Build: Debug R3 definitiva, verificata dal log `Build_Debug_R3BRebuild.log` (`FULL_DEBUG_TARGETS_EXIT=0`). Timestamp eseguibile: `2026-11-03 18:13:46`.
- Avvii: `FairworldEditor.exe --disable-twitch-layer`; input iniziale `0` per Hub e `6` per Block Maker durante la verifica di riapertura.
- Tutti e tre i log registrano `VK_LOADER_LAYERS_DISABLE=*twitch*`, `Validation Layers: ATTIVI` e la registrazione di `VkDebugUtilsMessengerEXT`. Il binario Debug configura `VK_LAYER_KHRONOS_validation`; i callback hanno registrato gli avvisi di validazione riportati sotto.
- Nessun VUID o messaggio `[VULKAN VALIDATION ERROR]` nei tre log.

## Operazioni GUI

| Operazione | Esito | Evidenza |
|---|---|---|
| Hub → Block Maker → Painter → Hub, ciclo 1 | PASS | completato nell'istanza principale |
| stesso ciclo, ripetizione 2 | PASS | completato nell'istanza principale |
| stesso ciclo, ripetizione 3 | PASS | completato nell'istanza principale |
| Resize 1: massimizzazione | PASS | ricreazione swapchain registrata |
| Resize 2: ripristino dimensioni | PASS | ricreazione swapchain registrata |
| Materiali scientifici | PASS | elenco visibile: Pure Water, Granite, Basalt, Iron / Structural Steel, Titanium Carbide; nessuna associazione modificata |
| Painting locale | PASS | stroke Atlas Paint su UV Island 1; `StrokeEnd: Mode=AtlasPaint`, GPU upload riuscito |
| Salvataggio | PASS | `Save BlockID=1`, bake albedo riuscito, 1 PaintLayer scritto in `assets/layers/block_1_layers.json` |
| Riapertura e persistenza | PASS | nuova istanza in modalità Block Maker riporta `Caricati 1 PaintLayer persistenti`; i tratti delle isole 0 e 1 e la pittura sul cubo sono visibili |
| Chiusura normale | PASS | ogni processo completato con exit code 0; `Runtime shutdown complete YES` |

Un tentativo intermedio di riapertura con modalità Hub è entrato nel mondo FAIRWORLD invece di Block Maker a causa del puntamento GUI sulla schermata massimizzata. È stato chiuso normalmente, senza errori, exit code 0; non è conteggiato fra le operazioni di accettazione. La verifica successiva è stata eseguita selezionando direttamente la modalità Block Maker (6).

## Risultati Vulkan

| Log | Validation errors | VUID | Framebuffer | Semafori | Ownership gate | Exit |
|---|---:|---:|---:|---|---|---:|
| `Editor_R3Final_GUICert.log` | 0 | 0 | 12 creati, 0 live; tutti con RenderPass `0xc000000000c` | 6 creati, 6 distrutti, 0 live | `VULKAN OWNERSHIP: CLEAN (PASS)` | 0 |
| `Editor_R3Final_PersistenceReopen.log` | 0 | 0 | 9 creati, 0 live | 6 creati, 6 distrutti, 0 live | `VULKAN OWNERSHIP: CLEAN (PASS)` | 0 |
| `Editor_R3Final_PersistenceReopen_BlockMaker.log` | 0 | 0 | 6 creati, 0 live; tutti con RenderPass `0xc000000000c` | 6 creati, 6 distrutti, 0 live | `VULKAN OWNERSHIP: CLEAN (PASS)` | 0 |

Non risultano framebuffer creati con render pass nullo, errori di lifetime Painter, deadlock o crash. I log includono warning non bloccanti per l'attributo vertex in location 7 non consumato dallo shader; il criterio richiesto era zero errori, e il conteggio errori resta zero.

## Log completi

- `Editor_R3Final_GUICert.log` — sessione con i tre cicli, resize, materiali, stroke e salvataggio.
- `Editor_R3Final_PersistenceReopen.log` — avvio intermedio non conteggiato, terminato normalmente.
- `Editor_R3Final_PersistenceReopen_BlockMaker.log` — riapertura e verifica visiva della persistenza.

I file di painting generati sono stati conservati. Il fixture di packaging non è stato modificato durante questa certificazione.

## Decisione

Tutti i criteri GUI e Vulkan richiesti risultano PASS. Proposta: **`P0.3C3 — READY FOR FREEZE`**. Non dichiaro lo stato FROZEN.
