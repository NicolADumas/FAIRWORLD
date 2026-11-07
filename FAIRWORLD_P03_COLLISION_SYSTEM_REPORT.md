# FAIRWORLD P03 — Planetary Collision System Report

**Date:** 2026-11-06
**Result:** CPU characterization PASS; bounded initial-overlap recovery PASS; remaining collision contract risks keep P04 at **NO-GO** pending review.
**GUI/GPU:** NOT RUN, as required.

## 1. Safety gate and baseline

- Repository: `D:\FAIRWORLD\FAIRWORLD`.
- HEAD remains `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`, the P01 baseline commit.
- The working tree was already substantially dirty before P03. No reset, clean, checkout, commit, push, or CMake reconfiguration was performed. Existing edits and untracked files were preserved.
- Builds and logs were written under `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`; the fixture was read-only.
- Tiny fixture SHA-256 before/after: `47AF4358E4BD663926E64A43AC2FB116F3A2939BA5605FC89B2983D32A9AD503`.
- No asset or save file was written by the headless runner.

## 2. Collision path and ownership

The runtime path is:

```text
DeviceManager/InputState
  → PlayerMovementSystem::Update
  → tangent movement velocity / jump impulse
  → PhysicsSystem::Update
  → PhysicsEngine::StepSimulation
  → ApplyGravity + ApplyDrag + Integrate
  → ResolveCollisions / ResolveSphericalCollisions
  → GameWorld::QueryBlockCartesian
  → final RigidBody position and velocity
  → PhysicsSystem synchronizes player Transform to body + radial eye offset
```

- `PlayerMovementSystem` projects input onto the transported planetary tangent frame, sets tangential velocity, preserves radial velocity, and applies a scaled jump only when `isGrounded` is true. Input is consumed from the existing `DeviceManager`; P03 did not change input routing.
- `PhysicsSystem` skips players while `planetarySpawnPending`, passes the camera's planetary forward when available, calls `StepSimulation`, then writes the body plus radial `eyeOffset` to `TransformComponent`.
- `StepSimulation` applies inverse-square radial gravity for a logical sphere, applies drag/terminal-speed policy, integrates velocity, then resolves collision.
- `ResolveCollisions` dispatches to `ResolveSphericalCollisions` when a `PlanetGeometryComponent` exists. The dispatch currently sets `isSpherical=true` without checking `isLogicalSphere`; this is a confirmed compatibility-routing inconsistency. No production world in this P03 run exercised a flat gameplay world, and no change was made.
- The spherical solver is the positional authority for the player. It samples the cylindrical collider at four heights and 17 tangent-plane offsets, then moves in radial/right/forward substeps no larger than 0.1 world units. Tangential step-up is attempted only while grounded and blocked by a solid voxel.
- `GameWorld::QueryBlockCartesian` is the voxel authority. It distinguishes `SolidVoxel`, `Air`, `PassableVoxel`, `OutOfBounds`, `TerrainPending`, and invalid input. A missing chunk and an existing ungenerated chunk both currently return `TerrainPending`; the API does not distinguish those two causes.

### Grounded and contact semantics

`ResolveSphericalCollisions` clears Grounded at the start of each tick. A downward radial sweep that meets `SolidVoxel` sets `isGrounded=true`, records the sampled voxel, assigns the radial `localUp` as contact normal, and refines the last-clear/first-solid interval. Air, OutOfBounds, TerrainPending, and InvalidInput do not directly set Grounded. This passed the existing Pending/OOB regression.

The solver does not derive a geometric voxel face normal or verify that the blocking sample is in the foot support band before assigning Grounded. Therefore “downward motion was blocked by a solid sample” is confirmed; “the voxel provides valid support below the feet” is not guaranteed by the implementation. This remains a code-level risk, not a reproduced fixture failure: the P03 search tested 8,181 deterministic candidate sweeps around the +Z/+X region and found no Grounded result without any solid voxel under the 17-point foot footprint.

## 3. Confirmed root cause and controlled fix

### Initial overlap recovery

Before P03, the initial depenetration path tried only five outward offsets (`0.1` through `0.5` m). If all remained blocked, it silently continued from the original embedded position.

The deterministic witness started the Tiny player body at radius `41.5 m`, with measured solid surface radius `42.5 m` (1.0 m overlap), zero initial velocity, and one fixed tick:

| Measurement | Before | After |
|---|---:|---:|
| Starting/final radius | 41.5 / 41.5 m | 41.5 / 42.5 m |
| Measured surface radius | 42.5 m | 42.5 m |
| Remaining dense radial penetration | 1.0 m | 0 m |
| Dense solid collider samples | 110 | 0 |
| Collision probes in recovery tick | 6 | 162 |
| Grounded | false | true after the same tick's downward contact |
| Radial velocity after recovery | — | `7.62939e-06 m/s` |
| Unresolved-overlap diagnostic | unavailable | false |

The pre-fix evidence is in `P03_final_run1.log`; post-fix evidence is in `P03_afterfix_run1.log` and `P03_afterfix_run2.log`.

### Implemented local correction

In `PhysicsEngine::ResolveSphericalCollisions`, initial overlap now searches outward in 0.1 m increments until the first fully clear collider placement, bounded by the configured radial voxel-column air boundary derived from `PlanetSize`, `VoxelsY`, and `PlanetRadialMapping::ReferenceVoxelOffset`. It refines the first clear interval for eight iterations. A non-solid result such as Pending/OOB/InvalidInput stops recovery; it is not treated as clear. The body records `dbg_initialPenetrationUnresolved` if it cannot recover.

When this positional recovery changes the body position, the solver preserves the incoming radial velocity (and removes inward velocity if the tick also established Grounded), so the positional correction does not become an outward launch impulse.

This is a bounded correction of the demonstrated initial-overlap defect. It is not continuous collision detection and does not prove absence of tunneling for arbitrary collider shapes or trajectories.

## 4. Characterization and performance

The runner performs 14 deterministic cases over Tiny: idle at +Z; radial fall; tangent walk; diagonal movement; voxel edge; +Z/+X face seam; cube corner; 60 m/s and 200 m/s movement; and starts at the six face directions. Existing runner cases additionally cover step-up/down terrain, rest, fall/landing, jump/relanding, variable timestep, pending/unavailable terrain, OOB, and the D1.2.19d.6 seam witness.

| Case | Ticks | Total probes | Max probes/tick | Max dense penetration |
|---|---:|---:|---:|---:|
| idle +Z | 30 | 3,530 | 278 | 0 |
| radial fall | 30 | 4,080 | 136 | 0 |
| tangent walk | 30 | 6,120 | 204 | 0 |
| diagonal | 30 | 7,892 | 272 | 0 |
| voxel edge | 20 | 4,301 | 489 | 0 |
| cross-face | 30 | 6,280 | 428 | 0 |
| cube corner | 20 | 6,086 | 918 | 0 |
| 60 m/s | 1 | 816 | 816 | 0 |
| 200 m/s | 1 | 2,448 | 2,448 | 0 |
| +Y | 10 | 2,040 | 204 | 0 |
| -Y | 10 | 2,040 | 204 | 0 |
| -Z | 10 | 2,040 | 204 | 0 |
| +X | 10 | 2,257 | 346 | 0 |
| -X | 10 | 2,644 | 808 | 0 |
| **Total / maximum** | **212** | **52,574** | **2,448** | **0** |

The count is per actual `QueryBlockCartesian` call, opt-in for diagnostics and disabled by default. The 68 probes from d6 are the upper count for one complete collider-position check; movement, binary refinement, and step attempts can invoke that check repeatedly in a tick. Thus frame totals can exceed 68.

Debug timing across the two post-fix runs was stable in outcome but naturally varied in time. The 200 m/s case took approximately **13.78–13.79 ms** for one debug tick; the corner case reached about **5.0 ms**. This is close to a 60 Hz budget in Debug and warrants a separate Release benchmark. Release was not built or measured. Probe counts are deterministic between runs after excluding timing fields.

The d6 expansion from 36 to 68 probes per full check is approximately **+88.9%** in probe count for that one check. P03 does not reduce this sampling because no equivalent coverage proof was established.

## 5. Test and build evidence

The final runner invocations used working directory `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime` and the existing TinySphere JSON fixture at `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`. MSBuild was invoked against the existing generated project from the repository, with explicit output and intermediate paths redirected to the sandbox runtime.

| Command/action | Result | Exit code |
|---|---|---:|
| Baseline `p02d-runner-bin\FairworldPlanetaryVoxelE2ETests.exe --fixture <TinySphere JSON>` before P03 changes | PASS, baseline combined CPU runner | 0 |
| MSBuild existing `build_r3z7\FairworldCore.vcxproj`, Debug, output redirected to `p03-core-bin`, intermediate files to `p03-core-obj` | PASS | 0 |
| MSBuild isolated runner project, Debug, output to `p03-runner-bin`, objects to `p03-runner-obj` | PASS | 0 |
| `p03-runner-bin\FairworldPlanetaryVoxelE2ETests.exe --fixture <TinySphere JSON>` after the fix, run 1 | PASS, including P03 matrix and deep-overlap recovery | 0 |
| Same P03 runner command, run 2 | PASS; P03 counts, positions, penetration, and recovery output deterministic | 0 |
| Scoped `git diff --check -- src/systems/PhysicsEngine.cpp src/systems/PhysicsEngine.h` plus trailing-space scan of the test source | PASS | 0 |
| Global `git diff --check` | FAIL due pre-existing trailing whitespace in `src/states/BlockMakerState.cpp` (lines 4652–4658, 4668–4669, 11563, 11570–11571, 11806, 11828, 11830); outside P03 files | 2 |

The final combined runner includes and passed:

- P02a coordinate and reciprocal topology checks: 13,956 face/chart round trips; 2,112 reciprocal directed cross-face relations.
- P02c cross-face topology and seam meshing across all PlanetSize values: 55,824 resolved coordinates; zero missing/mismatched neighbor relations in the measured set.
- P02d Cartesian slab/DDA and planetary ray traversal: 324/324 planetary probes, including boundary, pending, missing, and OOB checks.
- P02b compatibility evidence in the same run: legacy Cartesian chunk registration, `GetChunkCoordFromPosition` digest preservation, canonical generator mapping and Tiny chunk identity all pass.
- Tiny E2E: 54 chunk identities and TerrainSolver completion; CPU mesh contract; spawn; radial gravity; step/locomotion; landing, jump, and relanding.
- D1.2.19d.5/.6 regression diagnostics: radial mapping and locomotion/seam cases execute. As documented by d6, some step/diagonal routes legitimately end airborne after leaving support; the suite checks penetration and state rather than forcing permanent Grounded.

These are CPU/headless results only. No GUI, Vulkan device, graphics draw, or GPU collision test was run.

## 6. Files changed for P03

The repository was already dirty and `PhysicsEngine.cpp/.h` already contained earlier D1.2.19d changes before P03. The complete Git diff in these files therefore includes pre-existing work. The P03 additions in this task are limited to:

- `src/systems/PhysicsEngine.h`: opt-in collision probe counter and unresolved-initial-penetration diagnostic fields.
- `src/systems/PhysicsEngine.cpp`: counter increment at the actual voxel-query call; bounded initial-overlap recovery; preservation of radial velocity during position recovery.
- `src/world/PlanetaryVoxelE2ETests.cpp`: P03 14-case probe matrix, 8,181-candidate false-Grounded search, and 1 m initial-overlap regression. This file was untracked in the pre-existing working tree; no unrelated content was removed.
- `FAIRWORLD_P03_COLLISION_SYSTEM_REPORT.md`: this report.

No renderer, mesher, RaycastSystem, input, camera, material, serializer, asset, or save file was changed.

## 7. Remaining issues and risks

1. **Grounded support validation — SUSPECTED, not reproduced.** The code assigns a radial normal when any cylindrical sample blocks a downward radial step; it does not identify a voxel face normal or require a foot-band support sample. The 8,181-case fixture search found no false-Grounded witness. A dedicated synthetic overhang/corner fixture or a richer collision result is needed before changing this behavior.
2. **Unresolved overlap outside loaded radial columns — bounded.** Recovery stops on Pending/OOB/InvalidInput and marks unresolved rather than inventing a free position. The tested 1 m loaded-terrain overlap recovers; unloaded terrain recovery has not been exercised as a startup policy.
3. **Finite sample footprint.** The 17-point perimeter is deterministic but is not an exact cylinder-vs-voxel sweep. The tested 60/200 m/s cases had zero dense-scan penetration, but this is not a general CCD guarantee.
4. **Performance.** Debug corner/high-speed ticks approach or exceed the 60 Hz budget. Release performance and worst-case terrain behavior remain unverified.
5. **Sphere dispatch.** Collision dispatch treats any `PlanetGeometryComponent` as spherical, even when `isLogicalSphere` is false. Gameplay is architecturally spherical; legacy/document paths still need explicit coverage before any compatibility change.
6. **Missing vs Pending.** `QueryBlockCartesian` intentionally collapses missing and ungenerated chunks into `TerrainPending`; callers cannot report which condition caused the block.
7. **Global whitespace gate.** The repository-wide diff check remains blocked by pre-existing trailing whitespace in BlockMakerState; P03-scoped checks pass.

## 8. Acceptance and P04 recommendation

- **PASS:** 14 Tiny CPU characterization scenarios; deterministic outputs across two post-fix runs; zero dense radial penetration in those cases.
- **PASS:** demonstrated 1 m initial overlap now recovers to clear terrain without an outward velocity impulse.
- **PASS:** combined P02a/b/c/d, Tiny CPU E2E, radial mapping, and d6 regressions in the available runner.
- **NOT VERIFIED:** all-size collision recovery, exact support normals/false-Grounded overhang, Release performance, GUI behavior, and GPU behavior.

**P04: NO-GO for collision-dependent implementation until Grounded support semantics and the unresolved/Pending policy have dedicated fixtures and acceptance criteria.** The current patch fixes the confirmed bounded-depenetration failure only; it does not certify the full planetary collision contract.

No freeze or commit is proposed. Stop after P03 and await review.
