#pragma once

namespace fw {

class TerrainValidation {
public:
    static bool RunAll();
    static bool RunLegacyVoxelWriterAudit();
    
    // Phase 5 Determinism Freeze Gate
    static bool RunDeterminismTest();
    
    // Phase 5.1 Cube-Sphere Continuity Freeze Gate
    static bool RunCubeSphereContinuityTest();
    
    // Phase 5.2 Workspace Freeze Gate
    static bool RunWorkspaceTest();

    // Phase D2 Coordinate Contract Gate
    static bool RunD2CoordinateContractTest();

    // Phase D3 CubeSphere SSOT Gate
    static bool RunD3CubeSphereSSOTGateTest();

    // Phase D4 Mapping / Determinism Gate
    static bool RunD4MappingDeterminismGateTest();

private:
    static bool TestD4CanonicalCenters();
    static bool TestD4FaceOrientation();
    static bool TestD4RoundTripInterior();
    static bool TestD4DirectionRoundTrip();
    static bool TestD4CubeEdges();
    static bool TestD4CubeCorners();
    static bool TestD4EdgeEpsilons();
    static bool TestD4UVGridContract();
    static bool TestD4InvalidInputs();
    static bool TestD4DeterminismRepeated();
    static bool TestSameThreadReuse();
    static bool TestOrderIndependence();
    static bool TestParallelDeterminism();
    static bool TestInterleaving();
    static bool TestSeedSensitivity();
    static bool TestRuleSensitivity();
    
    // Phase 5.1
    static bool TestIntraFacePosition();
    static bool TestCrossFacePosition();
    static bool TestIntraFaceField();
    static bool TestCrossFaceField();
    
    // Phase 5.2
    static bool TestMemoryFootprint();
    static bool Test2DCapacity();
    static bool Test3DCapacity();
    static bool TestCapacityGrowth();
    static bool TestGenerationAllocations();
    static bool TestRepeatedGeneration();
    
    // Phase 5.3 Pipeline
    static bool RunPipelineTest();
    
    // Phase D2
    static bool TestPlanetID();
    static bool TestPlanetSizePreserved();
    static bool TestPlanetChunkCoord();
    
private:
    static bool TestStableRuleHash();
    static bool TestStableRegeneration();
    static bool TestStableGPUUpload();
    
    static void PrintResult(const char* testName, bool passed);
};

} // namespace fw
