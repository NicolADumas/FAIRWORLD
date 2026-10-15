# FAIRWORLD — FG-01R TOPOLOGY ARCHITECTURE FREEZE SPECIFICATION

- **Stato**: **FROZEN**
- **Data di Freeze**: 2026-10-15
- **Gate di Riferimento**: FG-01R (Micro-Gates 1, 2A, 2B, 2C, 2D, 3A, 3B, 4, 4B)
- **Approvazione**: Final Authorization MG-4B PASS

---

## 1. AMBITO AUTORIZZATO E CONGELATO

Il freeze architetturale FG-01R stabilisce e congela la pipeline topologica autorevole tra la definizione analitica delle forme, la compilazione CPU, la struttura dati DCEL (Half-Edge) e l'estrazione delle isole UV:

$$\text{ShapeDefinition} \longrightarrow \text{GeometryCompiler} \longrightarrow \text{CompiledTopology} \longrightarrow \text{MeshBridgeService} \longrightarrow \text{EditableMesh} \longrightarrow \text{SeamGraph} \longrightarrow \text{UVIslandExtractor}$$

### Primitive Migrate (Ambito Autorevole Congelato)
1. **Cube** (Box standard 1.0)
2. **Cuboid** (Box parametrico con dimensioni arbitrariamente scalate)
3. **Parallelepiped** (Box deformato con assi non ortogonali)
4. **Cylinder** (Cilindro con anelli condivisi e connettività periodica continua al seam)
5. **Cone** (Cono con singolo vertice apicale topologico e anello di base condiviso)
6. **ConeFrustum** (Tronco di cono con basi superiore e inferiore connesse al mantello)
7. **Sphere** (Sfera continua 2-manifold con poli Nord e Sud unici e chiusura periodica longitudinale)

---

## 2. GLI OTTO INVARIANTI CONGELATI

### Invariante 1 — CompiledTopology come Sorgente CPU Autorevole
`GeometryCompileResult::topology` (`CompiledTopology`) è l'unica rappresentazione canonica e autorevole per la modellazione, la scultura, la marcatura delle cuciture e la parametrizzazione UV. Non è consentito bypassare `CompiledTopology` né ricostruire topologie a partire dal buffer di vertici unrolled per le primitive migrate.

### Invariante 2 — Identità Geometrica Distinta dai Vertici GPU Unrolled
La connettività topologica è definita da indici univoci su `CompiledTopology::geometricPositions`.
- È rigorosamente vietato il welding euristico spaziale basato sulla distanza per coordinate (KD-Tree / spatial distance threshold).
- Ogni voce in `geometricPositions` corrisponde a esattamente un `VertexID` in `EditableMesh`.
- Il buffer di rendering `MeshComponent::vertices` rimane una rappresentazione derivata ottimizzata per la GPU, unrolled per rispettare le discontinuità hardware di normali e UV.

### Invariante 3 — Attributi d'Angolo Per-Corner (UV, Normali, Colori)
Tutti gli attributi discontinui sono memorizzati a livello di corner incidente (`CornerAttributes`) su ciascun half-edge (`MeshHalfEdge::corner`):
- `uv`: coordinate tessiturali per-corner.
- `normal`: normale di shading per-corner.
- `color`: colore per-corner.
- `EditableMesh::ComputeVertexNormals()` calcola esclusivamente la normale d'area perimetrale sui vertici (`MeshVertex::normal`) e sulle facce (`MeshFace::faceNormal`), senza sovrascrivere o alterare `MeshHalfEdge::corner.normal`.

### Invariante 4 — Ortogonalità e Indipendenza di SurfaceID e MaterialID
- `SurfaceID` rappresenta l'identità semantica strutturale della superficie geometrica (es. mantello vs base).
- `MaterialID` rappresenta il descrittore PBR del materiale assegnato.
- I due identificatori sono memorizzati in campi distinti a 32-bit (`CompiledFace::surfaceId`, `CompiledFace::materialId`, `MeshFace::surfaceId`, `MeshFace::materialId`) e possono essere modificati in modo indipendente senza causare accoppiamenti o mutazioni collaterali.

### Invariante 5 — DCEL con Gemellaggio Twin Reciproco Completo
Per tutte le forme migrate, la connettività DCEL soddisfa rigorosamente:
- Manifold 2-dimensionale chiuso orientabile.
- Caratteristica di Eulero $\chi = V - E + F = 2$.
- Ogni half-edge interno possiede un gemello reciproco univoco: `he->twin == twinHe->id` e `twinHe->twin == he->id`.
- Numero di spigoli di confine aperti: `boundaryEdgeCount == 0`.

### Invariante 6 — Indipendenza tra Seam UV e Connettività Geometrica
- Le cuciture UV generate o marcate in `SeamGraph` non alterano né lacerano la connettività topologica di `EditableMesh`.
- Uno spigolo marcato come seam (`seamGraph.IsSeam(eId) == true`) conserva il proprio gemello reciproco e i propri vertici condivisi nella DCEL.
- `UVIslandExtractor` sfrutta i seam per segmentare il flood-fill BFS delle isole UV senza richiedere duplicazioni o lacerazioni della geometria sottostante.

### Invariante 7 — Conversione Atomica e Diagnostica Programmatica
- `MeshBridgeService::CreateMeshFromShape(shapeDef, outError)` garantisce l'atomicità: in caso di compilazione fallita, topologia non valida o parametri corrotti, la mesh viene azzerata (`mesh.IsEmpty() == true`) e nessuna mesh parzialmente costruita viene mai pubblicata.
- Non viene eseguita alcuna sostituzione con geometrie fittizie (nessun fallback a cubo 1.0f).
- La causa puntuale dell'errore è accessibile programmaticamente tramite il puntatore opzionale `std::string* outError` (con messaggio non vuoto in caso di fallimento e stringa vuota su successo).

### Invariante 8 — Stabilità ABI GPU fw::Vertex a 64 Byte
Il vertice di rendering Vulkan `fw::Vertex` è rigorosamente protetto da asserzioni statiche a tempo di compilazione:
```cpp
static_assert(sizeof(Vertex) == 64, "fw::Vertex must be exactly 64 bytes");
static_assert(offsetof(Vertex, position) == 0);
static_assert(offsetof(Vertex, color) == 12);
static_assert(offsetof(Vertex, roughMetal) == 28);
static_assert(offsetof(Vertex, materialID) == 36);
static_assert(offsetof(Vertex, normal) == 40);
static_assert(offsetof(Vertex, ao) == 52);
static_assert(offsetof(Vertex, light) == 56);
static_assert(offsetof(Vertex, emissive) == 60);
```
Nessuna modifica alla memoria o agli shader Vulkan è autorizzata.

---

## 3. BASELINE CONSOLIDATO DEI 151 TEST

L'intera suite contrattuale consolidata è validata con esito **151 / 151 PASS, 0 FAIL, 0 SKIPPED (Exit code 0)**:

| Suite ID | Descrizione Suite | Test Passati |
|---|---|:---:|
| **UVP** | Primitive & Parametric UV Contract Tests | 15 / 15 |
| **UVG** | General Mesh Unwrapping Contract Tests | 18 / 18 |
| **UVK** | Universal Atlas Packing Contract Tests | 18 / 18 |
| **UVE** | 2D UV Editor Backend Contract Tests | 18 / 18 |
| **UVR** | Texture Reprojection Contract Tests | 20 / 20 |
| **UVI** | Factory End-to-End Integration Contract Tests | 10 / 10 |
| **TOP** | Compiled Topology Analytic Emission Tests | 36 / 36 |
| **MBR** | MeshBridge Authoritative & UV Island Extraction Tests | 16 / 16 |
| **TOTALE**| **Test Runner Headless Consolidato** | **151 / 151** |

---

## 4. INVENTARIO DELLE PRIMITIVE ANCORA LEGACY

Le seguenti forme procedurali secondarie **non sono ancora migrate** a `CompiledTopology`:
- `ShapeType::Pyramid`
- `ShapeType::Capsule`
- `ShapeType::Torus`
- `ShapeType::Prism`
- `ShapeType::Wedge`
- `ShapeType::HollowCylinder`
- `ShapeType::SlicedCylinder`
- `ShapeType::SphericalZone`
- `ShapeType::SphericalCap`
- `ShapeType::BoredSphere`

### Regole Operative per il Percorso Legacy:
1. Quando compilate, `res.topology` risulta `std::nullopt`.
2. `MeshBridgeService` emette un messaggio diagnostico esplicito:
   `[MeshBridgeService] DIAGNOSTIC: CompiledTopology absent for ShapeType ... Using legacy unrolled fallback.`
3. La conversione viene eseguita tramite il percorso unrolled storico garantendo il funzionamento e l'assenza di crash.
4. Non vi è alcun fallback fittizio a `CreateCube(1.0f)`.
5. La loro migrazione analitica è pianificata per un ciclo successivo e richiederà un nuovo gate autorizzato.

---

## 5. CLAUSOLA DI CONGELAMENTO E VINCOLI

1. **Immutabilità dei Contratti**: È fatto espresso divieto di modificare le firme pubbliche, le strutture dati e il comportamento delle classi `CompiledTopology`, `MeshBridgeService`, `EditableMesh` e `SeamGraph` relative a FG-01R.
2. **Procedura di Scongelamento**: Qualsiasi modifica architetturale richiede l'apertura formale di un micro-gate di revisione con approvazione esplicita.
3. **Divieto di Modifiche Vulkan / Rendering**: La pipeline Vulkan, i layout di memoria e gli shader `.spv` rimangono intatti e isolati dalla topologia CPU.
