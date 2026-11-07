# FAIRWORLD P03e.1c — Toolchain Recovery & Real Material Witness

**Esito complessivo: PASS CPU / GO per P03e.2 CPU-only; GPU ancora NOT VERIFIED.**

## 1. Baseline e sicurezza

- Repository: `D:\FAIRWORLD\FAIRWORLD`.
- HEAD prima e dopo: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- Nessun sorgente di produzione, registry originale o fixture è stato modificato.
- Nessun reset, clean, commit, push, installazione o rigenerazione CMake.
- Il progetto, sorgente del runner, build output, PDB, log, working directory, `TEMP` e `TMP` dell'harness sono sotto `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03e1c-harness`.
- Gli output storici non sono stati sovrascritti. I runner Tiny storici sono stati eseguiti dai rispettivi binari esistenti in working directory dedicate.
- Nessuna inizializzazione Vulkan o GUI è stata eseguita.

## 2. Toolchain recuperata

La diagnosi precedente era dovuta a una shell non inizializzata: `cl.exe` e MSBuild non erano nel PATH, ma i componenti compatibili erano installati.

| Componente | Percorso/versione verificati |
|---|---|
| Visual Studio Community 2026 | `D:\Visual Code\vs`, versione `18.10.12224.181` |
| vcvarsall | `D:\Visual Code\vs\VC\Auxiliary\Build\vcvarsall.bat` |
| MSBuild | `D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe`, versione `18.10.1.42706` |
| MSVC | toolset `14.51.36231` (`v145`), compilatore `19.51.36260` |
| Windows SDK | `10.0.26100.0` |
| Variabili dopo `vcvarsall x64` | `VSINSTALLDIR=D:\Visual Code\vs\`; `VCToolsInstallDir=...\14.51.36231\`; `WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\` |

La versione corrisponde al toolset riportato nei log di P03d.3/P03d.4. Non è stato usato un compilatore alternativo.

## 3. Harness e build

È stato creato un progetto diagnostico indipendente in:

`D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03e1c-harness`

Il progetto deriva dalle impostazioni MSVC v145 del runner sandbox storico, ma compila esclusivamente `P03e1cMaterialWitness.cpp`. Riutilizza le librerie preesistenti `FairworldCore.lib`, Vulkan e Jolt; non modifica né ricompila tali librerie. Tutti gli output di ogni configurazione, compresi `.obj`, `.pdb`, `.lib` importata, manifest e log, sono stati rediretti nel nuovo albero sandbox.

Comandi di build eseguiti, dopo `vcvarsall.bat x64`:

```text
MSBuild.exe ...\p03e1c-harness\project\P03e1cMaterialWitness.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:BuildProjectReferences=false /m:1 /v:minimal
MSBuild.exe ...\p03e1c-harness\project\P03e1cMaterialWitness.vcxproj /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /m:1 /v:minimal
```

| Build | Esito | Exit code |
|---|---:|---:|
| Harness Debug | PASS | 0 |
| Harness Release | PASS | 0 |

Release ha emesso un warning informativo che `/LTCG` non era necessario per questo piccolo eseguibile; il link è riuscito. I log sono `build-debug.log` e `build-release.log` nel sandbox.

## 4. Caricamento reale dei registry e fixture

Il runner ha invocato i loader C++ di produzione `ShapeRegistry::LoadFromJson`, `BlockRegistry::LoadFromJson` e `MaterialRegistry::LoadFromJson`. Tutti hanno restituito successo. `MaterialAppearanceRegistry::BootstrapLegacyBlockAppearances` è stato eseguito come nel bootstrap runtime. Il fixture è stato caricato con `MapDocument::LoadJSON`.

Input utilizzati in entrambe le configurazioni:

- Shapes: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\assets\definitions\shapes.json`
- Blocks: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\assets\definitions\blocks.json`
- Materials: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\assets\definitions\materials.json`
- Fixture: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03e-material-witness\world_map.json`

Il documento contiene un pianeta, un template e un'istanza; `planetSize` è Tiny (`0` nell'enum osservato) e `isFlat=false`. La generazione runtime ha creato 54 chunk; TerrainSolver ne ha processati 54.

| BlockName | BlockID lookup / definizione | Material target ID | Fallback RGB | Albedo nel sandbox | Voxel | Vertici mesh |
|---|---:|---:|---|---|---:|---:|
| `fairworld:grass` | 1 / 1 | 1 | (0.3, 0.8, 0.2) | non configurata; fallback | 13.824 | 171.468 |
| `fairworld:thesims` | 12 / 12 | 12 | (0, 0.0247681, 1) | `textures/blocks/fairworld_thesims_albedo.png`, assente | 41.472 | 24.480 |
| `fairworld:carburotitanio` | 18 / 18 | 18 | (0.7, 0.7, 0.7) | presente e leggibile, 13.429 byte | 449.581 | 101.808 |

Per ciascun materiale: BlockRegistry lookup, `SimBlockDef.id` e `PBRMaterialDef.target_block_id` coincidono; il test osserva voxel e vertici effettivi, `Vertex.materialID` corrisponde al BlockID e il colore dei vertici corrisponde al fallback con tolleranza `1e-6`. Tutti i vertici controllati hanno attributi finiti. Totali fixture: **504.877 voxel non-Air**, **297.756 vertici**, **54 mesh CPU non vuote**, zero vertici non finiti.

Il texture path di ID 12 è effettivamente mancante, ma la mesh CPU conserva il fallback blu nei vertici. ID 18 ha texture presente e leggibile; questa verifica conferma il file su disco, non il caricamento texture GPU.

## 5. Debug/Release e log

Il comando effettivo per entrambe le esecuzioni è il binario harness corrispondente con `--shapes`, `--blocks`, `--materials` e `--fixture` ai percorsi sopra riportati. Working directory e directory temporanee erano rispettivamente `...\p03e1c-harness\run-debug` e `...\p03e1c-harness\run-release`.

- Debug: PASS, exit code 0.
- Release: PASS, exit code 0.
- I log normalizzati sono identici; SHA-256 di entrambi: `7FCA3046185B3E2F6E00D3E22622A0A3BC01F115308DCD9C8C8B2BDE1D67D62F`.
- Conteggi BlockID, vertex material ID, colori, conteggi vertici e voxel coincidono.
- Nessun `255` è stato usato come sostituto per i tre testimoni.

Questo dimostra il percorso CPU registry → generazione voxel → mesher CPU per il fixture selezionato. Non dimostra descriptor/layer sampling, CacheManager live sync o resa pixel GPU.

## 6. Runner Tiny storico

Sono stati rieseguiti separatamente i binari precompilati storici con il fixture Tiny originale `...\p03d4-fixture\world_map.json`, non con il fixture P03e.

| Configurazione | Esito / exit code | Evidenza |
|---|---:|---|
| Runner Tiny Debug | PASS / 0 | 54 chunk, 504.877 voxel ID 3, 54 mesh CPU, 99.252 triangoli, zero mismatch di winding/materiali |
| Runner Tiny Release | PASS / 0 | stessi conteggi e verifiche del runner Debug |

Il runner storico dichiara esplicitamente GPU publication/upload **NOT VERIFIED**. Log e SHA-256: Debug `8745F9B11106A9E3206A8B3D89DE9529BAC1D5C120DED4EBB153F009A09D0794`; Release `990409DE4EF20D74D51C03411890B3C8BE1590BFB815CA6746CC03272DA6CBBE`.

## 7. Integrità

Gli hash SHA-256 iniziali e finali coincidono per i file di input originali e di fixture controllati. Le copie sandbox dei tre registry hanno hash uguali ai corrispondenti originali.

| File | SHA-256 |
|---|---|
| Originale `assets/definitions/shapes.json` e copia sandbox | `14F903D98029369D35BE9FA16F87069BDE21027023726006B85800135EA44328` |
| Originale `assets/definitions/blocks.json` e copia sandbox | `66922BF44F11FC19BD0E59B1A919364056507CC731EFBAF122152BB07BF34744` |
| Originale `assets/definitions/materials.json` e copia sandbox | `33C1E113852B22C0ECAF51020B8E4B6B195CDE2918925EF1C98D9FEF58F406E6` |
| Fixture sandbox P03e | `AD62FE45DAB43BF3C465F13D7832AB937E271549159A172BBF89D278F5D3C7B5` |

HEAD è rimasto invariato e non è stato eseguito alcun commit o push. La working tree conteneva già numerosi file modificati/non tracciati; gli artefatti diagnostici di questa fase sono stati scritti nel sandbox, mentre questo report è l'unico nuovo file previsto nel repository.

## 8. Decisione

**GO per P03e.2 CPU-only:** la toolchain originale è disponibile e il witness reale dei registry è stato eseguito con loader C++ di produzione; Debug/Release coincidono e il runner Tiny storico resta PASS.

**GPU: NOT VERIFIED.** La sessione non ha avviato Vulkan: restano da verificare la sincronizzazione/cache GPU, la layer effettivamente campionata e il pixel risultante. La diagnosi visiva dei blocchi grigi non può essere chiusa con questa evidenza CPU.
