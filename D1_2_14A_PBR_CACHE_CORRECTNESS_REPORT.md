# D1.2.14a — PBR Texture Cache Correctness

**Stato:** implementazione e validazioni richieste completate. Nessun commit o push. Nessun freeze D1 dichiarato.

## Causa tecnica

Il formato precedente di `assets/texture_cache.bin` conteneva soltanto `layerCount`, risoluzione e i tre array pixel. `TexturePacker` accettava la cache in base a un controllo timestamp limitato a `materials.json` e al confronto di layer count/risoluzione. Non rilevava cambiamenti ai byte delle texture, comparsa/rimozione di file, payload troncati o corrotti e non aveva una versione esplicita del formato. La cache legacy sandbox misurava 805.306.376 byte ed era quindi eleggibile se i due interi coincidevano, anche se la layer 12 non corrispondeva al fallback materiale corrente.

## Modifiche apportate

- `src/render/TexturePacker.h/.cpp`
  - aggiunto `assetRoot` opzionale al costruttore; default `.` conserva i percorsi di produzione, mentre i test usano directory temporanee e l'esecuzione sandbox scrive sotto il sandbox;
  - introdotto formato cache **v2** con magic `FWPBRCH2`, versione formato, risoluzione, layer count, canali e versione algoritmo;
  - fingerprint SHA-256 deterministico degli interi di packing, byte di `materials.json`, sequenza ordinata di material ID/fallback/path e stato/byte SHA-256 delle dipendenze albedo, normal e ORM;
  - stato missing/not-regular/present incluso nel fingerprint; quindi comparsa o rimozione di una texture invalida la cache;
  - digest SHA-256 del payload per respingere cache troncate o alterate;
  - input non leggibili o cambiati durante il packing impediscono di accettare/salvare la cache. Nessun affidamento sui timestamp.
- `src/render/TextureCacheContractTests.cpp` e `CMakeLists.txt`
  - aggiunto runner CTest dedicato `FairworldTextureCacheTests`, con fixture create e rimosse sotto la directory temporanea dell'utente.
- `D1_2_14A_PBR_CACHE_CORRECTNESS_REPORT.md`
  - questo rapporto e i risultati operativi.

Gli ID materiali e l'ordine delle layer non sono stati modificati. Il mapping resta quello della sequenza fornita dal `MaterialRegistry` (che usa 256 slot indicizzati per ID). Vecchie cache vengono rifiutate e rigenerate automaticamente; serializer e salvataggi dei mondi non sono stati toccati. Nessun shader, `SphericalLOD`, `CubeSphereMapping`, definizione materiale o asset originale è stato modificato.

## Test e risultati

| Verifica | Comando/configurazione | Risultato | Exit code |
|---|---|---:|---:|
| `git diff --check` | Repository FAIRWORLD | PASS (solo avvisi Git preesistenti di conversione LF→CRLF) | 0 |
| Build Debug completa | `cmake --build build_r3z7 --config Debug --target ALL_BUILD -- /m:1 /v:minimal`, Visual Studio 18 2026/MSVC | PASS | 0 |
| Runner cache diretto | `bin\Debug\FairworldTextureCacheTests.exe` | PASS, 10/10 | 0 |
| CTest cache | `ctest --test-dir build_r3z7 -C Debug -R ^FairworldTextureCacheTests$ --output-on-failure` | PASS, 1/1 | 0 |
| D1.2 matematico | `bin\Debug\FairworldEditor.exe --validate-d12` | PASS, 4 gruppi | 0 |

Test di cache inclusi: reuse invariato; contenuto `materials.json` modificato; byte albedo/normal/ORM cambiati con timestamp ripristinato; file mancante, comparso e rimosso; cache troncata; payload corrotto; versione incompatibile; mapping ID/layer e pixel fallback ID12; risoluzione e layer count cambiati.

La prima build del runner si è fermata per un errore nel fixture (`WriteDefinitions` riceveva il wrapper temporaneo anziché il suo `path`). È stata corretta la chiamata nel solo test; il successivo `ALL_BUILD` Debug è terminato con exit code 0.

Log prodotti sotto `build_r3z7`:

- `D1_2_14A_debug_build.log` (tentativo iniziale, runner non compilato);
- `D1_2_14A_debug_build_retry.log` (build completa PASS);
- `D1_2_14A_cache_tests.log` (10/10 PASS);
- `D1_2_14A_validate_d12.log` (4/4 PASS).

## Rigenerazione sandbox e verifica layer 12

- Working directory: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`.
- Eseguibile: `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe --disable-twitch-layer`.
- Il Block Maker ha richiesto `PBRTextures`; il log registra il rifiuto della cache legacy, il packing completo di 256 layer e `Cache PBR v2 salvata`.
- La GUI è stata chiusa normalmente; processo terminato con exit code **0**.
- File scritto esclusivamente nel sandbox: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\assets\texture_cache.bin`.
- Header: magic `FWPBRCH2`, formato 2, algoritmo 1, risoluzione 512, 256 layer, 4 canali.
- Dimensione verificata: **805.306.460 byte**, identica a `92 + 3 × 256 × 512 × 512 × 4`.
- SHA-256 dell'intero file: `FDAE5AF153D101DB5CDD41F263CC22C3AF612C227CE3FC5574CA34DF6FE2FE0D`.
- Albedo layer 12: pixel iniziale e uniforme sull'intera layer **RGBA `(0, 6, 255, 255)`**, cioè il fallback blu configurato (`baseColorFallback` JSON `(0.0, 0.02476811408996582, 1.0)`) dopo la stessa conversione byte usata dal packer.
- Il log segnala sei mappe configurate ma assenti nel percorso relativo risolto durante questa esecuzione. Sono state usate le definizioni fallback; la risoluzione dei percorsi asset non è stata cambiata in questo task.
- Log runtime: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\D1_2_14A_cache_regeneration.log`.
- Vulkan Validation Layer attivo: **0 errori**, **6 warning** già osservati di attributo vertex location 7 non consumato. Ownership gate: **6 semafori creati, 6 distrutti, 0 live**, PASS.
- Integrità originali: `Verify-OriginalInputs.ps1` prima e dopo ha restituito `ORIGINAL_INPUT_HASHES_MATCH=40`.

## Compatibilità e regressioni

La vecchia cache non viene interpretata come v2: magic/versione/dimensione non corrispondono e il packing la rigenera. Il payload include gli array nello stesso ordine per layer; non è stata introdotta rinumerazione. La dimensione aumenta di 84 byte rispetto al file osservato nel sandbox, per header e digest aggiuntivi.

La cache è derivata, non autoritativa. Se il fingerprint non è disponibile, il packing in memoria continua ma non legge/scrive la cache. Le sei mappe assenti e i relativi fallback sono una questione distinta di packaging/percorso, lasciata invariata.

## Rollback

Rimuovere la registrazione del target `FairworldTextureCacheTests`, il relativo file di test e ripristinare `TexturePacker.h/.cpp`; rimuovere soltanto la cache v2 del sandbox se si desidera ripartire da fixture/cache legacy. Non ripristinare o cancellare salvataggi e asset originali. Il formato legacy si rigenera al successivo avvio se resta installato il codice v2.

## Stato conclusivo

**PASS** per correttezza di invalidazione, rigenerazione e fallback layer 12 nelle verifiche elencate. Rimane separata la configurazione dei sei percorsi texture mancanti; il presente intervento non li corregge né cambia gli asset. D1 non viene dichiarato FROZEN.
