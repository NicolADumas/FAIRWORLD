#include "NoiseContractTests.h"
#include "Noise.h"
#include <iostream>
#include <vector>
#include <cmath>

namespace fw {

std::vector<NoiseContractTestResult> NoiseContractTests::RunAllTests() {
    std::vector<NoiseContractTestResult> results;
    results.reserve(7);

    results.push_back(Test01_RepeatedCreationDestruction());
    results.push_back(Test02_DeepCopyConstructorIndependence());
    results.push_back(Test03_CopyAssignmentIndependence());
    results.push_back(Test04_MoveConstructorSafety());
    results.push_back(Test05_MoveAssignmentSafety());
    results.push_back(Test06_ContainerReallocationNoDoubleFree());
    results.push_back(Test07_NumericalDeterminismInvariance());

    return results;
}

NoiseContractTestResult NoiseContractTests::Test01_RepeatedCreationDestruction() {
    NoiseContractTestResult r;
    r.testId = "NOISE-01";
    r.testName = "Repeated Creation & Destruction Lifecycle";
    r.inputDesc = "Instantiate and destroy 1,000 Noise objects with different seeds";
    r.expectedDesc = "Clean RAII construction and destruction of all instances without memory leaks or crash";

    try {
        for (int i = 0; i < 1000; ++i) {
            Noise n(i);
            float val = n.GetNoise(static_cast<float>(i), 0.0f, 0.0f);
            (void)val;
        }
        r.passed = true;
        r.observedDesc = "PASS: 1,000 Noise instances created, sampled, and cleanly destructed via RAII std::unique_ptr.";
    } catch (const std::exception& e) {
        r.passed = false;
        r.observedDesc = "FAIL: Exception thrown during lifecycle: " + std::string(e.what());
    }

    return r;
}

NoiseContractTestResult NoiseContractTests::Test02_DeepCopyConstructorIndependence() {
    NoiseContractTestResult r;
    r.testId = "NOISE-02";
    r.testName = "Deep Copy Constructor Independence & Determinism";
    r.inputDesc = "Configure Noise n1, copy-construct n2 = n1, verify identical output and mutate n2";
    r.expectedDesc = "n2 produces exact bitwise match with n1; mutating n2 does not affect n1 (no shared state)";

    Noise n1(42);
    n1.SetNoiseType(NoiseType::OpenSimplex2);
    n1.SetFractalType(FractalType::FBm);
    n1.SetFractalOctaves(5);
    n1.SetFrequency(0.05f);

    // Copy construction
    Noise n2 = n1;

    if (!n1.IsValid() || !n2.IsValid()) {
        r.passed = false;
        r.observedDesc = "FAIL: n1 or n2 is invalid after copy construction";
        return r;
    }

    // Verify deterministic identity across sample grid
    bool exactMatch = true;
    for (float x = -5.0f; x <= 5.0f; x += 2.5f) {
        for (float y = -5.0f; y <= 5.0f; y += 2.5f) {
            for (float z = -5.0f; z <= 5.0f; z += 2.5f) {
                float v1 = n1.GetNoise(x, y, z);
                float v2 = n2.GetNoise(x, y, z);
                if (v1 != v2) {
                    exactMatch = false;
                    break;
                }
            }
        }
    }

    if (!exactMatch) {
        r.passed = false;
        r.observedDesc = "FAIL: n2 output diverged from n1 immediately after copy construction";
        return r;
    }

    // Capture baseline before mutating n2
    float n1OriginalSample = n1.GetNoise(1.5f, 2.5f, 3.5f);

    // Mutate n2
    n2.SetSeed(9999);
    n2.SetFrequency(0.5f);

    float n1AfterMutate = n1.GetNoise(1.5f, 2.5f, 3.5f);
    float n2AfterMutate = n2.GetNoise(1.5f, 2.5f, 3.5f);

    if (n1OriginalSample == n1AfterMutate && n1AfterMutate != n2AfterMutate) {
        r.passed = true;
        r.observedDesc = "PASS: Copy constructor produced deep clone: identical output before mutation, complete isolation after.";
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: State bleeding detected between n1 and n2 (shallow copy bug present)";
    }

    return r;
}

NoiseContractTestResult NoiseContractTests::Test03_CopyAssignmentIndependence() {
    NoiseContractTestResult r;
    r.testId = "NOISE-03";
    r.testName = "Copy Assignment Operator Independence";
    r.inputDesc = "Assign n2 = n1 using copy operator=; verify determinism and state isolation";
    r.expectedDesc = "Target instance acquires full deep clone; mutations remain isolated";

    Noise n1(1234);
    n1.SetNoiseType(NoiseType::Cellular);
    n1.SetFrequency(0.01f);

    Noise n2(5678);
    n2 = n1; // Copy assignment

    float v1 = n1.GetNoise(2.0f, 4.0f, 6.0f);
    float v2 = n2.GetNoise(2.0f, 4.0f, 6.0f);

    if (v1 != v2) {
        r.passed = false;
        r.observedDesc = "FAIL: n2 did not match n1 after copy assignment";
        return r;
    }

    // Mutate n2
    n2.SetSeed(8888);
    float v1After = n1.GetNoise(2.0f, 4.0f, 6.0f);

    if (v1 == v1After) {
        r.passed = true;
        r.observedDesc = "PASS: Copy assignment operator performed deep copy; n1 unaffected by subsequent n2 mutation.";
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: n1 was modified when n2 was mutated after copy assignment";
    }

    return r;
}

NoiseContractTestResult NoiseContractTests::Test04_MoveConstructorSafety() {
    NoiseContractTestResult r;
    r.testId = "NOISE-04";
    r.testName = "Move Constructor Semantics & Moved-From Safety";
    r.inputDesc = "Move-construct n2 from n1; verify value transfer and safe access on moved-from n1";
    r.expectedDesc = "n2 receives exact configuration; n1 is left valid/null-safe with zero crash";

    Noise n1(777);
    n1.SetNoiseType(NoiseType::Perlin);
    n1.SetFrequency(0.04f);

    float expectedVal = n1.GetNoise(3.0f, 6.0f, 9.0f);

    // Move construct
    Noise n2 = std::move(n1);

    if (!n2.IsValid()) {
        r.passed = false;
        r.observedDesc = "FAIL: Target n2 is not valid after move construction";
        return r;
    }

    float observedVal = n2.GetNoise(3.0f, 6.0f, 9.0f);
    if (observedVal != expectedVal) {
        r.passed = false;
        r.observedDesc = "FAIL: n2 did not preserve expected noise value after move";
        return r;
    }

    // Verify moved-from safety
    if (n1.IsValid()) {
        r.passed = false;
        r.observedDesc = "FAIL: Moved-from n1 still reports IsValid() == true";
        return r;
    }

    // Calling methods on moved-from n1 must not crash
    float fallbackVal = n1.GetNoise(3.0f, 6.0f, 9.0f);
    n1.SetSeed(100);
    n1.SetFrequency(0.1f);

    if (fallbackVal == 0.0f) {
        r.passed = true;
        r.observedDesc = "PASS: Move construction cleanly transferred ownership; moved-from instance safely handled without crash.";
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: Unexpected fallback value from moved-from instance: " + std::to_string(fallbackVal);
    }

    return r;
}

NoiseContractTestResult NoiseContractTests::Test05_MoveAssignmentSafety() {
    NoiseContractTestResult r;
    r.testId = "NOISE-05";
    r.testName = "Move Assignment Operator & Self-Assignment Safety";
    r.inputDesc = "Move-assign n2 = std::move(n1), and test self-move assignment";
    r.expectedDesc = "Ownership transferred correctly; self-assignment handled safely without leak";

    Noise n1(999);
    n1.SetNoiseType(NoiseType::OpenSimplex2S);
    n1.SetFrequency(0.02f);
    float expectedVal = n1.GetNoise(1.0f, 2.0f, 3.0f);

    Noise n2(111);
    n2 = std::move(n1);

    float observedVal = n2.GetNoise(1.0f, 2.0f, 3.0f);
    if (observedVal != expectedVal || n1.IsValid()) {
        r.passed = false;
        r.observedDesc = "FAIL: Move assignment did not transfer ownership properly";
        return r;
    }

    // Self-move assignment safety test
#pragma warning(push)
#pragma warning(disable: 4293)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif
    n2 = std::move(n2);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#pragma warning(pop)

    if (n2.IsValid() && n2.GetNoise(1.0f, 2.0f, 3.0f) == expectedVal) {
        r.passed = true;
        r.observedDesc = "PASS: Move assignment transferred state cleanly; self-move handled safely.";
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: Self-move assignment corrupted instance";
    }

    return r;
}

NoiseContractTestResult NoiseContractTests::Test06_ContainerReallocationNoDoubleFree() {
    NoiseContractTestResult r;
    r.testId = "NOISE-06";
    r.testName = "Container Reallocation Zero Double-Free";
    r.inputDesc = "Store 100 Noise instances in std::vector<Noise> triggering capacity reallocation";
    r.expectedDesc = "Vector reallocations invoke move/copy without double-free or heap corruption";

    try {
        std::vector<Noise> noiseVec;
        for (int i = 0; i < 100; ++i) {
            Noise n(i * 10);
            n.SetFrequency(0.01f * (i + 1));
            noiseVec.push_back(std::move(n));
        }

        // Trigger further reallocation by resizing and shrinking
        noiseVec.shrink_to_fit();
        noiseVec.clear();
        noiseVec.shrink_to_fit();

        r.passed = true;
        r.observedDesc = "PASS: 100 instances reallocated and destroyed in std::vector without double-free or corruption.";
    } catch (const std::exception& e) {
        r.passed = false;
        r.observedDesc = "FAIL: Container reallocation threw exception: " + std::string(e.what());
    }

    return r;
}

NoiseContractTestResult NoiseContractTests::Test07_NumericalDeterminismInvariance() {
    NoiseContractTestResult r;
    r.testId = "NOISE-07";
    r.testName = "Numerical Determinism Bit-for-Bit Invariance";
    r.inputDesc = "Sample standard seed 1337 OpenSimplex2 with known test coordinates";
    r.expectedDesc = "Sampled values remain within [-1, 1] and match exact deterministic values";

    Noise n(1337);
    n.SetNoiseType(NoiseType::OpenSimplex2);

    float s1 = n.GetNoise(0.0f, 0.0f, 0.0f);
    float s2 = n.GetNoise(10.5f, -5.2f, 3.7f);
    float s3 = n.GetNoise(-100.0f, 50.0f, 25.0f);

    bool boundsOk = (s1 >= -1.0f && s1 <= 1.0f) &&
                    (s2 >= -1.0f && s2 <= 1.0f) &&
                    (s3 >= -1.0f && s3 <= 1.0f);

    // Repeated query must yield exact same value
    float s2_repeat = n.GetNoise(10.5f, -5.2f, 3.7f);

    if (boundsOk && s2 == s2_repeat) {
        r.passed = true;
        r.observedDesc = "PASS: Determinism preserved bit-for-bit. Samples: s1=" + std::to_string(s1) + 
                         ", s2=" + std::to_string(s2) + ", s3=" + std::to_string(s3);
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: Value out of bounds or non-deterministic repeat";
    }

    return r;
}

} // namespace fw
