# FAIRWORLD P01 — Shared Files & Change-Serialization Matrix

This matrix maps likely write conflicts. “Do not modify concurrently” means separate patches should be reviewed and validated serially unless a shared interface is agreed first. It does not prohibit read-only parallel audit.

| Shared file / contract | I1 Geometry & Collision | I2 Gravity & Movement | I3 Block Maker | I4 Atlas/PBR | I5 Neural Queries | Change rule / gate |
|---|---|---|---|---|---|---|
| `src/core/utils/PlanetMath.h` (`PlanetSize`, `PlanetChunkCoord`) | Primary | Reads coordinate/size | May serialize/place chunks | Indirect mesh layer size | Query scene coordinates | P02 owns contract; avoid concurrent edits with generator, physics or persistence. |
| `src/world/CubeSphereMapping.h/.cpp` | Primary face/UV/direction mapping | Up/cross-face movement consumers | Planet editor selection | Planet UV producer | Query face mapping | One owner per patch; validate all six faces and round-trip before I2 changes. |
| `src/world/PlanetGridCompatibility.h`, `PlanetGridCompatibility` callsites | Legacy↔canonical row | Indirect | Map authoring | Indirect | Indirect | Conversion remains single point; serialize no coordinate migration without approval. |
| `src/world/PlanetRadialMapping.h/.cpp` | Primary shared radius/cell contract | Spawn and collision | Planet placement | LOD/planet UV position | Query distance | Do not edit concurrently with `MapWorldGenerator`, `GameWorld` or `PhysicsEngine`; zero-offset regression gate. |
| `src/world/MapWorldGenerator.h/.cpp` | Chunk/voxel producer | Terrain availability/collision | Document consumption | Material IDs in generated voxels | Queryable scene | Serial with PlanetChunkCoord/radial changes and GameWorld API changes. |
| `src/world/GameWorld.h/.cpp` | Chunk identity, voxel query, mesher/async upload | Physics query and spawn | Runtime destination | mesh/material publication | Query target source | Highest shared hotspot; no parallel I1/I2/I3 implementation edits. CPU E2E after each patch. |
| `src/systems/PhysicsEngine.h/.cpp` | collision sampling and radial world query | Primary gravity/integration/collision | — | — | possible physics ray query | I1 collision changes and I2 movement changes must be serially integrated; keep d6 probe baseline. |
| `src/systems/Systems.h/.cpp` | — | input→movement→physics→camera order | — | — | input/query trigger | I2 sole writer during movement/input work. |
| `src/states/PlayState.h/.cpp` | spawn/planet fixture | player/input/camera runtime | consumes runtime data | render/material runtime | eventual query UI | Do not parallelize I2 and I3/I4 integration patches here. |
| `src/states/BlockMakerState.h/.cpp` | map/spawn authoring interactions | — | Primary preview, persistence, authoring | dynamic texture/material updates | possible query consumer | I3 owns this file; I4 changes to runtime material loading must avoid concurrent edits. |
| `src/blocks/BlockRegistry.*`, `MaterialRegistry.*`, `ScientificMaterialRegistry.*` | terrain block producer | collider block identity | authoring definitions | visual layer source | hit classification | First define Block ID / visual ID / scientific ID in P07; no simultaneous schema/loader changes. |
| `src/components/ForgeComponents.h`, `src/core/utils/World.h` (Vertex ABI) | LOD/voxel producer | — | mesh producer | shader vertex input | possible ray/mesh mapping | Single contract owner; preserve stride, offsets and field ABI; inventory all producers before edits. |
| `src/world/VoxelChunkMesher.*`, `VoxelFaceUV.h`, `src/world/SphericalLOD.cpp` | planetary/voxel mesh | collision-adjacent surface | authored mesh route | UV/material producer | scene geometry | Separate I1 geometry from I4 vertex/material edits; share tests, not concurrent implementation. |
| `src/render/TexturePacker.*`, `TextureCacheContractTests.cpp` | — | — | material authoring outputs | primary cache/atlas | — | P08 cache/data contract, then P09 renderer; do not change cache format with shader/ID mapping simultaneously. |
| `src/render/RenderManager.h/.cpp`, `forge.vert`, `forge.frag` | LOD/mesh submission | visible player | preview/runtime renderer | descriptor/shader/PBR | future GPU queries | P09 single-threaded integration; no concurrent vertex ABI or atlas mapping patch. |
| `src/render/VulkanDmaManager.*`, `VramSlabAllocator.*`, `VulkanMemory.*` | mesh upload lifetime | indirectly player mesh | preview mesh upload | texture/mesh upload | future GPU query buffers | GPU infrastructure shared across I1/I3/I4; only modify with specific Vulkan evidence and GPU acceptance case. |
| `src/systems/RaycastSystem.*`, `SharedContext.h` | voxel/sphere query API | physics/editor query consumer | editor interaction | material hit feedback | primary candidate | I5 research starts with API/design only; avoid coupling to gameplay until use case specified. |
| `CMakeLists.txt`, `CMakePresets.json`, `build_r3z7/` | test registration/build | same | same | same | same | Build configuration changes serialized. Existing cache is VS 18 2026, preset says Ninja; do not reconfigure during P01/P02. |

## Parallel work allowed

- Read-only P02 coordinate inventory, P05 authoring data-flow audit and P12 I5 feasibility study can run independently if they produce documents only.
- P03 collision test design and P04 input/camera audit may be read-only parallel; edits touching `PhysicsEngine`/`Systems` must be serialized.
- P05 authoring and P07 material identity can audit in parallel, but implementation is gated on an agreed ID contract.
- P08 cache logic and P04 player math are separable; P09 GPU integration depends on P07/P08 and vertex contract.
- P10 fixture integration follows P02/P05/P07; P11 GUI follows P04/P06/P09/P10; P13 final gate follows all required workstreams.

## P02–P13 verification gate matrix

| Phase | Minimum verification gate | Must remain distinct |
|---|---|---|
| P02 Coordinate contract | CPU deterministic six-face, six-size, row bounds, round trips, legacy single-conversion, chunk identity and radial mesh/collision equality; run named binaries from sandbox | No GUI/GPU assertion. |
| P03 Collision | CPU sweep/volume tests, Solid/Air/Pending/OOB classification, uneven terrain, edge/corner, drop and actual landing, reproducible trace; benchmark d6 probe cost | Grounded state vs no penetration; not a GUI result. |
| P04 Player/input | CPU frame orthonormality, gravity, input-vector normalization, update order and pending-spawn tests; later authorized GUI for real WASD/Space/mouse/focus/capture | CPU action model vs physical input and rendered camera. |
| P05 Block Maker data | Isolated fixture author/save/reopen, source IDs/hashes, path audit, no original writes | Preview vs persisted artifact. |
| P06 Runtime staging | Load P05 artifact into runtime world; verify template/instance/chunk/material provenance and counts | ECS/CPU mesh vs GPU MeshComponent/draw. |
| P07 Material IDs | Exhaustive supported-ID mapping Block→Visual Layer and separate Scientific ID; Air/fallback/unknown-ID tests | Registry contract vs shader output. |
| P08 Atlas/PBR CPU | Cache invalidation, source-content fingerprints, missing/appearing texture, deterministic layer/fallback, path/cwd isolation | Packed bytes vs visual sampling. |
| P09 GPU materials | Real GPU descriptor/vertex/texture upload, draw completion, validation messages, pixel/screenshot material cases | Upload/mesh publication vs draw and fence completion. |
| P10 CPU E2E | Fixture MapDocument→generator→solver→ECS→shared mesher; 54 chunk identity (Tiny), non-Air, material/UV/normals/index/winding/source association | CPU mesh only; no fabricated MeshComponent/GPU PASS. |
| P11 GUI sandbox | Authorized full authoring→Planet Mapper→PlayState run, real input, visible materials/terrain, shutdown, logs, original hash pre/post | Every unobserved control remains NOT VERIFIED. |
| P12 I5 feasibility | Approved use-case definition, CPU reference, target Vulkan/device capabilities, latency/accuracy/safety criteria | Research decision vs implemented feature. |
| P13 acceptance | Re-run named build/test matrix from fixed baseline; all open issues classified; GUI/GPU evidence where required; final review | Readiness recommendation is not automatic FROZEN. |

