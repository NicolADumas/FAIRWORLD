# FAIRWORLD P02c — Cross-Face Mesher Neighborhood Integration

**Esito:** PASS CPU per risoluzione topologica, adattamento del piano di confine e meshing Tiny headless. **GPU/GUI:** NOT VERIFIED. Nessun freeze, commit o push.

## 1. Safety gate e baseline

- Repository: `D:\FAIRWORLD\FAIRWORLD`; HEAD invariato `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- Working tree già DIRTY secondo P01 (49 file modificati e 375 non tracciati allo snapshot P01). Nessuna modifica preesistente è stata ripristinata o rimossa.
- Nessun asset o salvataggio è stato scritto. Il fixture Tiny è stato soltanto letto; SHA-256 attuale `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`, uguale al valore riportato nelle verifiche precedenti.
- Non è stato riconfigurato CMake. È stato riutilizzato `build_r3z7`/Visual Studio 18 2026 e sono state collocate librerie, eseguibili, progetto runner temporaneo e log esclusivamente nel sandbox `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`.
- Rispetto agli hash critici P01, `GameWorld.cpp` e `MapWorldGenerator.cpp` sono diversi. `MapWorldGenerator.cpp` era già oggetto del lavoro P02b; P02c non lo ha modificato. Gli altri 11 file critici P01 elencati sotto risultano invariati.
- `git diff --check -- src/world/GameWorld.cpp`: PASS, exit 0. Nessun check globale è usato per attribuire a P02c le whitespace preesistenti in altri file dirty.

## 2. Root cause

Il percorso reale in `GameWorld::Update` cercava i vicini con chiavi cartesiane `cx±1/cz±1`. Per un chunk sferico queste chiavi non rappresentano il vicino topologico sulle seam Cube-Sphere. Inoltre, anche sostituire il lookup con il puntatore al chunk corretto non basta: ai cambi di faccia il suo asse normale locale può passare da X a Z e il suo asse tangenziale può essere invertito. Il mesher legge invece il vicino cardinale secondo gli assi locali del chunk sorgente. Senza adattamento, il piano campionato non corrisponde alla seam.

La prova di caratterizzazione P02a aveva rilevato 70/72 vicini Tiny mancanti o errati nel percorso precedente. La correzione qui risolve indirizzo e orientamento prima di invocare il builder condiviso.

## 3. Modifiche P02c

| File | Intervento |
|---|---|
| `src/world/PlanetaryChunkNeighborhood.h` | Nuovo helper per validazione coordinate, risoluzione dei 4 vicini cardinali via `PlanetMath::ResolveNeighbor`, conversione storage tramite `PlanetGridCompatibility`, classificazione availability e orientamento di snapshot seam. |
| `src/world/GameWorld.cpp` | Il neighbor-preparation planetario usa il `PlanetChunkCoord` canonico, verifica identità e stato generato e prepara lo snapshot orientato per il mesher. La ricerca legacy non planetaria resta invariata. Per le seam crea direttamente lo snapshot del piano, evitando una copia integrale temporanea del chunk poi sovrascritta; i vicini same-face conservano una copia completa sicura per il job asincrono. |
| `src/world/PlanetaryVoxelE2ETests.cpp` | Estende il runner headless con risoluzioni per tutte le dimensioni, seam orientate, classificazione degli stati e verifica del culling interno; il runner Tiny misura i vicini consegnati e confronta i risultati mesh. Il file era già non tracciato nella baseline P01. |

Nessun cambiamento a `VoxelChunkMesher::Build`, Vertex ABI, block/material ID, UV, normali, winding, renderer, raycast o fisica.

### Contratto implementato

- Input cardinali del mesher: `(dx,dz)`.
- Conversione topologica: `ResolveNeighbor(source, dCol=dx, dRow=-dz)`, perché row canonica cresce in verso opposto alla chiave legacy z.
- PlanetID e layer devono essere preservati; coordinate fuori `[0,N-1]`, facce invalide e identità discordanti sono rifiutate.
- Chiave storage ricavata con l’adattatore P02b, non ricostruita da `Transform.position`.
- Stato `Available`: entità, componente, identità e `isGenerated` validi. `Pending`: il candidato esiste ma non è pronto; il meshing è rinviato e resta dirty per retry. `Missing`: nessun candidato storage, quindi la faccia resta aperta. `Invalid`: entità/componente/identità o orientamento incoerenti; il meshing viene rinviato, non mascherato come aria.
- Sulle seam si trova la direzione reciproca dal chunk target al source. Il suo asse identifica il piano di bordo target; il segno del prodotto scalare degli assi tangenziali decide se invertire l’indice tangenziale. Vengono copiati solo i voxel del piano che `VoxelChunkMesher` campiona.
- Le diagonali AO sono risolte soltanto quando rimangono nella stessa faccia. Non è definito un contratto canonico per i corner diagonali cross-face: è un limite residuo esplicito, distinto dai 4 vicini cardinali richiesti dal culling delle facce.

## 4. Build e test effettivi

Toolchain: MSBuild `18.10.1`, Visual Studio 18 2026, configurazione Debug, cache esistente `build_r3z7`. Le directory di output/intermedie erano nel sandbox. I warning `MSB8028` segnalano directory intermedie condivise dai progetti generati; la build è terminata con exit 0.

| Operazione | Risultato |
|---|---|
| Build incrementale `FairworldCore.vcxproj` dopo l’ultima modifica a `GameWorld.cpp` | PASS, exit 0; `GameWorld.cpp` ricompilato in `p02c-core-bin`. |
| Build Debug runner in copia temporanea isolata | PASS, exit 0. Il progetto copiato punta esplicitamente alla libreria `p02c-core-bin\FairworldCore.lib`; nessun file di progetto/CMake originale è stato cambiato. |
| Runner Tiny, run 1 | PASS, exit 0. |
| Runner Tiny, run 2 | PASS, exit 0. |
| P02a coordinate/topologia e P02b compatibility nel runner | PASS in entrambi i run finali. |
| `git diff --check` sui sorgenti tracciati modificati da P02c | PASS, exit 0. |

### Risultati topologici

Entrambi i run riportano:

- 55.824 risoluzioni coordinate valide sulle sei `PlanetSize`.
- 2.112 relazioni cross-face orientate; 2.112 reciproche.
- 33.792 campioni voxel seam (`2.112 × CHUNK_SIZE`).
- 2.112/2.112 facce sintetiche interne cullate; zero fallimenti.
- Tiny: 72/72 edge diretti risolti e consegnati al relativo slot del mesher; 0 mancanti o discordanti.
- Classificazione sintetica `Missing`, `Pending`, `Available`, `Invalid`: verificata.

### Risultati Tiny mesh

- 54 chunk candidati/ECS con identità PlanetChunkCoord coerente; 54/54 processati dal TerrainSolver.
- 504.877 voxel non-Air.
- 54 risultati mesh CPU associati ai chunk sorgente; 297.756 vertici e 99.252 triangoli.
- Indici/posizioni/UV/normali invalidi: 0; mismatch di winding: 0; mismatch materiale/associazione: 0; vertici Material ID 0: 0.
- Degenerazioni: 0.
- Input same-face-only: 178.112 triangoli; con i vicini cross-face corretti sono state rimosse 78.860 facce duplicate/interne. Questo è un confronto sintetico del runner, non il conteggio del vecchio algoritmo.
- Ricostruzione del lookup legacy piatto: 135.820 triangoli e hash `0x20c0d276200e0e20`, uguale al baseline registrato prima della patch.
- Mesh corretta: hash `0xaa8de4f86721f55c` in entrambi i run. La variazione rispetto al lookup legacy è conseguenza della sostituzione dei vicini errati con i piani seam reali; hash diversi sono attesi.

I log completi sono nel sandbox:

- `p02c-core-final3-build_20261105.log`
- `p02c-isolated-runner-final-build_20261105.log`
- `p02c-isolated-verified-run1_20261105.log`
- `p02c-isolated-verified-run2_20261105.log`

Un tentativo intermedio del runner con exit 1 è stato scartato come evidenza: il target generato linkava la vecchia `build_r3z7\Debug\FairworldCore.lib`, non il core isolato. Il mismatch P02b risultante non era un test valido della build appena compilata. Per i run finali è stata creata nel sandbox una copia temporanea del progetto runner con dipendenza assoluta alla libreria isolata; P02a/P02b sono quindi tornati PASS senza modificare il build tree o CMake.

## 5. Performance

Nel runner Release-candidate/debug diagnostico, per l’intero sweep di 55.824 coordinate: lookup topologico circa 9,5 ms. L’orientamento di 2.112 snapshot seam circa 43,1 ms e il meshing sintetico delle seam circa 631,6 ms nel run 1 (run 2: 9,7 ms / 43,4 ms / 610,8 ms). Sono misure di un singolo processo di test, influenzate da fixture e allocazioni, non benchmark di produzione.

Il lookup non scandisce globalmente i chunk: risolve coordinate e interroga direttamente ChunkManager con la chiave storage. Per una seam GameWorld conserva il solo piano richiesto dal mesher; la copia integrale precedente, che veniva subito sovrascritta, è stata rimossa. Il tempo effettivo end-to-end di `GameWorld::Update` con JobSystem concorrente non è stato misurato separatamente.

## 6. Regressioni e limiti

- La ricerca cartesiana legacy e i suoi chiamanti non planetari non sono stati cambiati; il runner conserva il test di compatibilità legacy.
- P02a/P02b, mapping radiale D1.2.19d.5, collision tests D1.2.19d.6 e Tiny CPU E2E sono inclusi nel runner finale e PASS in due esecuzioni.
- Il runner esercita la risoluzione e l’orientamento tramite gli helper condivisi e ricostruisce l’input del builder; il worker reale asincrono di `GameWorld::Update` è stato compilato ma non avviato dal runner. Quindi la consegna dal worker di produzione resta NOT VERIFIED end-to-end.
- MeshComponent GPU, upload DMA, draw, fence, ownership Vulkan e GUI: NOT VERIFIED e fuori perimetro.
- Nei runtime con più pianeti sovrapposti al singolo ChunkManager legacy, l’identità planetaria resta limitata dalla chiave di storage senza PlanetID; il codice rifiuta mismatch/deferisce, ma una migrazione del manager non fa parte di P02c.
- I test su tutte le dimensioni usano fixture sintetiche di topologia e seam; soltanto Tiny usa il fixture voxel completo.

## 7. Integrità e file

Hash critici P01 invariati:

| File | SHA-256 corrente |
|---|---|
| `PlanetRadialMapping.h/.cpp` | `6D958E4781D2678BAC6FAAC60232207AEFD961B41616549B049E0F41D0604F07` / `6061B9C589C38E6936D810D5EB1D67F84E88366ADD563D80D172DFA1318872BA` |
| `CubeSphereMapping.h/.cpp` | `F640151BD2A888AE620B0DCE7E2ADCEE8297E647F421068E0E2213FB7B4DB5A7` / `28C1494AB36D3DB93CA084879E901D154F295D47481460E33E99A4ACBE37F905` |
| `VoxelChunkMesher.cpp` | `3B529DABC04E6011A71A27693D4902592143191B0CE75908A6BB42ED9401EC02` |
| `PhysicsEngine.cpp`, `Systems.cpp`, `PlayState.cpp` | `52AC556FCA210E931151813CAA94537CF048C564E4174BEF82E8E99281CC86C6` / `FF4D5DF720A851D78E9D84BF3B934CE5239A40C1C06577A480C5DB4031138A1D` / `40F39376CAD012DFEB719638A4984B2AE745CF5FCC4137C2D59AE20AA1B2463B` |
| `BlockMakerState.cpp`, `TexturePacker.cpp`, `RenderManager.cpp` | `C30A8A39E30C710879ABCA08228D54845B40238E5DDE9793F50AB6467BF17053` / `4D7A3870FB06F18ED2ECA69CCA3E86011C2E145E857E8544007676529AB3F382` / `80496142C3BFA35C5F7A0AF6501BF2CEBFC69F8EDB87D63CBD14C8677AF4F8B2` |

File P01 che risultano diversi: `GameWorld.cpp` SHA-256 `14BE54EA3D96C4BE7D3613A996A6F4695EDFBE6D708A07FFA873B14246400AF4` (P01 `91DF781298C5224BCA7A4F83D013374DE40976B8060CCA6706799989D4E7345E`; file già dirty, contiene P02c oltre a modifiche precedenti) e `MapWorldGenerator.cpp` SHA-256 `9D2AAE33F6198268AD9ADDD26F6941119567BA57484A5B16CF56E16D85241300` (P01 `73B69E957F4D7AC3E9C956F578903DA8D8B5BEDCCBDBEF192B27C929A5B61027`; differenza preesistente P02b, non modificata in P02c).

## 8. Matrice esito e proposta

| Gate | Esito |
|---|---|
| Risoluzione cross-face cardinali, sei dimensioni | PASS CPU |
| Reciprocità e orientamento seam | PASS CPU |
| Tiny 72/72 e seam culling | PASS CPU |
| Chunk/materiali/UV/normali/winding/associazione | PASS CPU |
| Build Debug target core e runner | PASS |
| Ripetibilità mesh hash su due run | PASS |
| Worker async GameWorld reale | NOT VERIFIED |
| GUI, GPU upload/draw/fence | NOT VERIFIED |
| Dati originali | Fixture invariato; nessun dato originale scritto |

**Proposta: GO per P02d (Raycast & Slab Robustness)** come fase separata; P02c chiude il gate CPU delle relazioni cardinali e delle seam, ma non certifica il worker asincrono o il rendering GPU. La verifica end-to-end del worker di produzione può essere aggiunta come test indipendente, senza allargare la patch a P02d.
