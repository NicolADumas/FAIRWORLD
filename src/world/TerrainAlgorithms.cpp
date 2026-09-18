#include "TerrainAlgorithms.h"
#include "noise/Noise.h"

namespace fw {

void GeneratePlains(const TerrainAlgorithmContext& ctx) {
    Noise plainsNoise(ctx.context.planetSeed);
    plainsNoise.SetNoiseType(NoiseType::OpenSimplex2);
    plainsNoise.SetFractalType(FractalType::FBm);
    plainsNoise.SetFractalOctaves(ctx.rules.rules.height.octaves);
    plainsNoise.SetFrequency(ctx.rules.rules.height.frequency);

    const auto& hr = ctx.rules.rules.height;

    for (int z = 0; z < ctx.context.voxelResolutionZ; ++z) {
        for (int x = 0; x < ctx.context.voxelResolutionX; ++x) {
            int idx2D = z * ctx.context.voxelResolutionX + x;
            glm::vec3 pos = ctx.solver->GetVoxelSpherePos(ctx.context, x, 0, z);

            // Very low amplitude, smooth rolling shapes
            float n = plainsNoise.GetNoise(pos.x * hr.macroScale, pos.y * hr.macroScale, pos.z * hr.macroScale);
            
            // Map -1..1 to 0..1 for easier height reasoning
            float normalizedNoise = (n + 1.0f) * 0.5f;
            
            // Plains: small amplitude variation, no ridges or sharp valleys
            float finalHeight = hr.baseHeight + (normalizedNoise * hr.amplitude * 0.5f);
            
            ctx.workspace.surfaceHeights[idx2D] = finalHeight;
            
            // Write debug fields if needed
            ctx.workspace.macroField[idx2D] = n;
            ctx.workspace.regionalField[idx2D] = 0.0f;
            ctx.workspace.detailField[idx2D] = 0.0f;
        }
    }
}

void GenerateMountains(const TerrainAlgorithmContext& ctx) {
    Noise mountainNoise(ctx.context.planetSeed);
    mountainNoise.SetNoiseType(NoiseType::OpenSimplex2);
    mountainNoise.SetFractalType(FractalType::Ridged);
    mountainNoise.SetFractalOctaves(ctx.rules.rules.height.octaves);
    mountainNoise.SetFrequency(ctx.rules.rules.height.frequency);
    mountainNoise.SetFractalGain(ctx.rules.rules.height.persistence);

    const auto& hr = ctx.rules.rules.height;

    for (int z = 0; z < ctx.context.voxelResolutionZ; ++z) {
        for (int x = 0; x < ctx.context.voxelResolutionX; ++x) {
            int idx2D = z * ctx.context.voxelResolutionX + x;
            glm::vec3 pos = ctx.solver->GetVoxelSpherePos(ctx.context, x, 0, z);

            float n = mountainNoise.GetNoise(pos.x * hr.macroScale, pos.y * hr.macroScale, pos.z * hr.macroScale);
            float normalizedNoise = (n + 1.0f) * 0.5f;

            // Ridged fractal is usually returned already ridged in FNL, or we can use ridge strength
            // FNL Ridged returns values between -1 and 1. Peaks are near 1, valleys near -1.
            float ridgeInfluence = glm::mix(1.0f, normalizedNoise, hr.ridgeStrength);
            
            float finalHeight = hr.baseHeight + (normalizedNoise * hr.amplitude * ridgeInfluence);
            
            ctx.workspace.surfaceHeights[idx2D] = finalHeight;
            
            ctx.workspace.macroField[idx2D] = n;
            ctx.workspace.ridgeField[idx2D] = ridgeInfluence;
        }
    }
}

void GenerateDunes(const TerrainAlgorithmContext& ctx) {
    Noise duneNoise(ctx.context.planetSeed);
    duneNoise.SetNoiseType(NoiseType::OpenSimplex2);
    duneNoise.SetFractalType(FractalType::DomainWarpProgressive);
    duneNoise.SetDomainWarpAmp(30.0f);
    duneNoise.SetFrequency(ctx.rules.rules.height.frequency);

    const auto& hr = ctx.rules.rules.height;

    for (int z = 0; z < ctx.context.voxelResolutionZ; ++z) {
        for (int x = 0; x < ctx.context.voxelResolutionX; ++x) {
            int idx2D = z * ctx.context.voxelResolutionX + x;
            glm::vec3 pos = ctx.solver->GetVoxelSpherePos(ctx.context, x, 0, z);

            float wx = pos.x * hr.macroScale;
            float wy = pos.y * hr.macroScale;
            float wz = pos.z * hr.macroScale;

            duneNoise.DomainWarp(wx, wy, wz);
            
            // After domain warp, evaluate a simple directional sine wave
            // or another noise to form dune ridges.
            float duneValue = std::sin(wx * hr.frequency + wz * hr.frequency);
            
            // Sharpen the peaks of the sine wave to look like dunes
            duneValue = 1.0f - std::abs(duneValue);
            // Power curve to shape the dune peak
            duneValue = std::pow(duneValue, 2.0f);

            float finalHeight = hr.baseHeight + (duneValue * hr.amplitude);
            
            ctx.workspace.surfaceHeights[idx2D] = finalHeight;
            ctx.workspace.macroField[idx2D] = duneValue;
        }
    }
}

void GenerateHills(const TerrainAlgorithmContext& ctx) {
    // Similar to Plains but higher amplitude/frequency
    GeneratePlains(ctx); 
}

} // namespace fw
