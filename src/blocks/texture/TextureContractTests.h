#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace fw {

struct TextureTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class TextureContractTests {
public:
    // Esegue i 10 test di conformità contrattuale deterministici BM-TX2 (TX-1 .. TX-10)
    static std::vector<TextureTestResult> RunContractTests();
};

} // namespace fw
