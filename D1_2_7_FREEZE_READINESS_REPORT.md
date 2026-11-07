# FAIRWORLD — D1.2.7 Freeze Readiness Report

## Decision

**D1 INCOMPLETE — not ready for freeze.** The D1.2 automated mathematical gate passes. The full CTest run completed all 15 registered tests; the only failure is the already isolated EXT-10 `catalogGeneration` mismatch. The required Planet Mapper face/border/spawn checks and PlayState GUI checks were not completed safely.

## A — Automated verification

### D1.2 targeted validation

Command, from `D:\FAIRWORLD\FAIRWORLD`:

```powershell
& '.\bin\Debug\FairworldEditor.exe' --validate-d12
```

- Executable: `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe` (existing Debug build; no rebuild performed in this validation step).
- Result: **PASS**, exit code **0**, measured duration **249 ms**.
- All four reported groups passed:
  - UV/chart/direction round-trip, six faces, edges, corners and ties.
  - Six sizes, valid rows, cell centers, endpoints and U/V `nextafter` boundaries.
  - Single legacy-row conversion through TerrainSolver and mesh mapping.
  - Legacy spawn JSON/binary values and direction preservation.
- Complete output: `D1_2_7_validate_d12.log`.

### Complete CTest run

Command:

```powershell
& 'D:\Visual Code\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir 'build_r3z7' -C Debug --output-on-failure --parallel 1
```

CTest ran all tests sequentially and continued after failures. Result: **14 PASS / 1 FAIL**, 15 total; CTest exit code **8**; total CTest time **49.35 s** (wrapper wall time **50.188 s**).

| # | Test | Result | Duration |
|---:|---|---|---:|
| 1 | FairworldUVTests | PASS | 14.32 s |
| 2 | FairworldSculptTests | PASS | 0.26 s |
| 3 | FairworldScientificTests | **FAIL** | 0.34 s |
| 4 | FairworldEventTests | PASS | 0.13 s |
| 5 | FairworldNoiseTests | PASS | 0.02 s |
| 6 | FairworldAsyncInputTests | PASS | 0.30 s |
| 7 | FairworldMemoryTests | PASS | 0.60 s |
| 8 | FairworldGameplayStressTests | PASS | 1.52 s |
| 9 | FairworldDiagnostics | PASS | 8.67 s |
| 10 | FairworldLifecycleTests | PASS | 8.05 s |
| 11 | FairworldAppearanceTests | PASS | 0.07 s |
| 12 | FairworldCustomScientificTests | PASS | 13.00 s |
| 13 | FairworldBindingTests | PASS | 1.27 s |
| 14 | FairworldAuthoringTests | PASS | 0.40 s |
| 15 | FairworldVulkanRecoveryTests | PASS | 0.09 s |

`FairworldScientificTests` reported 37/38 internal cases passing; the sole failure is **EXT-10**, with `okSave1=1`, `okLoad=1`, `okSave2=1`, and `identical=0`. It is recorded as **FAIL**, not excluded or counted as PASS. The prior read-only isolation in [D1_2_6_EXT10_ISOLATION_REPORT.md](D1_2_6_EXT10_ISOLATION_REPORT.md) found the sole JSON difference at `/catalogGeneration` (`1` → `2`), which is the registry's monotonic generation field. The test compares the whole JSON text byte-for-byte. The EXT-10 path does not call D1.2 spatial components; the observed failure is classified as unrelated to D1.2, but the overall CTest gate remains FAIL.

Complete output: `D1_2_7_ctest_full.log`. The suite was not interrupted at the first failure. No additional failures occurred. A separate Lifecycle Stability 5/5 invocation was not run; CTest's `FairworldLifecycleTests` passed.

## B — Planet Mapper GUI

The existing Debug executable was started with `--disable-twitch-layer`, and Planet Mapper was opened. The GUI displayed its project panel and the current camera report **NORD (+Z)**. The size selector exposed Tiny, Small, Medium, Large, Huge and Gigantic. Selecting Tiny displayed **Risoluzione Faccia: 3 × 3** and **Chunk Totali Generati: 54**; the planet preview and orientation axes became visible. The run's Vulkan log showed validation layers active, zero validation errors, six semaphores created and destroyed, zero tracked semaphores live, and a clean ownership gate. The shutdown lifecycle gate passed.

The interactive verification stopped at this point. Selecting Tiny caused the application to auto-save and report `SALVATO! (saves/map/world_map.json)`. Before launch, `saves` contained only `lab` and `world`; `saves/map` did not exist. The application created `saves/map/world_map.json` (6,492 bytes) and `saves/map/world_map.fwb` (1,301 bytes), both timestamped 23:01:48. This was not an overwrite of a pre-existing map, but it is a new persistent project artifact caused by the GUI check.

The editor was closed; its log reports normal runtime shutdown and `VULKAN OWNERSHIP: CLEAN (PASS)`. An automatic-review check rejected removal of the generated files and explicitly instructed not to bypass the rejection. They remain at the paths above pending user authorization. No further Planet Mapper interactions were made after discovering the autosave.

| Check | Status | Evidence / limitation |
|---|---|---|
| Planet Mapper opens and renders preview | PARTIAL PASS | UI visible; Tiny preview and axes became visible. |
| One face orientation | PARTIAL PASS | UI reported NORD (+Z); no systematic six-face verification. |
| All six face orientations and UV consistency | NOT VERIFIED | No safe way to complete after UI changes triggered autosave. |
| Terrain selection and spawn creation | NOT VERIFIED | Not exercised. |
| Grid edges/borders | NOT VERIFIED in GUI | Automated D1.2 boundary tests passed. |
| All six GUI-supported sizes | NOT VERIFIED | Options observed; only Tiny selected. |
| Validation / semaphore shutdown for this run | PASS | 0 validation errors; 6 created / 6 destroyed; 0 live; ownership gate clean. |
| Process exit code | NOT CAPTURED | Window closed normally; shutdown gate passed in log, but the shell wrapper did not retain an exit-code record. |

GUI evidence: `D1_2_7_planetmapper.stdout.log` and `D1_2_7_planetmapper.stderr.log`.

## C — PlayState and persisted documents

PlayState was not launched. Source inspection shows `PlayState::~PlayState()` calls `forgeWorld->ClearWorld(true)`, and `PlayState::Init()` sets the save directory to `saves/world`; that directory already existed before this validation. Running the state could rewrite original user chunks during teardown, contrary to the no-overwrite requirement.

The D1.2 automated group **“Legacy spawn JSON/binary values and direction preserved”** passed. This does not substitute for the requested GUI inspection of initial spawn, teleport, face-relative direction, or loading an existing saved document in PlayState. No original saved document was opened or modified in PlayState.

## D — Freeze readiness matrix

| D1 requirement | Automated evidence | GUI evidence | Status |
|---|---|---|---|
| Canonical face/UV/direction math and round-trip | `--validate-d12` PASS; six-face mathematical checks | Only current camera report NORD (+Z) observed | **Automated PASS; GUI incomplete** |
| Grid conversion, bounds, ties and legacy row | `--validate-d12` PASS across six sizes | Tiny alone displayed as 3×3 | **Automated PASS; GUI incomplete** |
| Spawn producers/consumers and persisted legacy formats | JSON/binary compatibility group PASS | No new spawn created; PlayState not run | **Automated PASS; GUI incomplete** |
| Full automated suite | 14/15 CTest PASS | N/A | **FAIL due EXT-10** |
| EXT-10 exception | Separately isolated to generation metadata; no spatial call path found | N/A | **Documented external failure; not PASS** |
| Planet Mapper selection, all six faces/sizes, terrain, spawn | N/A | Autosave interrupted safe exploration | **Incomplete** |
| PlayState spawn/teleport/direction/document compatibility | Automated compatibility partial | Not run due possible writes to existing `saves/world` | **Incomplete** |
| Existing user saves unchanged | Pre-run `saves/map` absent; no prior map overwritten | New default map artifacts were created during the GUI check and remain | **Requires review/authorization** |

## Recommendation

**D1 INCOMPLETE.** The mathematical test gate passes, and EXT-10 is documented as a separate scientific-registry issue, but the required GUI coverage was not completed and a GUI test created a new map save. The generated `saves/map/world_map.json` and `saves/map/world_map.fwb` were not present before the test and remain because automatic review rejected their deletion. Review these artifacts and authorize whether they should be removed before any further GUI validation. Do not declare D1 FROZEN.

No source, test, serializer, or existing save was intentionally edited; no commit or push was made.
