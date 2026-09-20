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

private:
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
    
private:
    static bool TestStableRuleHash();
    static bool TestStableRegeneration();
    static bool TestStableGPUUpload();
    
    static void PrintResult(const char* testName, bool passed);
};

} // namespace fw
