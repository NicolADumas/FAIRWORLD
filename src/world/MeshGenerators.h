#pragma once
#include "ForgeComponents.h"
#include <string>

struct SharedContext;

namespace fw {

class MeshGenerators {
public:
    static MeshComponent MakeCube(float size = 1.0f);
    static MeshComponent MakeSphere(int segs = 16, int rings = 8, float r = 1.0f);
    static MeshComponent MakeSuperSphere(float n = 2.0f, float radius = 1.0f, int resolution = 24);
    static MeshComponent MakeVoxelPreview(int blockId, SharedContext* ctx);
    static MeshComponent MakeGridBox(int width, int height, int depth, float thickness = 0.05f);
    // Obelisco alto per i marker spawn-point: base 2x2, altezza 10
    static MeshComponent MakeObelisk(float baseW = 2.0f, float baseD = 2.0f, float height = 10.0f);
};

} // namespace fw
