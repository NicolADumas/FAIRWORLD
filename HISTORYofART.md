# HISTORY of ART: FAIRWORLD VR Engine

Questo è il documento vivente che descrive cosa stiamo costruendo, le scelte architetturali effettuate e l'attuale "Stato dell'Arte" (il progresso) del progetto. Questo file verrà letto e aggiornato automaticamente dopo ogni fase importante dello sviluppo, basandosi sui parametri implementati.

## Cosa stiamo progettando
Stiamo costruendo **FAIRWORLD**, un'opera di design concepita come un **ACTION RPG super immersivo in Realtà Virtuale**. 

Per raggiungere questo livello di immersione totale e non scendere a compromessi, il cuore di FAIRWORLD è un **motore VR nativo e ad alte prestazioni** scritto in C++ da zero. L'obiettivo è piegare l'hardware alle nostre esigenze narrative e di gameplay, mantenendo requisiti ferrei (90/120 fps costanti, latenza invisibile) per garantire una presenza fisica perfetta nel mondo di gioco.

Le tecnologie core scelte per sostenere questa visione sono:
- **OpenXR**: Lo standard industriale per interfacciarsi con i visori VR (Meta Quest, SteamVR), permettendo di leggere i movimenti fisici del giocatore per combattimenti e interazioni fluide nell'Action RPG.
- **Vulkan**: L'API grafica di basso livello scelta per le sue altissime prestazioni, per spingere al massimo la resa visiva senza impattare il framerate.
- **miniaudio**: Un motore audio essenziale per gestire il suono 3D spaziale (HRTF), cruciale per percepire i nemici o gli eventi atmosferici attorno al giocatore.
- **C++ Moderno**: Architettura pulita e separazione delle responsabilità per mantenere il controllo totale su fisica, logica del GDR e rendering.

### La Filosofia di Sviluppo: L'Editor In-Game (God Mode)
Per plasmare FAIRWORLD, non useremo editor tradizionali su schermi piatti. Costruiremo un **Editor In-Game immersivo**: una sorta di "inventario divino" richiamabile direttamente in VR. Durante la fase di sviluppo, potremo estrarre oggetti, nemici e pezzi di scenario da questo inventario e posizionarli fisicamente nel mondo usando le nostre stesse mani, salvando la mappa in tempo reale.

---

## Stato dell'Arte (Stato Attuale)

**Progresso stimato: 45% (Engine 3D Interattivo Funzionante)**

### Parametri e Moduli Implementati (Struttura Definitiva):
- [x] **FairWorldEngine (`FAIRWORLD.h/cpp`)**: Game Loop con `Init()`, `Run()`, `Update(deltaTime)` e `Render()`. Architettura di Fallback Desktop Mode se il visore non è collegato.
- [x] **XrManager (`XrManager.h/cpp`)**: Inizializzazione reale di OpenXR (`XrInstance`).
- [x] **RenderManager (`RenderManager.h/cpp`)**: Pipeline Vulkan completa — `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, `VkSwapchain`, `VkRenderPass`, `VkFramebuffer`, `VkCommandPool`, sincronizzazione multi-frame (MAX_FRAMES_IN_FLIGHT=2), Uniform Buffer Object (UBO), Descriptor Sets, Vertex Buffer e Index Buffer con staging pattern GPU-ottimizzato. Rendering con `vkCmdDrawIndexed`.
- [x] **WindowManager (`WindowManager.h/cpp`)**: Finestra nativa Win32 con message loop.
- [x] **AudioManager (`AudioManager.h`)**: Inizializzato con `miniaudio`. Include `UpdateListener` e `PlaySound3D`.
- [x] **Gestione Dipendenze**: Vulkan SDK, OpenXR.Loader, GLM (incluso manualmente nella directory del progetto).

### Sistema 3D — MVP e Camera:
- [x] **Shader GLSL (`shader.vert/frag`)**: Vertex shader con UBO per matrici `model`/`view`/`proj` (MVP). Fragment shader con colore per vertice. Compilati in SPIR-V con `glslc`.
- [x] **Camera (`Camera.h/cpp`)**: Telecamera 3D con angoli di Eulero (Yaw/Pitch). Movimento WASD+QE con `deltaTime`. Rotazione mouse con `GetCursorPos`. Calcolo vettori `Front`/`Right`/`Up` via cross product. Limitazione Pitch per evitare gimbal lock.
- [x] **Input Sistema**: `GetAsyncKeyState` per tastiera (W/A/S/D/Q/E + Frecce) e mouse (click sinistro/destro). Edge detection per distinguere click singolo da tasto tenuto premuto.

### World System — Sandbox 3D Stile Minecraft:
- [x] **World (`World.h/cpp`)**: Griglia 3D di blocchi 16×16×16. Tipi: `Air`, `Grass` (verde), `Dirt` (marrone), `Stone` (grigio). Pavimento di erba automatico al Layer Y=0.
- [x] **Generazione Mesh**: `BuildMesh()` con face culling CPU — genera solo le facce dei blocchi esposte all'aria. Ombreggiatura directional fake (top chiaro, lati scuri, fondo scurissimo) per dare volume senza lighting reale.
- [x] **Building Tools**: Raycast DDA dalla camera (passo 0.05, distanza max 8 unità). **Click Sinistro = Piazza blocco**, **Click Destro = Rimuovi blocco**. Ricostruzione mesh + re-upload GPU automatico dopo ogni modifica.

### Lavori in Corso / Prossimi Passi:
- [ ] **Selettore di blocchi**: Tasti 1/2/3 per scegliere il tipo (Erba, Terra, Pietra).
- [ ] **Crosshair/Mirino**: Overlay 2D al centro dello schermo.
- [ ] **Gravità e Collisioni**: Fisica del player — caduta e collisione con il terreno.
- [ ] **Mondo più grande**: Generazione procedurale del terreno con rumore di Perlin.
- [ ] **Texture**: Sostituire i colori flat con immagini PNG (Vulkan texture sampler).
- [ ] **Salvataggio**: Serializzazione del mondo su file binario.
- [ ] **VR Mode**: Integrazione OpenXR completa con rendering stereo (`RenderStereo`).
