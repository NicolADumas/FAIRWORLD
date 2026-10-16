# FAIRWORLD — ROADMAP TECNICA

Ultimo aggiornamento: 2026-10-15  
Stato: **ACTIVE**

---

## MILESTONES & ARCHITECTURE GATES

### 1. BLOCK MAKER & MESH FOUNDATION

| Gate ID | Nome / Ambito | Stato | Data Freeze / Validazione | Note |
|---|---|:---:|:---:|---|
| **BM-S1 .. S8** | EditableMesh DCEL, Selezioni, Scultura, Undo/Redo | **COMPLETED** | Precedente | Struttura DCEL Half-Edge CPU |
| **BM-S9** | MeshBridgeService & Factory Integration | **COMPLETED** | Precedente | Connessione Block Maker / Sculpt |
| **BM-UV1 .. UV7**| UV Mapping, LSCM, Atlas Packer, Reprojection | **COMPLETED** | Precedente | Suite UV consolidata (99 test) |
| **FG-01** | Geometry $\to$ EditableMesh Topology Audit | **COMPLETED** | 2026-10-15 | Rilevata perdita identità topologica |
| **FG-01R** | **Authoritative CompiledTopology Architecture** | **FROZEN** | **2026-10-15** | **151/151 Test PASS (Exit code 0)** |
| **BM-F2A.1**| PainterStationAdapter Composite Albedo Integration | **COMPLETED** | 2026-10-15 | 10/10 PASS (PNT-01..PNT-10) |
| **BM-F2B** | Texture Baking Service & Atomic PNG Export | **COMPLETED** | 2026-10-15 | 10/10 PASS (TXB-01..TXB-10) |
| **BM-F2C** | Asset Packaging & Runtime Registry Binding | **COMPLETED** | 2026-10-15 | 10/10 PASS (PKG-01..PKG-10) |
| **BM-F2C.1**| Transaction Integrity & Fault Injection Audit | **COMPLETED** | 2026-10-15 | 10/10 PASS (TRX-01..TRX-10) |
| **BM-F3**  | Sculpted Mesh Runtime Bridge (DCEL $\to$ Runtime)| **COMPLETED** | 2026-10-15 | 10/10 PASS (SBR-01..SBR-10) |
| **BM-F4**  | Asset Integrity & End-to-End Round-Trip | **COMPLETED** | 2026-10-15 | 10/10 PASS (INT-01..INT-10) |
| **BM-F4.1**| **Asset Integrity Hardening Gate (SHA-256 / PNG Cache)** | **COMPLETED** | 2026-10-15 | Contratti SEC-01..SEC-10 implementati |
| **BM-F4.2**| **Digest Verification, Trust Policy & Zero-Work Safety** | **COMPLETED** | 2026-10-16 | 10/10 PASS (DGV-01..DGV-10); Trust Policy (Strict/AllowLegacy), Zero-Work Safety |
| **BM-F4.3**| **Transactional Asset Publication & Crash Recovery** | **COMPLETED** | 2026-10-16 | 10/10 PASS (CRH-01..CRH-10); Persistent Staging, Journaled Commit, Idempotent Recovery |
| **BM-F4.3.1**| **Crash Recovery Hardening & Commit Semantics** | **COMPLETED** | 2026-10-16 | 8/8 PASS (CRH-H01..H08); Commit Authority, Safe Recovery, Win32 Atomic Replace |
| **BM-F4.4**| **Asset Dependency Graph & Referential Integrity** | **COMPLETED** | 2026-10-16 | 10/10 PASS (DEP-01..DEP-10); Cycle Detection, Topological Order, Zero-Work Invalidation |
| **BM-F4.5**| **Asset Schema Migration & Backward Compatibility** | **COMPLETED** | 2026-10-16 | 10/10 PASS (MIG-01..MIG-10); Deterministic V1->V2 Migration, Lossless Conservation, Strict Verification |

---

## DETTAGLIO STATO FG-01R — ARCHITECTURE FREEZE

- **Status**: **FROZEN** (Autorizzato da Revisione Finale MG-4B)
- **Documento di Riferimento**: [`docs/FG01R_TOPOLOGY_FREEZE.md`](docs/FG01R_TOPOLOGY_FREEZE.md)
- **Ambito Congelato**: Pipeline topologica autorevole per le 7 primitive migrate:
  - `ShapeType::Cube`
  - `ShapeType::Cuboid`
  - `ShapeType::Parallelepiped`
  - `ShapeType::Cylinder`
  - `ShapeType::Cone`
  - `ShapeType::ConeFrustum`
  - `ShapeType::Sphere`

### Invarianti Architetturali Congelati
1. `CompiledTopology` è la sorgente CPU autorevole per la topologia geometrica.
2. Identità geometrica distinta dai vertici GPU unrolled (no spatial coordinate welding).
3. Attributi d'angolo (`CornerAttributes`: UV, normali, colori) preservati per-corner su half-edge.
4. `SurfaceID` ortogonale e indipendente da `MaterialID`.
5. DCEL con half-edge gemelli (`twin`) reciproci (2-manifold chiuse con $\chi = 2$).
6. Seam UV in `SeamGraph` indipendenti dai confini geometrici (lo spigolo rimane continuo nella DCEL).
7. Conversione atomica (nessuna mesh parziale restituita) e diagnostica programmatica via `std::string* outError`.
8. ABI GPU `fw::Vertex` rigidamente invariata a 64 byte (`static_assert(sizeof(Vertex) == 64)`).

### Baseline Test Suite (151 Test Consolidati)
- **UVP (Primitive & Parametric UV)**: 15 / 15 PASS
- **UVG (General Mesh Unwrapping)**: 18 / 18 PASS
- **UVK (Universal Atlas Packing)**: 18 / 18 PASS
- **UVE (2D UV Editor Backend)**: 18 / 18 PASS
- **UVR (Texture Reprojection)**: 20 / 20 PASS
- **UVI (Factory End-to-End Integration)**: 10 / 10 PASS
- **TOP (Compiled Topology Validation)**: 36 / 36 PASS
- **MBR (MeshBridge Authoritative & UV Island Extraction)**: 16 / 16 PASS
- **Totale**: **151 / 151 PASS**, 0 FAIL, 0 SKIPPED. Exit code: **0**.

### Primitive Non Ancora Migrate (Stato Legacy Controllato)
- `Pyramid`, `Capsule`, `Torus`, `Prism`, `Wedge`, `HollowCylinder`, `SlicedCylinder`, `SphericalZone`, `SphericalCap`, `BoredSphere`.
- Gestite tramite percorso legacy diagnosticato (`[MeshBridgeService] DIAGNOSTIC: CompiledTopology absent...`) senza fallback a cubo e senza regressioni.
- La loro migrazione è pianificata per milestone future post-freeze.

---

## REGOLE DI GOVERNANCE POST-FREEZE

1. È severamente vietato modificare i contratti, le strutture o le firme pubbliche congelate senza l'apertura formale di un nuovo micro-gate di revisione.
2. Nessun commit su `GeometryCompiler`, `MeshBridgeService` o `EditableMesh` può alterare il baseline dei 151 test superati.
3. Nessun refactoring o modifica ABI Vulkan/GPU è consentita durante lo stato congelato.
