# FAIRWORLD P02a — Geometry & Raycast Characterization Report

**Mode:** CPU/headless characterization; no production fixes applied.
**Repository:** `D:\FAIRWORLD\FAIRWORLD`
**Fixture:** `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`
**Runner working directory:** `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`
**Fixture SHA-256 recorded after the run:** `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`

## 1. Safety gate and scope

| Check | Result |
|---|---|
| HEAD vs P01 | **PASS** — unchanged at `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`. |
| P01 critical file hashes | **PASS** — 18/18 match the P01 SHA-256 table. |
| Status inventory | P01 comparison had 424 paths; final inventory is 429, consisting of those paths plus the three P01 reports, `FAIRWORLD_P02_COORDINATE_CONTRACT_AUDIT.md`, and this report. The P02a test additions are inside the already-untracked E2E test source, so they do not add another Git status path. |
| Production source | **PASS** — no production source or CMake file was edited. The only source change is additive diagnostic code in the pre-existing untracked `src/world/PlanetaryVoxelE2ETests.cpp`. |
| Fixture | Read by the test runner. No fixture content was changed by the runner; its post-run SHA is recorded above. A pre-run fixture hash was not captured. |
| GUI/GPU | **NOT RUN** — this task was CPU/headless only. |

The test target was built with the existing `build_r3z7` Visual Studio project; no CMake configure or cache regeneration was requested. **The existing generated Debug test executable and its object file were rebuilt in place** at `bin\Debug\FairworldPlanetaryVoxelE2ETests.exe` and `build_r3z7\FairworldPlanetaryVoxelE2ETests.dir\Debug\PlanetaryVoxelE2ETests.obj`. They are generated build artifacts, but they existed before this run; this is recorded as an overwrite of generated artifacts. No original assets or save files were intentionally used as output destinations. The E2E logs were created as new files under the sandbox runtime.

## 2. Commands and exit codes

Build command, using the existing generated project and not building project references:

```powershell
& 'D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe' `
  'build_r3z7\FairworldPlanetaryVoxelE2ETests.vcxproj' /t:Build `
  /p:Configuration=Debug /p:Platform=x64 /p:BuildProjectReferences=false /m:1 /v:minimal
```

**MSBuild exit code: 0.** The runner was executed from the sandbox runtime with the absolute fixture path:

```powershell
& 'D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldPlanetaryVoxelE2ETests.exe' --fixture `
  'D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json'
```

Final run **exit code: 0**. Full stdout/stderr is retained at [P02A_E2E_20261105_03.log](D:/FAIRWORLD/D1_2_19D2_SANDBOX/runtime/P02A_E2E_20261105_03.log). Earlier characterization runs also exited 0; run 03 contains the final diagnostics. The process exit code means the existing Tiny E2E checks passed; it does **not** mean every newly observed P02a behavior is correct. Characterization outputs that contradict their expected contract are marked FAIL below, even though the diagnostic harness itself did not abort.

`git diff --check` returned **2** on pre-existing tracked changes in `src/states/BlockMakerState.cpp` (15 trailing-whitespace lines: 4652–4658, 4668–4669, 11563, 11570–11571, 11806, 11828, 11830). These lines were not touched. A direct trailing-whitespace scan of the modified E2E test source found **0** lines. No whitespace cleanup was performed.

## 3. Coordinate characterization

Source under test: `CubeSphereMapping::{GridToUV, FaceUVToDirection, DirectionToFaceUV, UVToGrid}` and `PlanetMath::{GetFaceResolution, GetEditorCanvasExtents, ResolveNeighbor}`. The P02a diagnostics are in `src/world/PlanetaryVoxelE2ETests.cpp:1931–2000`.

| Input / expected | Observed | Result |
|---|---|---|
| All six faces × all six PlanetSize values; round trip cell-center UV → direction → face/UV → direction. Expected same face and direction error ≤ 2e-5. | 13,956 samples; maximum direction error `1.74414e-07`. | **PASS** |
| `N={3,5,7,11,21,41}`; verify `2*GetEditorCanvasExtents+1=N`. | 6/6. | **PASS** |
| All logical `(face,col,row)` keys across those dimensions; legacy reflection `N-1-row` then inverse. | 13,956 unique logical keys; all reflected rows returned to the original row and stayed in `[0,N-1]`. | **PASS** |
| Each UV grid boundary `k/N`, both axes, endpoints, and representable `nextafter` neighbors; repeat each conversion to check deterministic in-range indices. | 564 samples deterministic and in range. For the represented exact internal boundaries in this sample set, 0 were assigned to the preceding cell. | **PASS** for determinism/range; numerical tie behavior is characterized, not changed. |

The existing E2E also emitted 72 Cube-Sphere edge samples and 24 corner samples with maximum direction round-trip error 0. This verifies direction projection samples, but **does not** verify cross-face voxel-neighbor topology at corners; `ResolveNeighbor` accepts cardinal one-step offsets only.

## 4. Cross-face topology and mesher input

Source: `PlanetMath::ResolveNeighbor` in `src/core/utils/PlanetMath.h:170+`; production neighbor gathering in `src/world/GameWorld.cpp:573`; characterization reproduces the same net-key lookup and checks the actual neighbor pointers passed to `VoxelChunkMesher::Build` in `src/world/PlanetaryVoxelE2ETests.cpp:2619+`.

| Input / expected | Observed | Result |
|---|---|---|
| Every oriented edge position for all six faces and all six resolutions; neighbor should be in range and stepping back from that neighbor should return the source cell. | 2,112 directed edge positions; 2,112 reciprocal. | **PASS** for `ResolveNeighbor` reciprocity. |
| Tiny generated fixture: compare each of 72 directed face-edge neighbor relationships with the neighbor pointers supplied by the current 3×3 legacy global `(cx±1,cz±1)` lookup. Expected all topological neighbors present. | 72 expected; 2 correct pointers delivered; 70 absent or mismatched. | **FAIL** — the mesher input is predominantly a 2D storage-net neighborhood, not the Cube-Sphere topology. |
| Corner topology | No diagonal/corner-neighbor contract exists in `ResolveNeighbor`; no corner topology implementation tested. | **NOT RUN** |

This is an observed data-delivery mismatch, not a claim that all resulting seam geometry is visibly defective. No mesher or generation code was changed.

## 5. Slab, DDA, and transform characterization

Relevant production callsites: `RaycastSystem::IntersectAABB` at `src/systems/RaycastSystem.cpp:14`, called by `CastVoxel` at `:55+`; DDA starts around `:93+`. The helper is private, so the tests exercised it through the public voxel cast using an isolated flat chunk with one solid voxel. Values from the final run are in its `[P02A][OBSERVED]` line.

| Input / expected | Observed | Result |
|---|---|---|
| Axis ray with two zero direction components through a single voxel; expected hit at surface distance 4.5. | Hit, distance 4.5. | **PASS** for this axial case. |
| Ray parallel to the X slab and originating on the inclusive AABB boundary `x=-0.5`. | Hit. | **PASS** for the tested boundary case. |
| Origin inside solid voxel. | Hit at the starting location. | **PASS** for presence of a hit; exact distance is 0 by the current DDA. |
| Origin outside at `z=30`, traveling toward a solid voxel at `z=15`; expected first surface distance 14.5. | Hit reported at distance 0. | **FAIL** — DDA starts from a clamped boundary cell but reports a distance relative to that cell rather than the original ray entry. |
| Ray lies exactly on the `x=0.5` voxel boundary, with voxel 0 solid and voxel 1 Air. Under the tested half-open grid convention it selects voxel 1. | Miss. | **PASS** for this deterministic half-open ownership case; other edge/corner tie combinations remain untested. |
| NaN or infinite direction component. Expected invalid query rejected, no hit. | Both produced `hit=true`. | **FAIL** — invalid ray inputs can be reported as voxel hits. |
| Child chunk translated by a parent to world X=1000; ray intersects the transformed chunk. | No hit. | **FAIL** — `CastVoxel` broadphase compares world ray origin against the child’s local `transform.location` before computing its parent-aware global matrix. |
| Uniform scale `(2,2,2)` and non-uniform scale `(1,1,2)`; ray from world Z=10 toward a scaled voxel whose top is world Z=1, expected distance 9. | Both report distance 4.5. | **FAIL** — local normalized DDA distance is returned as world distance without scale correction. |

No correction was attempted. The exact behavior of all slab parallel/boundary combinations, rotated chunks, and AABB corner tangencies remains **NOT RUN**.

## 6. Planetary query consistency

Relevant code: `GameWorld::QueryBlockCartesian` at `src/world/GameWorld.cpp:887+`, which calls `MapWorldGenerator::WorldToVoxelCoord` at `:904`; `WorldToVoxelCoord` is at `src/world/MapWorldGenerator.cpp:351`; `GetChunkCoordFromPosition` is at `:396`; `RaycastSystem::CastVoxel` uses its own local AABB and DDA path.

| Input / expected | Observed | Result |
|---|---|---|
| 54 Tiny face-cell centers at radius `PlanetRadius+18`; compare chunk key from `WorldToVoxelCoord` output with `GetChunkCoordFromPosition`. | Agreement 36/54; disagreement 18/54. | **FAIL** — duplicate coordinate conversion does not agree at these samples. The latter function has no internal production caller found in the P02 audit, so runtime impact outside external/API users is unverified. |
| Six center directions `+Z,-Z,+X,-X,+Y,-Y`; query a point just under the reference radius and cast a radial ray from outside. Expected query and raycast both locate solid terrain. | `QueryBlockCartesian`: solid 6/6. `CastVoxel`: hit 1/6, only +Y; that hit reports distance 0 and voxel `(112,74,80)`. | **FAIL** — the two query paths are not geometrically consistent on this spherical fixture. |

The center-ray result supports the static finding that `CastVoxel` uses local axis-aligned chunk bounds rather than the spherical world-to-voxel mapping. It does not alone identify every reason for the five misses.

## 7. CPU performance baseline

Final-run measurements used `std::chrono::steady_clock`, Debug build, 1,000 iterations for each listed workload, one Tiny fixture, and one machine/run. These are characterization numbers, not stable performance thresholds.

| Workload | Observed | Scope/limit |
|---|---:|---|
| `CastVoxel` broadphase with 54 chunks all rejected by distance | 89,834 ns/query average | Includes the 54-chunk loop and distance filter; excludes slab/DDA for rejected chunks. |
| `GameWorld::QueryBlockCartesian` at a known solid sample | 11,706 ns/query average | Includes world-to-voxel conversion and chunk/block lookup. |
| `PhysicsEngine::StepSimulation` at fixed position, `dt=0`, flying body | 753,710 ns/step average | Includes two block reads, physics setup, and the spherical collision path; it is **not** an isolated per-probe timing. The spherical path constructs 4×17=68 probe offsets per full `isCylinderBlocked` scan, but it may return early on a blocking status. |
| Isolated one-chunk `CastVoxel` scenario with DDA traversal | 49,332,300 ns / 4,000 casts = 12,333 ns/cast average | Includes candidate scan, broadphase, inverse transform, slab, and DDA; pure DDA time is not separately exposed. |

Pure DDA timing and per-probe `QueryBlockCartesian` timing are **NOT RUN** because the current APIs expose neither stage independently. No API or production instrumentation was added. Timing varies substantially between repeated runs, so compare only in controlled future benchmarks.

## 8. Result matrix and cause assessment

| Area | Result | Evidence-based conclusion |
|---|---|---|
| Face/chart/direction centers across six sizes | **PASS** | 13,956 round trips, error ≤ `1.74414e-07`. |
| UV boundaries and row reflection | **PASS** | Deterministic in-range conversions and valid involutive legacy-row reflection in tested samples. |
| Cross-face table reciprocity | **PASS** | All 2,112 edge positions reciprocal. Physical seam continuity of voxel data is not thereby proven. |
| Neighbor pointers supplied to mesher | **FAIL** | 70/72 directed face-edge neighbor pointers absent/mismatched under the current net lookup. |
| Ray slab/DDA robustness | **FAIL** | Outside-origin distance is zero; invalid NaN/Inf rays hit; parent translation misses; scaling distances are local. |
| Planetary query agreement | **FAIL** | 18/54 chunk conversion mismatches and CastVoxel hit 1/6 where QueryBlock is solid 6/6. |
| Tiny CPU fixture baseline | **PASS** | Existing runner: 54 chunks, 54 solver completions, 504,877 non-Air voxels (ID 3), 54 non-empty CPU meshes, 135,820 triangles; full runner exit 0. This is not a GPU/rendering claim. |
| Corner topology / exact DDA-only timing | **NOT RUN** | No corner-neighbor API; no isolated DDA hook. |

## 9. Recommended correction sequence (not applied)

1. **P02b — Coordinate canonicalization (prerequisite).** Centralize duplicate face/chart/grid conversions behind the canonical mapping and keep the storage-net/legacy row transform as a named adapter. First resolve and test the 18/54 `GetChunkCoordFromPosition` divergence while preserving its observed legacy behavior until consumers are enumerated. Keep persistent chunk identity unchanged.
2. **P02c — Cross-face mesher neighborhood.** Replace only the topology source for neighbor delivery with `ResolveNeighbor`-derived face coordinates; preserve net coordinates for storage/manager lookup. Acceptance: every directed edge in all sizes supplies the expected ECS neighbor; verify seam voxel visibility and mesh output separately.
3. **P02d — Planetary raycast and slab robustness.** Prioritize rejecting non-finite/zero rays, correcting world-vs-local broadphase, distance conversion for transformed chunks, and making the spherical raycast traverse the same authoritative voxel mapping as `QueryBlockCartesian`. Then test parallel slabs, tangencies, DDA ties and expected hit distances. Keep Cartesian legacy behavior under separate regression tests.

P02b must precede P02c and P02d because both consume face/grid coordinates. Once its adapter contract is fixed, P02c and P02d can proceed independently in separate patches; neither should be mixed into this characterization change.

## 10. Files and retained evidence

- Test-only additions: `src/world/PlanetaryVoxelE2ETests.cpp` (existing untracked P01 runner; additive diagnostics at lines 1931+, 2028+, 2224+, 2500+, 2619+).
- No production file or `CMakeLists.txt` changed.
- Final build log: MSBuild output recorded in the execution transcript; exit 0.
- Final complete runner log: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\P02A_E2E_20261105_03.log`.
- No commit, push, reset, clean, GUI run, or production correction was performed.

**P02a characterization outcome:** coordinate centers and edge reciprocity passed; cross-face mesher delivery and planetary raycast consistency failed with repeatable numeric witnesses. The next implementation should follow the dependency order above. No subsystem is declared FROZEN.
