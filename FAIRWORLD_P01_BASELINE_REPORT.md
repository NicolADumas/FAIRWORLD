# FAIRWORLD P01 — Baseline Report

**Snapshot:** 2026-11-05, repository `D:\FAIRWORLD\FAIRWORLD`.  
**Purpose:** freeze the evidence baseline only; no functional changes.  
**P01 tools:** read-only Git/filesystem inspection and SHA-256 reads. No build, tests, GUI, restore, cleanup, commit or push were run.

## 1. Repository identity

| Item | Snapshot |
|---|---|
| Branch | `SPHERE-WORLD-EXPERIMENT` |
| HEAD | `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1` |
| Tracked modified | 49 paths (44 production source, 1 source test, 1 shader, 1 asset/data JSON, 2 other: `CMakeLists.txt`, `imgui.ini`) |
| Untracked, excluding ignored | 375 paths at pre-P01 capture |
| Unmerged/conflict entries | none shown by `git status --short` |
| Working tree | **DIRTY**; not a clean reproducible checkout |
| `git diff --stat` | 49 files changed, 3,433 insertions, 1,076 deletions |

No commit is created by this report. The modified and untracked paths predate P01 except the three P01 documents and are preserved. Since no baseline copy of the complete diffs existed before this inventory, the recorded diffs are the current pre-P01 working-tree diff against HEAD, not proof of authorship or chronology. A complete path inventory is appended below; rerun commands in §5 to reproduce the snapshot. The P01 documents are intentionally excluded from the captured 375 untracked paths.

### Tracked modified paths

`CMakeLists.txt`; `assets/test_packaging_artifacts/pkg_roundtrip_03.json`; `imgui.ini`; `src/components/ForgeComponents.h`; `src/core/app/FAIRWORLD.cpp`; `src/core/app/FAIRWORLD.h`; `src/core/app/LifecycleFreezeGate.cpp`; `src/core/app/LifecycleFreezeGate.h`; `src/core/app/LifecycleRemediationContractTests.cpp`; `src/core/app/RuntimeManager.cpp`; `src/core/app/main.cpp`; `src/core/entities/Components.h`; `src/core/utils/World.cpp`; `src/render/RenderManager.cpp`; `src/render/RenderManager.h`; `src/render/TexturePacker.cpp`; `src/render/TexturePacker.h`; `src/render/VramSlabAllocator.cpp`; `src/render/VramSlabAllocator.h`; `src/render/VulkanDmaManager.cpp`; `src/render/VulkanDmaManager.h`; `src/render/forge.vert`; `src/render/vulkan/VulkanMemory.cpp`; `src/render/vulkan/VulkanMemory.h`; `src/states/BlockMakerState.cpp`; `src/states/ForgeState.cpp`; `src/states/PhysicsLabState.cpp`; `src/states/PlanetMapperState.cpp`; `src/states/PlayState.cpp`; `src/states/PlayState.h`; `src/states/components/PlanetMapperCamera.cpp`; `src/states/components/PlanetMapperCamera.h`; `src/states/components/PlanetMapperUI.cpp`; `src/systems/JobSystem.cpp`; `src/systems/JobSystem.h`; `src/systems/PhysicsEngine.cpp`; `src/systems/PhysicsEngine.h`; `src/systems/Systems.cpp`; `src/world/CubeSphereMapping.cpp`; `src/world/CubeSphereMapping.h`; `src/world/GameWorld.cpp`; `src/world/GameWorld.h`; `src/world/MapWorldGenerator.cpp`; `src/world/MeshGenerators.cpp`; `src/world/PlanetRadialMapping.cpp`; `src/world/PlanetRadialMapping.h`; `src/world/SphericalLOD.cpp`; `src/world/terrain/validation/TerrainValidation.cpp`; `src/world/terrain/validation/TerrainValidation.h`.

## 2. Untracked and generated content

`git ls-files --others --exclude-standard` counted 375 paths. Category counts (one path belongs to one category):

| Category | Paths | Examples / note |
|---|---:|---|
| Build/output | 249 | `build_r3/` 94; `build_r3z7/` 155, including CMake caches, Visual Studio projects, dependencies and generated build metadata. |
| Reports/logs/data | 99 | D1 audit/validation reports, logs, CSV/JSON evidence, CTest inventory and R3 validation logs. |
| Production source | 9 | New helper/implementation files in `src/`; see exact inventory appendix. |
| Source tests | 8 | JobSystem, DMA, allocator, transfer/graphics, Forge vertex, texture cache and planetary E2E contract tests. |
| Save data | 7 | `saves/` tree; not read or modified during P01. |
| Assets | 2 | `assets/layers/block_1_layers.json`, `assets/textures/blocks/fairworld_Grass_albedo.png`. |
| Other | 1 | `R3_validation_logs/R3_source_diff.patch` (captured source-diff artifact). |

Important generated/untracked roots: `build_r3/`, `build_r3z7/`, `R3_validation_logs/`, `saves/`, and report/log files at repository root. P01 did not delete or alter any of them. Exact untracked paths at capture appear in the appendices.

## 3. Build configuration inventory

| Configuration | Observed state | Notes |
|---|---|---|
| `CMakePresets.json` | Configure presets `x64-Debug` and `x64-Release` inherit `windows-base`; declared generator is Ninja; compiler cache variables `cl.exe`. | Preset definition only; no evidence P01 configured from it. |
| `build_r3z7/CMakeCache.txt` | Present; generator `Visual Studio 18 2026`; source `D:/FAIRWORLD/FAIRWORLD`; configurations `Debug;Release;MinSizeRel;RelWithDebInfo`. | Generated VS project files and test targets exist. Do not regenerate/reconfigure as part of baseline. |
| `build_r3/CMakeCache.txt` | Present; generator `Visual Studio 18 2026`; source `D:/FAIRWORLD/FAIRWORLD`; same multi-config list. | Separate generated tree. |
| Build evidence in prior reports | MSVC / Visual Studio 18 2026, `build_r3z7`, Debug. | Historical evidence only; P01 did not build. |

The checked-in preset says Ninja while existing caches use Visual Studio. This is a configuration divergence, not an instruction to switch generators. Use the existing `build_r3z7` cache for planned validations unless a later approved task explicitly changes that decision. The cache inspection did not identify a standalone compiler path in the queried entries.

## 4. Critical-file SHA-256 snapshot

Hashes below were computed from the current dirty working tree. They identify this P01 snapshot; they are **not** Git blob hashes and do not imply correspondence to a release build.

| File | Bytes | SHA-256 |
|---|---:|---|
| `src/world/PlanetRadialMapping.h` | 2,328 | `6D958E4781D2678BAC6FAAC60232207AEFD961B41616549B049E0F41D0604F07` |
| `src/world/PlanetRadialMapping.cpp` | 3,641 | `6061B9C589C38E6936D810D5EB1D67F84E88366ADD563D80D172DFA1318872BA` |
| `src/world/CubeSphereMapping.h` | 2,398 | `F640151BD2A888AE620B0DCE7E2ADCEE8297E647F421068E0E2213FB7B4DB5A7` |
| `src/world/CubeSphereMapping.cpp` | 3,710 | `28C1494AB36D3DB93CA084879E901D154F295D47481460E33E99A4ACBE37F905` |
| `src/world/MapWorldGenerator.cpp` | 25,137 | `73B69E957F4D7AC3E9C956F578903DA8D8B5BEDCCBDBEF192B27C929A5B61027` |
| `src/world/GameWorld.cpp` | 71,811 | `91DF781298C5224BCA7A4F83D013374DE40976B8060CCA6706799989D4E7345E` |
| `src/world/VoxelChunkMesher.cpp` | 14,023 | `3B529DABC04E6011A71A27693D4902592143191B0CE75908A6BB42ED9401EC02` |
| `src/systems/PhysicsEngine.cpp` | 44,038 | `52AC556FCA210E931151813CAA94537CF048C564E4174BEF82E8E99281CC86C6` |
| `src/systems/Systems.cpp` | 26,128 | `FF4D5DF720A851D78E9D84BF3B934CE5239A40C1C06577A480C5DB4031138A1D` |
| `src/states/PlayState.cpp` | 40,790 | `40F39376CAD012DFEB719638A4984B2AE745CF5FCC4137C2D59AE20AA1B2463B` |
| `src/states/BlockMakerState.cpp` | 608,007 | `C30A8A39E30C710879ABCA08228D54845B40238E5DDE9793F50AB6467BF17053` |
| `src/render/TexturePacker.cpp` | 20,991 | `4D7A3870FB06F18ED2ECA69CCA3E86011C2E145E857E8544007676529AB3F382` |
| `src/render/RenderManager.cpp` | 249,786 | `80496142C3BFA35C5F7A0AF6501BF2CEBFC69F8EDB87D63CBD14C8677AF4F8B2` |
| `src/render/forge.vert` | 1,943 | `6062EF06AD49EA676355F33BBBEA5E770FD395686E0D5FA11A7E9C4CB4E02358` |
| `src/render/forge.frag` | 7,162 | `9E105B3D9A08A71D9DC2BD15717FC6D0C00D974F0C95EAEA09B6DD58338E196F` |
| `D1_2_19D5_UNIFIED_RADIAL_MAPPING_REPORT.md` | 8,920 | `DDC57961A29B8C1F4AC31D72108166842AF1D30AEA22D4B1CEFEC98AD09140D8` |
| `D1_2_19D6_PLAYER_COLLISION_CONTINUITY_REPORT.md` | 8,514 | `BBA9AE35C11065CC8E22DA032C58FD735B14602917CFCEE6F63667DE8BCFFA09` |
| `FAIRWORLD_P00_MASTER_INTEGRATION_AUDIT.md` | 26,051 | `0D29BB86B637C6C3E46792040604D9D0C7A3102BFB06645EA6B31BF16265C8DB` |

The historical 40-file user-data manifest is not regenerated in P01. Two historical divergences remain documented in earlier reports (`assets/texture_cache.bin` and `assets/test_packaging_artifacts/pkg_roundtrip_03.json`); P01 did not compare/rebaseline those user-data hashes.

## 5. Reproduction commands

Run from the repository root to capture a new snapshot (it will of course include later P01 artifacts):

```powershell
git rev-parse HEAD
git branch --show-current
git status --short --untracked-files=all
git diff --stat
Get-FileHash -Algorithm SHA256 -LiteralPath <critical-path>
Select-String -Path build_r3z7/CMakeCache.txt -Pattern 'CMAKE_GENERATOR:|CMAKE_CONFIGURATION_TYPES:|CMAKE_HOME_DIRECTORY:'
```

P01 did not rerun historical test commands. The exact recorded commands and evidence provenance are in `FAIRWORLD_P01_EVIDENCE_LEDGER.md`.

## 6. GO / NO-GO for P02

**Decision: NO-GO for implementation; GO only for a read-only P02 contract review if the user explicitly authorizes that scope.**

Reason: the current tree has 49 modified tracked paths and 375 pre-P01 untracked paths, including relevant I1–I4 production code and tests. Although HEAD, path inventory and critical hashes are now recorded, the existing diff has no separately preserved known-good source baseline and the working tree is not a clean build snapshot. P02 work that edits shared coordinate/chunk/radial files now risks mixing attribution and regressions. Before any P02 patch, the user/reviewer should identify which existing changes constitute the approved baseline, or authorize a separate clean worktree/commit snapshot. No cleanup or checkout is proposed or performed.

This is not a claim that the code is incorrect. It is a provenance and change-isolation gate. No D1 freeze is declared.

## Appendix A — tracked and untracked path inventory at pre-P01 capture

The following list is the captured `git status --short --untracked-files=all`, excluding only the three P01 reports created after the snapshot. Directory entries for build/log roots are retained as reported by Git.

```text
 M CMakeLists.txt
 M assets/test_packaging_artifacts/pkg_roundtrip_03.json
 M imgui.ini
 M src/components/ForgeComponents.h
 M src/core/app/FAIRWORLD.cpp
 M src/core/app/FAIRWORLD.h
 M src/core/app/LifecycleFreezeGate.cpp
 M src/core/app/LifecycleFreezeGate.h
 M src/core/app/LifecycleRemediationContractTests.cpp
 M src/core/app/RuntimeManager.cpp
 M src/core/app/main.cpp
 M src/core/entities/Components.h
 M src/core/utils/World.cpp
 M src/render/RenderManager.cpp
 M src/render/RenderManager.h
 M src/render/TexturePacker.cpp
 M src/render/TexturePacker.h
 M src/render/VramSlabAllocator.cpp
 M src/render/VramSlabAllocator.h
 M src/render/VulkanDmaManager.cpp
 M src/render/VulkanDmaManager.h
 M src/render/forge.vert
 M src/render/vulkan/VulkanMemory.cpp
 M src/render/vulkan/VulkanMemory.h
 M src/states/BlockMakerState.cpp
 M src/states/ForgeState.cpp
 M src/states/PhysicsLabState.cpp
 M src/states/PlanetMapperState.cpp
 M src/states/PlayState.cpp
 M src/states/PlayState.h
 M src/states/components/PlanetMapperCamera.cpp
 M src/states/components/PlanetMapperCamera.h
 M src/states/components/PlanetMapperUI.cpp
 M src/systems/JobSystem.cpp
 M src/systems/JobSystem.h
 M src/systems/PhysicsEngine.cpp
 M src/systems/PhysicsEngine.h
 M src/systems/Systems.cpp
 M src/world/CubeSphereMapping.cpp
 M src/world/CubeSphereMapping.h
 M src/world/GameWorld.cpp
 M src/world/GameWorld.h
 M src/world/MapWorldGenerator.cpp
 M src/world/MeshGenerators.cpp
 M src/world/PlanetRadialMapping.cpp
 M src/world/PlanetRadialMapping.h
 M src/world/SphericalLOD.cpp
 M src/world/terrain/validation/TerrainValidation.cpp
 M src/world/terrain/validation/TerrainValidation.h
?? D1_1_MATHEMATICAL_CONVENTIONS.md
?? D1_2_10_GUI_OBSERVABILITY_AUDIT.md
?? D1_2_11_DIAGNOSTIC_INSTRUMENTATION_REPORT.md
?? D1_2_12A_RAYCAST_ISOLATION_REPORT.md
?? D1_2_12C_PLANET_RENDERING_MATERIALS_AUDIT.md
?? D1_2_12_FUNCTIONAL_VERIFICATION_REPORT.md
?? D1_2_12a_validate_d12.log
?? D1_2_14A_PBR_CACHE_CORRECTNESS_REPORT.md
?? D1_2_14B1_SHARED_FORGE_VERTEX_CONTRACT_REPORT.md
?? D1_2_14B2_INTEGRITY_RUNTIME_GATE_REPORT.md
?? D1_2_14B_LOD_VERTEX_CONTRACT_REPORT.md
?? D1_2_14C_INTERRUPTED_SESSION_REPORT.md
?? D1_2_16_INPUT_CAMERA_WORLD_DATA_FLOW_AUDIT.md
?? D1_2_17_PLANETARY_RUNTIME_CONTRACT_REPORT.md
?? D1_2_18A_MISSING_CHUNK_ROOT_CAUSE_REPORT.md
?? D1_2_18B_CHUNK_IDENTITY_FIX_REPORT.md
?? D1_2_18C_CPU_VOXEL_MESHER_REPORT.md
?? D1_2_18D_PLANETARY_MESH_WINDING_AUDIT.md
?? D1_2_18E_ASYNC_MESH_PUBLICATION_AUDIT.md
?? D1_2_18F_JOB_ACCEPTANCE_CONTRACT_REPORT.md
?? D1_2_18G1_DMA_VRAM_LIFETIME_AUDIT.md
?? D1_2_18G2A_DMA_RESULT_SAFETY_REPORT.md
?? D1_2_18G2B_VRAM_ALLOCATOR_SAFETY_REPORT.md
?? D1_2_18G2C_TRANSFER_GRAPHICS_SYNC_REPORT.md
?? D1_2_18_PLANETARY_VOXEL_E2E_REPORT.md
?? D1_2_19A_PLANETARY_SPAWN_REPORT.md
?? D1_2_19B_PLANETARY_CAMERA_REPORT.md
?? D1_2_19C_PLANETARY_MOVEMENT_REPORT.md
?? D1_2_19D1_RUNTIME_VISUAL_AUDIT.md
?? D1_2_19D2_RUNTIME_GROUNDING_VISIBILITY_REPORT.md
?? D1_2_19D3_PLANETARY_GRAVITY_REPORT.md
?? D1_2_19D3_all_build.err.log
?? D1_2_19D3_all_build.log
?? D1_2_19D3_all_build_final.log
?? D1_2_19D3_all_build_verbose.log
?? D1_2_19D4_SURFACE_CONTACT_AUDIT.md
?? D1_2_19D4_diagnostic_build.log
?? D1_2_19D5_UNIFIED_RADIAL_MAPPING_REPORT.md
?? D1_2_19D6_PLAYER_COLLISION_CONTINUITY_REPORT.md
?? D1_2_19D_PLANETARY_PLAYER_INTEGRATION_REPORT.md
?? D1_2_2_BUILD_DIAGNOSTICS.md
?? D1_2_3_VALIDATION_REPORT.md
?? D1_2_3_validate_d12.log
?? D1_2_4_FAILURE_ISOLATION.md
?? D1_2_4_validate_d12.log
?? D1_2_5_BOUNDARY_FIX_REPORT.md
?? D1_2_5_automatic_suite.log
?? D1_2_5_validate_d12.log
?? D1_2_6_EXT10_ISOLATION_REPORT.md
?? D1_2_6_ext10_diagnostic.log
?? D1_2_6_scientific_run1.log
?? D1_2_6_scientific_run2.log
?? D1_2_6_scientific_run3.log
?? D1_2_6_scientific_run4_restored.log
?? D1_2_7_FREEZE_READINESS_REPORT.md
?? D1_2_7_ctest_full.log
?? D1_2_7_ctest_inventory.json
?? D1_2_7_planetmapper.stderr.log
?? D1_2_7_planetmapper.stdin
?? D1_2_7_planetmapper.stdout.log
?? D1_2_7_validate_d12.log
?? D1_2_8_SAFE_GUI_ENVIRONMENT_REPORT.md
?? D1_2_9_GUI_VALIDATION_REPORT.md
?? D1_2_IMPLEMENTATION_REPORT.md
?? D1_2_PREPATCH_VERIFICATION.md
?? D1_PLANET_SPATIAL_AUDIT.md
?? FAIRWORLD_P00_MASTER_INTEGRATION_AUDIT.md
?? R3_validation_logs/Authoring_R3B.log
?? R3_validation_logs/Binding_R3B.log
?? R3_validation_logs/Build_Debug.log
?? R3_validation_logs/Build_Debug_R3BRebuild.log
?? R3_validation_logs/Editor_PersistenceReopen.log
?? R3_validation_logs/Editor_R3Final_GUICert.log
?? R3_validation_logs/Editor_R3Final_PersistenceReopen.log
?? R3_validation_logs/Editor_R3Final_PersistenceReopen_BlockMaker.log
?? R3_validation_logs/Editor_TwitchDisabled_GUIcycles.log
?? R3_validation_logs/Editor_TwitchEnabled.log
?? R3_validation_logs/FairworldAuthoringTests.log
?? R3_validation_logs/FairworldBindingTests.log
?? R3_validation_logs/FairworldLifecycleTests.log
?? R3_validation_logs/FairworldUVTests.log
?? R3_validation_logs/FairworldVulkanRecoveryTests.log
?? R3_validation_logs/LifecycleStability_1.log
?? R3_validation_logs/LifecycleStability_2.log
?? R3_validation_logs/LifecycleStability_3.log
?? R3_validation_logs/LifecycleStability_4.log
?? R3_validation_logs/LifecycleStability_5.log
?? R3_validation_logs/LifecycleStability_R3B_1.log
?? R3_validation_logs/LifecycleStability_R3B_2.log
?? R3_validation_logs/LifecycleStability_R3B_3.log
?? R3_validation_logs/LifecycleStability_R3B_4.log
?? R3_validation_logs/LifecycleStability_R3B_5.log
?? R3_validation_logs/LifecycleStability_R3B_summary.log
?? R3_validation_logs/Lifecycle_R3B.log
?? R3_validation_logs/R3_GUI_Certification.md
?? R3_validation_logs/R3_RESULTS.md
?? R3_validation_logs/R3_source_diff.patch
?? R3_validation_logs/UV_R3B.log
?? R3_validation_logs/VulkanRecovery_R3B.log
?? R3_validation_logs/paint_persistence_baseline.json
?? assets/layers/block_1_layers.json
?? assets/textures/blocks/fairworld_Grass_albedo.png
?? build_r3/ALL_BUILD.vcxproj
?? build_r3/ALL_BUILD.vcxproj.filters
?? build_r3/CMakeCache.txt
?? build_r3/CMakeFiles/4.3.1-msvc1/CMakeCXXCompiler.cmake
?? build_r3/CMakeFiles/4.3.1-msvc1/CMakeDetermineCompilerABI_CXX.bin
?? build_r3/CMakeFiles/4.3.1-msvc1/CMakeRCCompiler.cmake
?? build_r3/CMakeFiles/4.3.1-msvc1/CMakeSystem.cmake
?? build_r3/CMakeFiles/4.3.1-msvc1/CompilerIdCXX/CMakeCXXCompilerId.cpp
?? build_r3/CMakeFiles/4.3.1-msvc1/CompilerIdCXX/CompilerIdCXX.exe
?? build_r3/CMakeFiles/4.3.1-msvc1/CompilerIdCXX/CompilerIdCXX.vcxproj
?? build_r3/CMakeFiles/4.3.1-msvc1/VCTargetsPath.txt
?? build_r3/CMakeFiles/4.3.1-msvc1/VCTargetsPath.vcxproj
?? build_r3/CMakeFiles/62fbda1645fea3cfd4a1934493194407/INSTALL_force.rule
?? build_r3/CMakeFiles/62fbda1645fea3cfd4a1934493194407/RUN_TESTS_force.rule
?? build_r3/CMakeFiles/62fbda1645fea3cfd4a1934493194407/generate.stamp.rule
?? build_r3/CMakeFiles/CMakeConfigureLog.yaml
?? build_r3/CMakeFiles/InstallScripts.json
?? build_r3/CMakeFiles/TargetDirectories.txt
?? build_r3/CMakeFiles/VerifyGlobs.cmake
?? build_r3/CMakeFiles/c5656cedde46fe5f87bc021b2a2ae0e0/INSTALL_force.rule
?? build_r3/CMakeFiles/c5656cedde46fe5f87bc021b2a2ae0e0/RUN_TESTS_force.rule
?? build_r3/CMakeFiles/cmake.check_cache
?? build_r3/CMakeFiles/cmake.verify_globs
?? build_r3/CMakeFiles/generate.stamp
?? build_r3/CMakeFiles/generate.stamp.depend
?? build_r3/CMakeFiles/generate.stamp.list
?? build_r3/CTestTestfile.cmake
?? build_r3/FAIRWORLD.slnx
?? build_r3/FairworldAppearanceTests.vcxproj
?? build_r3/FairworldAppearanceTests.vcxproj.filters
?? build_r3/FairworldAsyncInputTests.vcxproj
?? build_r3/FairworldAsyncInputTests.vcxproj.filters
?? build_r3/FairworldAuthoringTests.vcxproj
?? build_r3/FairworldAuthoringTests.vcxproj.filters
?? build_r3/FairworldBindingTests.vcxproj
?? build_r3/FairworldBindingTests.vcxproj.filters
?? build_r3/FairworldCore.vcxproj
?? build_r3/FairworldCore.vcxproj.filters
?? build_r3/FairworldCustomScientificTests.vcxproj
?? build_r3/FairworldCustomScientificTests.vcxproj.filters
?? build_r3/FairworldDiagnostics.vcxproj
?? build_r3/FairworldDiagnostics.vcxproj.filters
?? build_r3/FairworldEditor.vcxproj
?? build_r3/FairworldEditor.vcxproj.filters
?? build_r3/FairworldEventTests.vcxproj
?? build_r3/FairworldEventTests.vcxproj.filters
?? build_r3/FairworldGameplayStressTests.vcxproj
?? build_r3/FairworldGameplayStressTests.vcxproj.filters
?? build_r3/FairworldLifecycleTests.vcxproj
?? build_r3/FairworldLifecycleTests.vcxproj.filters
?? build_r3/FairworldMemoryTests.vcxproj
?? build_r3/FairworldMemoryTests.vcxproj.filters
?? build_r3/FairworldNoiseTests.vcxproj
?? build_r3/FairworldNoiseTests.vcxproj.filters
?? build_r3/FairworldScientificTests.vcxproj
?? build_r3/FairworldScientificTests.vcxproj.filters
?? build_r3/FairworldSculptTests.vcxproj
?? build_r3/FairworldSculptTests.vcxproj.filters
?? build_r3/FairworldUVTests.vcxproj
?? build_r3/FairworldUVTests.vcxproj.filters
?? build_r3/FairworldVulkanRecoveryTests.vcxproj
?? build_r3/FairworldVulkanRecoveryTests.vcxproj.filters
?? build_r3/INSTALL.vcxproj
?? build_r3/INSTALL.vcxproj.filters
?? build_r3/Module_GeometryNodes.vcxproj
?? build_r3/Module_GeometryNodes.vcxproj.filters
?? build_r3/Module_Modeling.vcxproj
?? build_r3/Module_Modeling.vcxproj.filters
?? build_r3/Module_Sculpting.vcxproj
?? build_r3/Module_Sculpting.vcxproj.filters
?? build_r3/RUN_TESTS.vcxproj
?? build_r3/RUN_TESTS.vcxproj.filters
?? build_r3/ZERO_CHECK.vcxproj
?? build_r3/ZERO_CHECK.vcxproj.filters
?? build_r3/_deps/joltphysics-build/ALL_BUILD.vcxproj
?? build_r3/_deps/joltphysics-build/ALL_BUILD.vcxproj.filters
?? build_r3/_deps/joltphysics-build/CMakeFiles/Jolt.dir/MinSizeRel/cmake_pch.hxx
?? build_r3/_deps/joltphysics-build/CMakeFiles/Jolt.dir/RelWithDebInfo/cmake_pch.hxx
?? build_r3/_deps/joltphysics-build/CMakeFiles/Jolt.dir/cmake_pch.cxx
?? build_r3/_deps/joltphysics-build/CMakeFiles/_CMakeLTOTest-CXX/src/CMakeLists.txt
?? build_r3/_deps/joltphysics-build/CMakeFiles/_CMakeLTOTest-CXX/src/foo.cpp
?? build_r3/_deps/joltphysics-build/CMakeFiles/_CMakeLTOTest-CXX/src/main.cpp
?? build_r3/_deps/joltphysics-build/CMakeFiles/generate.stamp
?? build_r3/_deps/joltphysics-build/CMakeFiles/generate.stamp.depend
?? build_r3/_deps/joltphysics-build/CTestTestfile.cmake
?? build_r3/_deps/joltphysics-build/INSTALL.vcxproj
?? build_r3/_deps/joltphysics-build/INSTALL.vcxproj.filters
?? build_r3/_deps/joltphysics-build/Jolt.vcxproj
?? build_r3/_deps/joltphysics-build/Jolt.vcxproj.filters
?? build_r3/_deps/joltphysics-build/JoltPhysics.slnx
?? build_r3/_deps/joltphysics-build/RUN_TESTS.vcxproj
?? build_r3/_deps/joltphysics-build/RUN_TESTS.vcxproj.filters
?? build_r3/_deps/joltphysics-build/cmake_install.cmake
?? build_r3/cmake_install.cmake
?? build_r3z7/ALL_BUILD.vcxproj
?? build_r3z7/ALL_BUILD.vcxproj.filters
?? build_r3z7/CMakeCache.txt
?? build_r3z7/CMakeFiles/0a9dea4ddeb8df6873a55fa2f1c54b69/INSTALL_force.rule
?? build_r3z7/CMakeFiles/0a9dea4ddeb8df6873a55fa2f1c54b69/RUN_TESTS_force.rule
?? build_r3z7/CMakeFiles/0a9dea4ddeb8df6873a55fa2f1c54b69/generate.stamp.rule
?? build_r3z7/CMakeFiles/4.3.1-msvc1/CMakeCXXCompiler.cmake
?? build_r3z7/CMakeFiles/4.3.1-msvc1/CMakeDetermineCompilerABI_CXX.bin
?? build_r3z7/CMakeFiles/4.3.1-msvc1/CMakeRCCompiler.cmake
?? build_r3z7/CMakeFiles/4.3.1-msvc1/CMakeSystem.cmake
?? build_r3z7/CMakeFiles/4.3.1-msvc1/CompilerIdCXX/CMakeCXXCompilerId.cpp
?? build_r3z7/CMakeFiles/4.3.1-msvc1/CompilerIdCXX/CompilerIdCXX.exe
?? build_r3z7/CMakeFiles/4.3.1-msvc1/CompilerIdCXX/CompilerIdCXX.vcxproj
?? build_r3z7/CMakeFiles/4.3.1-msvc1/VCTargetsPath.txt
?? build_r3z7/CMakeFiles/4.3.1-msvc1/VCTargetsPath.vcxproj
?? build_r3z7/CMakeFiles/CMakeConfigureLog.yaml
?? build_r3z7/CMakeFiles/InstallScripts.json
?? build_r3z7/CMakeFiles/TargetDirectories.txt
?? build_r3z7/CMakeFiles/VerifyGlobs.cmake
?? build_r3z7/CMakeFiles/cmake.check_cache
?? build_r3z7/CMakeFiles/cmake.verify_globs
?? build_r3z7/CMakeFiles/fafe90db46d2cd01f3bba56642b89c6f/INSTALL_force.rule
?? build_r3z7/CMakeFiles/fafe90db46d2cd01f3bba56642b89c6f/RUN_TESTS_force.rule
?? build_r3z7/CMakeFiles/generate.stamp
?? build_r3z7/CMakeFiles/generate.stamp.depend
?? build_r3z7/CMakeFiles/generate.stamp.list
?? build_r3z7/CTestTestfile.cmake
?? build_r3z7/D1_2_11_full_debug_build.log
?? build_r3z7/D1_2_11_full_debug_build_attempt2.log
?? build_r3z7/D1_2_14A_cache_tests.log
?? build_r3z7/D1_2_14A_debug_build.log
?? build_r3z7/D1_2_14A_debug_build_retry.log
?? build_r3z7/D1_2_14A_validate_d12.log
?? build_r3z7/D1_2_2_msbuild.diag.log
?? build_r3z7/D1_2_4_rebuild.diag.log
?? build_r3z7/D1_2_5_full_debug_build.log
?? build_r3z7/D1_2_6_diagnostic_test_build.log
?? build_r3z7/D1_2_6_final_restore_build.log
?? build_r3z7/D1_2_6_restore_test_build.log
?? build_r3z7/FAIRWORLD.slnx
?? build_r3z7/FairworldAppearanceTests.vcxproj
?? build_r3z7/FairworldAppearanceTests.vcxproj.filters
?? build_r3z7/FairworldAsyncInputTests.vcxproj
?? build_r3z7/FairworldAsyncInputTests.vcxproj.filters
?? build_r3z7/FairworldAuthoringTests.vcxproj
?? build_r3z7/FairworldAuthoringTests.vcxproj.filters
?? build_r3z7/FairworldBindingTests.vcxproj
?? build_r3z7/FairworldBindingTests.vcxproj.filters
?? build_r3z7/FairworldCore.vcxproj
?? build_r3z7/FairworldCore.vcxproj.filters
?? build_r3z7/FairworldCustomScientificTests.vcxproj
?? build_r3z7/FairworldCustomScientificTests.vcxproj.filters
?? build_r3z7/FairworldDiagnostics.vcxproj
?? build_r3z7/FairworldDiagnostics.vcxproj.filters
?? build_r3z7/FairworldDmaResultContractTests.vcxproj
?? build_r3z7/FairworldDmaResultContractTests.vcxproj.filters
?? build_r3z7/FairworldEditor.vcxproj
?? build_r3z7/FairworldEditor.vcxproj.filters
?? build_r3z7/FairworldEventTests.vcxproj
?? build_r3z7/FairworldEventTests.vcxproj.filters
?? build_r3z7/FairworldForgeVertexContractTests.vcxproj
?? build_r3z7/FairworldForgeVertexContractTests.vcxproj.filters
?? build_r3z7/FairworldGameplayStressTests.vcxproj
?? build_r3z7/FairworldGameplayStressTests.vcxproj.filters
?? build_r3z7/FairworldJobSystemContractTests.vcxproj
?? build_r3z7/FairworldJobSystemContractTests.vcxproj.filters
?? build_r3z7/FairworldLifecycleTests.vcxproj
?? build_r3z7/FairworldLifecycleTests.vcxproj.filters
?? build_r3z7/FairworldMemoryTests.vcxproj
?? build_r3z7/FairworldMemoryTests.vcxproj.filters
?? build_r3z7/FairworldNoiseTests.vcxproj
?? build_r3z7/FairworldNoiseTests.vcxproj.filters
?? build_r3z7/FairworldPlanetaryVoxelE2ETests.vcxproj
?? build_r3z7/FairworldPlanetaryVoxelE2ETests.vcxproj.filters
?? build_r3z7/FairworldScientificTests.vcxproj
?? build_r3z7/FairworldScientificTests.vcxproj.filters
?? build_r3z7/FairworldSculptTests.vcxproj
?? build_r3z7/FairworldSculptTests.vcxproj.filters
?? build_r3z7/FairworldTextureCacheTests.vcxproj
?? build_r3z7/FairworldTextureCacheTests.vcxproj.filters
?? build_r3z7/FairworldTransferGraphicsSyncTests.vcxproj
?? build_r3z7/FairworldTransferGraphicsSyncTests.vcxproj.filters
?? build_r3z7/FairworldUVTests.vcxproj
?? build_r3z7/FairworldUVTests.vcxproj.filters
?? build_r3z7/FairworldVramAllocatorContractTests.vcxproj
?? build_r3z7/FairworldVramAllocatorContractTests.vcxproj.filters
?? build_r3z7/FairworldVulkanRecoveryTests.vcxproj
?? build_r3z7/FairworldVulkanRecoveryTests.vcxproj.filters
?? build_r3z7/INSTALL.vcxproj
?? build_r3z7/INSTALL.vcxproj.filters
?? build_r3z7/Module_GeometryNodes.vcxproj
?? build_r3z7/Module_GeometryNodes.vcxproj.filters
?? build_r3z7/Module_Modeling.vcxproj
?? build_r3z7/Module_Modeling.vcxproj.filters
?? build_r3z7/Module_Sculpting.vcxproj
?? build_r3z7/Module_Sculpting.vcxproj.filters
?? build_r3z7/RUN_TESTS.vcxproj
?? build_r3z7/RUN_TESTS.vcxproj.filters
?? build_r3z7/Testing/Temporary/CTestCostData.txt
?? build_r3z7/Testing/Temporary/LastTest.log
?? build_r3z7/Testing/Temporary/LastTest.log.tmp6c93f
?? build_r3z7/Testing/Temporary/LastTestsFailed.log
?? build_r3z7/ZERO_CHECK.vcxproj
?? build_r3z7/ZERO_CHECK.vcxproj.filters
?? build_r3z7/_deps/joltphysics-build/ALL_BUILD.vcxproj
?? build_r3z7/_deps/joltphysics-build/ALL_BUILD.vcxproj.filters
?? build_r3z7/_deps/joltphysics-build/CMakeFiles/Jolt.dir/MinSizeRel/cmake_pch.hxx
?? build_r3z7/_deps/joltphysics-build/CMakeFiles/Jolt.dir/RelWithDebInfo/cmake_pch.hxx
?? build_r3z7/_deps/joltphysics-build/CMakeFiles/Jolt.dir/cmake_pch.cxx
?? build_r3z7/_deps/joltphysics-build/CMakeFiles/_CMakeLTOTest-CXX/src/CMakeLists.txt
?? build_r3z7/_deps/joltphysics-build/CMakeFiles/_CMakeLTOTest-CXX/src/foo.cpp
?? build_r3z7/_deps/joltphysics-build/CMakeFiles/_CMakeLTOTest-CXX/src/main.cpp
?? build_r3z7/_deps/joltphysics-build/CMakeFiles/generate.stamp
?? build_r3z7/_deps/joltphysics-build/CMakeFiles/generate.stamp.depend
?? build_r3z7/_deps/joltphysics-build/CTestTestfile.cmake
?? build_r3z7/_deps/joltphysics-build/INSTALL.vcxproj
?? build_r3z7/_deps/joltphysics-build/INSTALL.vcxproj.filters
?? build_r3z7/_deps/joltphysics-build/Jolt.vcxproj
?? build_r3z7/_deps/joltphysics-build/Jolt.vcxproj.filters
?? build_r3z7/_deps/joltphysics-build/JoltPhysics.slnx
?? build_r3z7/_deps/joltphysics-build/RUN_TESTS.vcxproj
?? build_r3z7/_deps/joltphysics-build/RUN_TESTS.vcxproj.filters
?? build_r3z7/_deps/joltphysics-build/cmake_install.cmake
?? build_r3z7/cmake_install.cmake
?? build_r3z7/editor_build.log
?? build_r3z7/logs/Build_Debug.log
?? build_r3z7/logs/Build_Debug_R3BRebuild.log
?? build_r3z7/logs/Editor_PersistenceReopen.log
?? build_r3z7/logs/Editor_TwitchDisabled.stderr.log
?? build_r3z7/logs/Editor_TwitchDisabled.stdout.log
?? build_r3z7/logs/Editor_TwitchDisabled_GUIcycles.log
?? build_r3z7/logs/Editor_TwitchEnabled.log
?? build_r3z7/logs/FairworldAuthoringTests.log
?? build_r3z7/logs/FairworldAuthoringTests_R3B.log
?? build_r3z7/logs/FairworldBindingTests.log
?? build_r3z7/logs/FairworldBindingTests_R3B.log
?? build_r3z7/logs/FairworldLifecycleTests.log
?? build_r3z7/logs/FairworldLifecycleTests_R3B.log
?? build_r3z7/logs/FairworldUVTests.log
?? build_r3z7/logs/FairworldUVTests_R3B.log
?? build_r3z7/logs/FairworldVulkanRecoveryTests.log
?? build_r3z7/logs/FairworldVulkanRecoveryTests_R3B.log
?? build_r3z7/logs/LifecycleStability_1.log
?? build_r3z7/logs/LifecycleStability_2.log
?? build_r3z7/logs/LifecycleStability_3.log
?? build_r3z7/logs/LifecycleStability_4.log
?? build_r3z7/logs/LifecycleStability_5.log
?? build_r3z7/logs/LifecycleStability_R3B.log
?? build_r3z7/logs/LifecycleStability_R3B_1.log
?? build_r3z7/logs/LifecycleStability_R3B_2.log
?? build_r3z7/logs/LifecycleStability_R3B_3.log
?? build_r3z7/logs/LifecycleStability_R3B_4.log
?? build_r3z7/logs/LifecycleStability_R3B_5.log
?? build_r3z7/logs/editor_empty.stdin
?? build_r3z7/logs/paint_persistence_baseline.json
?? saves/map/world_map.fwb
?? saves/map/world_map.json
?? saves/world/chunk_-1_0.bin
?? saves/world/chunk_0_-1.bin
?? saves/world/chunk_0_0.bin
?? saves/world/chunk_0_1.bin
?? saves/world/chunk_1_0.bin
?? src/render/DmaResultContractTests.cpp
?? src/render/ForgeVertexContractTests.cpp
?? src/render/FrameSubmissionRecovery.h
?? src/render/FrameSubmissionRecoveryTests.cpp
?? src/render/TextureCacheContractTests.cpp
?? src/render/TransferGraphicsSync.h
?? src/render/TransferGraphicsSyncTests.cpp
?? src/render/VramAllocatorContractTests.cpp
?? src/systems/JobSystemContractTests.cpp
?? src/systems/PlanetaryCameraFrame.h
?? src/systems/PlanetaryMovementMath.h
?? src/world/PlanetGridCompatibility.h
?? src/world/PlanetarySpawnPolicy.h
?? src/world/PlanetaryVoxelE2ETests.cpp
?? src/world/VoxelChunkMesher.cpp
?? src/world/VoxelChunkMesher.h
?? src/world/VoxelFaceUV.h
```
