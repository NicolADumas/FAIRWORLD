#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

namespace fw {

    // Forward declaration of RegionShape
    enum class RegionShape : int;

    struct ShapeMath {
        // Evaluates the exact (or very close approximate) Signed Distance Field for a 2D shape.
        // localPos: the point to evaluate, relative to the center of the shape (world space).
        // halfExtents: the half-width and half-height of the bounding box (world space).
        static float EvaluateSDF(RegionShape shape, glm::vec2 localPos, glm::vec2 halfExtents) {
            glm::vec2 p = glm::abs(localPos);
            
            // Cast to int to avoid including MapDocument.h and creating circular dependency
            switch (static_cast<int>(shape)) {
                case 1: { // Circle
                    if (halfExtents.x <= 0 || halfExtents.y <= 0) return 0.0f;
                    glm::vec2 normP = p / halfExtents;
                    return (glm::length(normP) - 1.0f) * std::min(halfExtents.x, halfExtents.y);
                }
                case 2: { // Rhombus
                    if (halfExtents.x <= 0 || halfExtents.y <= 0) return 0.0f;
                    // Normal of the edge in the first quadrant
                    glm::vec2 n = glm::normalize(glm::vec2(halfExtents.y, halfExtents.x));
                    // Distance from point to the line forming the edge
                    return glm::dot(p, n) - (halfExtents.x * halfExtents.y) / glm::length(glm::vec2(halfExtents.y, halfExtents.x));
                }
                case 3: { // Star (4-pointed)
                    if (halfExtents.x <= 0 || halfExtents.y <= 0) return 0.0f;
                    glm::vec2 normP = localPos / halfExtents; // Keep sign for atan2
                    float angle = std::atan2(normP.y, normP.x);
                    float r = glm::length(normP);
                    // Base radius: tips at 1.0 (angle 0, pi/2), inner corners at 0.3 (angle pi/4)
                    float boundaryR = 0.65f + 0.35f * std::cos(4.0f * angle);
                    return (r - boundaryR) * std::min(halfExtents.x, halfExtents.y);
                }
                case 0: // Rectangle
                default: {
                    glm::vec2 d = p - halfExtents;
                    return glm::length(glm::max(d, glm::vec2(0.0f))) + std::min(std::max(d.x, d.y), 0.0f);
                }
            }
        }

        // Generates boundary vertices for rendering in the Editor, perfectly matching the SDF geometry.
        // center: world center of the shape
        // halfExtents: half-width and half-height (world space)
        static void GenerateBoundaryVertices(RegionShape shape, glm::vec2 center, glm::vec2 halfExtents, std::vector<glm::vec2>& outVertices) {
            outVertices.clear();
            
            switch (static_cast<int>(shape)) {
                case 0: { // Rectangle
                    outVertices.push_back(center + glm::vec2(-halfExtents.x, -halfExtents.y));
                    outVertices.push_back(center + glm::vec2( halfExtents.x, -halfExtents.y));
                    outVertices.push_back(center + glm::vec2( halfExtents.x,  halfExtents.y));
                    outVertices.push_back(center + glm::vec2(-halfExtents.x,  halfExtents.y));
                    break;
                }
                case 1: { // Circle
                    const int segments = 32;
                    for (int i = 0; i < segments; ++i) {
                        float angle = i * (glm::pi<float>() * 2.0f / segments);
                        outVertices.push_back(center + glm::vec2(std::cos(angle) * halfExtents.x, std::sin(angle) * halfExtents.y));
                    }
                    break;
                }
                case 2: { // Rhombus
                    outVertices.push_back(center + glm::vec2(0.0f, -halfExtents.y));
                    outVertices.push_back(center + glm::vec2(halfExtents.x, 0.0f));
                    outVertices.push_back(center + glm::vec2(0.0f, halfExtents.y));
                    outVertices.push_back(center + glm::vec2(-halfExtents.x, 0.0f));
                    break;
                }
                case 3: { // Star (4-pointed)
                    const int segments = 16;
                    for (int i = 0; i < segments; ++i) {
                        float angle = i * (glm::pi<float>() * 2.0f / segments);
                        float radius = 0.65f + 0.35f * std::cos(4.0f * angle);
                        outVertices.push_back(center + glm::vec2(std::cos(angle) * halfExtents.x * radius, std::sin(angle) * halfExtents.y * radius));
                    }
                    break;
                }
            }
        }
    };

} // namespace fw
