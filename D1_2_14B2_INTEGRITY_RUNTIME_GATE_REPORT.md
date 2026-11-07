# D1.2.14b.2 — Integrity & Runtime Gate Recovery

**Data:** 2026-11-04  
**Esito sintetico:** **GUI GATE — READY** (solo per avviare la verifica nella working directory sandbox verificata). **Lifecycle resta FAIL: 11/13.** I due file originali hanno divergenze dal manifest e non sono stati ripristinati o modificati in questa fase.

## A — Analisi dei file divergenti

| File | Manifest originale | Stato attuale | Git |
|---|---|---|---|
| `assets/texture_cache.bin` | 805,306,376 byte; SHA-256 `BFFDC3DD710108999E3251C349A27F2B27F27923AFBBE4570F21120EE4107851`; timestamp UTC `2026-11-03T14:59:08Z` | 805,306,460 byte; SHA-256 `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D`; timestamp UTC `2026-11-04T13:14:28Z` | Ignorato (`.gitignore:33`), non tracciato |
| `assets/test_packaging_artifacts/pkg_roundtrip_03.json` | 28,358 byte; SHA-256 `0F0FE0DC7F2BEB69BF229092628D8C9159CAB7CD3730C7F97C26B9B839BE36C5`; timestamp UTC `2026-11-03T21:57:58Z` | 28,358 byte; SHA-256 `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089`; timestamp UTC `2026-11-04T13:13:36Z` | Tracciato e modificato |

### Cache

Il file corrente inizia con la magic `FWPBRCH2`; il successivo `uint32` è versione `2`, seguito dai parametri 512×256 layer×4 canali e algoritmo di packing 1. La dimensione corrente corrisponde a header v2 di 92 byte più tre array da 256×512×512×4 byte. La dimensione del manifest corrisponde agli stessi payload preceduti soltanto dalla magic di 8 byte: è quindi compatibile con la vecchia rappresentazione senza header/fingerprint v2. La versione legacy è dedotta dalla struttura e dalla dimensione del manifest; il vecchio contenuto non è disponibile per una verifica diretta del suo header.

Il sorgente `src/render/TexturePacker.cpp` costruisce il percorso `m_assetRoot / "assets/texture_cache.bin"`, verifica formato/fingerprint e, quando necessario, apre la destinazione in troncamento e scrive la cache v2. `TexturePacker` usa per default `"."` come asset root; la risoluzione effettiva dipende quindi dal current working directory.

Esiste un meccanismo concreto con cui una suite avviata da CTest dalla root sorgente può scrivere l'originale: il working directory configurato per `FairworldLifecycleTests` è `${CMAKE_SOURCE_DIR}` e il test richiede PBR. La data/ora e il passaggio da dimensione legacy a v2 sono compatibili con una scrittura durante le operazioni del 4 novembre. Non è stato trovato un log persistente che dimostri quale singola esecuzione abbia scritto precisamente il file originale; la causa puntuale resta **non attribuita**.

### JSON PKG-03

`git diff` mostra soltanto due differenze: `manifest.creationTimestamp` e `manifest.lastModifiedTimestamp`, entrambi passati da `1793667422` a `1793798014`. Il resto del documento non differisce. `AssetPackagingContractTests::RunAllTests()` usa il percorso relativo `assets/test_packaging_artifacts`; PKG-03 apre e salva `fairworld:pkg_roundtrip_03` nello stesso `pkg_roundtrip_03.json`. `FairworldUVTests` ha working directory `${CMAKE_SOURCE_DIR}`. Il percorso di scrittura della suite e i due valori timestamp spiegano la divergenza; il timestamp file coincide con la finestra temporale delle esecuzioni del 4 novembre. Il test può quindi riscrivere questo fixture tracciato se lanciato dalla root sorgente. Non è stato eseguito in questa fase.

## B — Isolamento del sandbox

Sandbox verificato: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX`; working directory dei processi di test: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`.

- Il sandbox è composto da copie fisiche di `assets`, `saves`, configurazioni e directory runtime. La scansione ricorsiva corrente ha trovato **zero reparse point**.
- La verifica del rapporto D1.2.8 documenta anche l'assenza di link/junction/hardlink; i percorsi scritti dal runtime sono relativi al current working directory, inclusi cache, `saves`, fixture di prova e `imgui.ini`.
- Gli hash delle copie sandbox prima della riproduzione erano: cache `FDAE5AF153D101DB5CDD41F263CC22C3AF612C227CE3FC5574CA34DF6FE2FE0D`; JSON `0F0FE0DC7F2BEB69BF229092628D8C9159CAB7CD3730C7F97C26B9B839BE36C5`.
- La riproduzione Lifecycle ha cambiato la sola cache sandbox in `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D`, coerentemente con la rigenerazione loggata. Il fixture JSON sandbox è rimasto alla copia manifest.
- Hash degli originali immediatamente prima e dopo il processo Lifecycle: cache `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D` invariato; JSON `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089` invariato.
- `Verify-OriginalInputs.ps1` continua a segnalare le due divergenze dal manifest, già presenti prima del run; non segnala nuove modifiche dovute al run isolato.

**Conclusione isolamento:** dimostrato per i percorsi di scrittura runtime esaminati e per la riproduzione effettuata. Il fatto che il manifest originale sia divergente rimane una questione d'integrità distinta: non è stato ignorato, ma non impedisce di scrivere in una copia fisicamente separata verificata. La GUI può essere avviata nel sandbox; nessun risultato GUI è rivendicato in questo rapporto.

## C — EXT-10 e Lifecycle

### EXT-10 — `catalogGeneration`

Il rapporto `D1_2_6_EXT10_ISOLATION_REPORT.md` registra una riproduzione deterministica 4/4 del test EXT-10: 37/38 PASS, exit code 1. Il test serializza una `ScientificMaterialRegistry`, salva, ricarica e salva di nuovo, poi confronta le stringhe intere. La sola differenza rilevata è `/catalogGeneration`, JSON integer 1 contro 2. `ScientificMaterialRegistry.cpp` aggiorna deliberatamente il contatore monotono e lo serializza. L'audit non trova riferimenti a CubeSphereMapping, UVToGrid, PlanetGridCompatibility o MapWorldGenerator nel percorso del test: il difetto non risulta correlato alla patch D1.2.14b. EXT-10 non è stato corretto né rieseguito in questa fase.

### RLC-01 e RLC-03 — riproduzione isolata

Comando effettivo: `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldLifecycleTests.exe`; working directory: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`. Log completo: [`D1_2_14B2_lifecycle_isolated.log`](D:/FAIRWORLD/D1_2_8_GUI_SANDBOX/runtime/logs/D1_2_14B2_lifecycle_isolated.log). Durata wall clock 134.389 s; exit code **1**. Risultato suite: **11/13 PASS**, RLC-01 e RLC-03 FAIL.

| Test | Risultato misurato | Diagnosi supportata dal log |
|---|---|---|
| RLC-01 | FAIL, durata 32,272 ms. Al watchdog (>30 s): GV CpuReady, JS CpuReady, PBR NotReady, WorkerActive YES. | La richiesta concorrente raggiunge il packer mentre la cache non corrisponde al fingerprint e viene rigenerata. Dopo il watchdog lo shutdown richiede cancellazione; il packer completa la generazione/scrittura della cache e poi il worker registra cancellazione cooperativa prima di promuovere PBR a CpuReady. Join worker 2,259 ms. È un timeout dovuto a preparazione lenta e cancellazione tardiva, non un deadlock permanente. |
| RLC-03 | FAIL, shutdown 25,461.6 ms; IsShutdownComplete YES, PBRNotReady YES. | Il worker completa la chiamata corrente e lo shutdown termina. Nessun hang permanente: il limite di 1 s del test è superato di circa 24.46 s. È completamento tardivo/non cancellabile durante la fase corrente. |

La pipeline è `RuntimeManager::PrepareCpuPBRTextures()` → `TexturePacker(512)::PackMaterials()` → fingerprint dei materiali/dipendenze, lettura/validazione cache o rigenerazione. L'hashing usa SHA-256 in blocchi da 64 KiB e registra anche stato/assenza delle texture; il controllo di cancellazione avviene prima e dopo fasi e tra materiali, non dentro `HashFile`, il caricamento/cache payload o il loop per pixel del fallback.

Nei run con cache valida, `PackMaterials` impiega **24,950–25,295 ms** e logga cache v2 valida. Il cache contiene 768 MiB di payload: il codice carica i tre array in RAM e calcola inoltre SHA-256 sull'intero payload caricato. Il tempo misurato è aggregato; non ci sono timer interni che permettano di separare con evidenza il costo di fingerprint/hash, lettura dei 768 MiB e hash del payload. Nel cold miss RLC-01 il log conferma rigenerazione, scrittura cache e 768 MiB allocati; dopo la richiesta di stop il completamento della fase corrente dura altri 2.259 s prima dell'interruzione cooperativa. Non sono stati aumentati timeout.

Stato lifecycle complessivo: **11 PASS / 2 FAIL**, non 15/17 CTest. I 15/17 sono il precedente risultato CTest complessivo dichiarato nello stato iniziale, non il risultato del binario Lifecycle. La suite completa CTest non è stata rilanciata.

## D — Shader e SPIR-V

La sorgente `src/render/forge.vert` dichiara location 2 come `vec2 inUV`; `forge.frag` riceve `vec2 inUV` alla location 2 e lo usa come coordinata delle texture. `RenderManager.cpp` descrive l'attributo CPU location 2 come `VK_FORMAT_R32G32_SFLOAT`.

Verifiche eseguite senza sovrascrivere gli asset:

- Compilazione sorgente con `D:\VulkanSDK\Bin\glslc.exe` in `runtime\temp_d12\forge_vert_d1214b2_audit.spv`: exit code 0.
- Disassemblaggio dell'SPV sorgente, SPV in `bin\Debug` e SPV temporaneo con `spirv-dis.exe`: tutti exit code 0.
- L'SPV distribuito e quello in `bin\Debug` hanno SHA-256 identico `301C4C22EE8DF9175A47B8A92B590FB14618D38CFEB23AAAC49CFCA341EE8F7C`.
- L'SPV distribuito dichiara location 2 con input `%inRoughMetal` di tipo pointer a `v2float`; quello ricompilato dichiara `%inUV` di tipo pointer a `v2float`, anch'esso location 2. La differenza d'interfaccia osservata è il nome simbolico; location e tipo sono compatibili con il contratto UV. L'SPV temporaneo ha SHA-256 `4D1A1E79C5CCCF315E322C4E6627D7BD6F4442F3C2FA35BF05C1B1D958606658` e non è stato copiato sugli asset.

Questa verifica conferma il contratto d'interfaccia CPU/GPU location 2 (`vec2` UV), ma non sostituisce una prova visiva GPU della correttezza delle UV prodotte dalle mesh.

## E — Decisione del gate GUI

### **READY — isolamento verificato**

È possibile procedere con la sessione GUI usando esclusivamente la working directory `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`, gli asset/caches/salvataggi/configurazioni copiati nel sandbox e la verifica post-run degli hash originali. La riproduzione ha già dimostrato che la rigenerazione della cache colpisce la copia sandbox e non l'originale.

READY indica che la destinazione è isolata e sicura rispetto ai percorsi di scrittura auditati; **non** significa che i test Lifecycle o EXT-10 siano PASS, che la GUI sia stata eseguita o che D1 sia pronto/frozen. Non ripristinare né aggiornare il manifest dei due file originali senza un'autorizzazione distinta.

## F — Azioni successive proposte

1. Avviare la GUI di validazione solo con current working directory sandbox e ripetere hash originali prima/dopo.
2. Conservare il log Lifecycle isolato e trattare RLC-01/RLC-03 come regressioni aperte; prima di una patch, profilare separatamente fingerprint, lettura cache e verifica payload.
3. Gestire separatamente EXT-10 e la divergenza del fixture PKG-03; non ripristinare file originali automaticamente.
4. Nessun intervento sui sorgenti, serializer, test, dati utente o cache originale è stato fatto in questa fase. Non è stato eseguito commit/push.

## Controllo finale del worktree

`git diff --check` è stato eseguito dalla root Git ed è terminato con exit code **2** per whitespace finale preesistente in `src/states/BlockMakerState.cpp` alle righe 11565, 11572, 11573, 11808, 11830 e 11832. Questo file non è stato modificato durante D1.2.14b.2. I messaggi CRLF/LF sono avvisi Git separati e non errori del controllo.
