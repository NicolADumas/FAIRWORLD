# FAIRWORLD P01 — Evidence Ledger

**Snapshot reference:** HEAD `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`, dirty working tree as inventoried in `FAIRWORLD_P01_BASELINE_REPORT.md`.  
**P01 execution policy:** no test or build was rerun. Every PASS below is a **historically documented result**, not a P01 rerun.

## Status key

- **PASS (reported):** prior report contains an observed command/result; evidence is attributed to that report and its named log where present.
- **PARTIAL:** some contract exercised; scope/paths not complete.
- **NOT VERIFIED:** no operational proof for the named layer or current runtime state.
- **Exit `—`:** the report did not supply an exit code for that subtest, so none is inferred.

## D1.2.19d.5 — Unified Radial Mapping

| Command / fixture | Historical result | Exit | Evidence provenance | Layer | Limit |
|---|---|---:|---|---|---|
| `cmake --build build_r3z7 --config Debug --target FairworldPlanetaryVoxelE2ETests -- /m:1` | PASS | 0 | `D1_2_19D5_UNIFIED_RADIAL_MAPPING_REPORT.md` | CPU/build | Historical build, not rerun at P01. |
| `cmake --build build_r3z7 --config Debug --target FairworldEditor -- /m:1` | PASS | 0 | same report | CPU/build | Does not prove GUI/GPU. |
| `FairworldPlanetaryVoxelE2ETests.exe --fixture D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\saves\map\world_map.json` — two runs | PASS, same summary/hash | 0 each | report and `d19d5_tiny_run1.log`, `d19d5_tiny_run2.log` | CPU | Tiny fixture only; movement path had one Grounded loss. |
| `FairworldEditor.exe --validate-d12` | 4/4 groups PASS | 0 | report, `d19d5_validate_d12.log` | CPU | Mathematical/serialization validation, not gameplay GUI. |
| Radial face/cell tests | 128/128 cells, `nextafter`, max round-trip delta 0; collision/mesh top radius both 42.5 for Tiny voxel y=82 | — | report | CPU | Specific formula/cases. |
| GUI / GPU | NOT VERIFIED | — | report states GUI not started | GUI/GPU | No visual or Vulkan runtime claim. |

**Documented radial convention:** `g(L,y)=L*128+y-64`; `r=R+g*S`; cell coordinate uses `(r-R)/S + 64 + 0.5`, then `floor`; outer face at `y+0.5`. The report identifies prior offset 25 vs 64 and documents the CPU correction.

## D1.2.19d.6 — Collision Continuity

| Command / fixture | Historical result | Exit | Evidence provenance | Layer | Limit |
|---|---|---:|---|---|---|
| `cmake --build build_r3z7 --config Debug --target FairworldPlanetaryVoxelE2ETests -- /m:1 /v:minimal` | PASS | 0 | `D1_2_19D6_PLAYER_COLLISION_CONTINUITY_REPORT.md` | CPU/build | Historical build only. |
| `FairworldPlanetaryVoxelE2ETests.exe --fixture D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\saves\map\world_map.json` — two final certification runs | PASS; equal summaries | 0 each | report, `d19d6_final_cert_run1.log`, `d19d6_final_cert_run2.log` | CPU | Headless trajectories; not interactive input. |
| Baseline A/B with original nine probes | FAIL reproduced: solid voxel at ring 0.95, angle 247.5°, penetration 0.998573 while grounded | nonzero (exact code not stated in report excerpt) | report, `d19d6_baseline9probes.log` | CPU | Deliberate failing baseline comparison. |
| Corrected `+Z → +X` route | PASS for tested route; grounded at end, dense collider scan 0 solid samples / 0 penetration | — | report | CPU | Does not cover all terrain trajectories. |
| Patch-uniform walk, variable timestep, Tiny route, diagonal and step tests | Individual PASS as scoped; step/diagonal can lose support; step did not await landing on the same trajectory | 0 for final runner | report | CPU | Full trajectory/terrain space not covered; probe cost not benchmarked. |
| GUI / GPU | NOT VERIFIED | — | report explicitly says not run | GUI/GPU | No runtime certification. |

The d6 report describes increasing the maximum query footprint from 36 to 68 samples (about +89% in the fully visited case). No isolated performance benchmark is recorded. `Grounded` may legitimately become false over a drop; it is not forced for continuity.

## I1 — Geometry & Collision

| Command / fixture | Historical result | Exit | Provenance | Layer | Coverage limit |
|---|---|---:|---|---|---|
| `FairworldPlanetaryVoxelE2ETests.exe --fixture ...TinySphere/world_map.json` (fixture under `D1_2_17_HEADLESS_SANDBOX/fixtures/TinySphere`) | Chunk identity report: 54/54 candidates, ECS entities, unique PlanetChunkCoord, lookups and solver; 504,877 non-Air voxels, ID 3 | 0 for identity gate; a later mesh gate in that historical runner returned 2 | `D1_2_18B_CHUNK_IDENTITY_FIX_REPORT.md` | CPU | The 18b runner explicitly did **not** verify non-empty CPU mesh; exit 2 reflected that outstanding gate. Do not call the whole runner PASS. |
| Tiny CPU mesh runner (later shared mesher) | 54 mesh CPU, 504,877 non-Air, 135,820 triangles; valid indices/UV/normals/winding as reported | 0 | `D1_2_18C_CPU_VOXEL_MESHER_REPORT.md`, later repeated in 18F/18G2C/19D3 reports | CPU | Mesh CPU only; no ECS MeshComponent/GPU draw implied. |
| D1.2 `--validate-d12` | 4 groups PASS in recorded later runs | 0 | d5, d17, 18F, 18G2C, 19D3 reports | CPU | Does not exercise all runtime chunk collision/mesh integration. |
| GUI/GPU | NOT VERIFIED in the later d5/d6 gates; separate D1.2.18g.2d GPU sandbox was interrupted/not verified | — | d5/d6 and 19d reports | GUI/GPU | No accepted full GPU path evidence. |

## I2 — Gravity & Movement

| Command / fixture | Historical result | Exit | Provenance | Layer | Coverage limit |
|---|---|---:|---|---|---|
| Tiny CPU E2E with spawn/camera/movement and gravity assertions, sandbox fixture | PASS: 54 chunks, 504,877 non-Air, 324-tick fall, radial gravity, jump/return, body→Transform→camera sync | 0 | `D1_2_19D3_PLANETARY_GRAVITY_REPORT.md`, logs `D1_2_19D2_SANDBOX/logs/d19d3_*.log` | CPU | No actual keyboard/mouse polling or GUI camera observation. |
| JobSystem contract | 10/10 PASS | 0 | D1.2.19d.3 report, `JobSystem.log` | CPU | Job contract only. |
| DMA result contract | 14/14 PASS | 0 | same report | CPU fault-injection | Simulated Vulkan results, not GPU submission. |
| VRAM allocator contract | 7/7 PASS (later report variants record 6/6 at earlier point) | 0 | D1.2.19d.3 and D1.2.18g.2c reports | CPU/fault-injection | Does not prove in-flight GPU retirement. Counts differ by report revision; use the latest explicit gate count only when identifying the binary/version. |
| Transfer/Graphics Sync contract | 6/6 PASS | 0 | D1.2.19d.3 and 18g.2c reports | CPU/fault-injection | Does not prove real queue/fence/draw. |
| `--validate-d12` | PASS, 4/4 groups | 0 | D1.2.19d.3 report | CPU | No GUI. |
| Player GUI, physical input, visual anchoring | NOT VERIFIED | — | D1.2.19d report: GUI prohibited/not started | GUI | No real input, rendered visibility, or Vulkan draw/fence evidence. |

## I3 — Block Maker Runtime Integration

| Command / fixture | Historical result | Exit | Provenance | Layer | Coverage limit |
|---|---|---:|---|---|---|
| Block Maker open in interrupted D1.2.14c session | UI opened (limited PASS); a grass albedo was logged as loaded to layer 1 | 0 for one normal close as reported; another startup has no recorded exit | `D1_2_14C_INTERRUPTED_SESSION_REPORT.md` | GUI, partial | User interrupted the session; no visual material sampling, voxel mesh, six-face, Planet Mapper, PlayState voxel, teleport, or full close/validation checks. |
| Headless authoring and persistence suites | Historical authoring/binding reports exist, but no cited test here proves Block Maker document → Planet Mapper → GameWorld → PlayState runtime integration | — | P01 source/report inventory | CPU | End-to-end runtime integration NOT VERIFIED. |
| Fixture/Tiny runtime | Separate planetary E2E reports exercise map generator/solver/ECS/CPU mesher, not an artifact authored and then consumed through a completed Block Maker GUI flow | 0 in later CPU runner | D1.2.18c, 19d.3 reports | CPU | Does not establish authoring source provenance or runtime UI behavior. |

## I4 — Material Atlas & PBR

| Command / fixture | Historical result | Exit | Provenance | Layer | Coverage limit |
|---|---|---:|---|---|---|
| `cmake --build build_r3z7 --config Debug --target ALL_BUILD -- /m:1 /v:minimal` | PASS | 0 | `D1_2_14A_PBR_CACHE_CORRECTNESS_REPORT.md` | CPU/build | Historical build. |
| `FairworldTextureCacheTests.exe` | 10/10 PASS; unchanged inputs, JSON/texture content, missing/appearing/removed files, corrupt/truncated/version, ID-layer/fallback ID12 | 0 | D1.2.14a, D1.2.17 reports | CPU | Cache logic; no GPU sampling proof. |
| `ctest --test-dir build_r3z7 -C Debug -R ^FairworldTextureCacheTests$ --output-on-failure` | 1/1 PASS | 0 | D1.2.14a report | CPU | One CTest registration. |
| `FairworldForgeVertexContractTests.exe` | PASS; 3 groups in D1.2.17 report | 0 | D1.2.17 report | CPU | Producer/layout contract, not actual GPU pixels. |
| `FairworldEditor.exe --validate-d12` | 4 groups PASS | 0 | D1.2.14a / D1.2.17 | CPU | Not PBR render validation. |
| Sandbox cache regeneration | v2 generated, 256 layers, ID12 albedo pixel `(0,6,255,255)`; validation reported 0 errors and 6 location-7 warnings; ownership 6 created / 6 destroyed / 0 live | process exit 0 | D1.2.14a report, sandbox log `D1_2_8_GUI_SANDBOX/runtime/logs/D1_2_14A_cache_regeneration.log` | GUI/GPU runtime, partial | Cache bytes/pixels and Vulkan messages logged; visual selection/sampling of materials was not thereby verified. |
| D1.2.14c visual session | Interrupted; visual checks listed NOT VERIFIED | one close exit 0, no completed session gate | interrupted-session report | GUI partial | No full Block Maker/Chunk Editor/Planet Mapper/PlayState visual acceptance. |

**Cross-report note:** D1.2.14b.1 records CTest 15/17 with EXT-10 and two lifecycle checks failing at that point. This is historical and must not be silently reconciled with later cache/contract PASS results; P01 did not rerun CTest or establish the current outcome.

## Evidence interpretation and next gates

1. d5 and d6 evidence is CPU-only and task-scoped; it supports the exact corrected cases and nothing broader.
2. I1 has later Tiny CPU mesh evidence, while chunk identity report 18b itself had exit 2 at the mesh gate. Use the later shared-mesher evidence for CPU mesh only.
3. I2 integrates more headless cases in d19d.3, but physical GUI input and actual visual physics remain open.
4. I3 has authoring infrastructure, but this ledger finds no completed, provenance-linked authoring→runtime acceptance run.
5. I4 has useful cache/vertex CPU contracts and one sandbox cache/GPU setup report; end-to-end visual draw/material proof is incomplete.
6. No test result in this ledger was rerun by P01. Full command lines omitted from historical reports remain “not recorded here”; do not invent them.

