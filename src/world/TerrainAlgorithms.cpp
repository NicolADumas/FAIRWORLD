#include "TerrainAlgorithms.h"
#include "noise/Noise.h"

namespace fw {

void GeneratePlains(const TerrainAlgorithmContext& ctx) {
    const auto& hr = ctx.rules.rules.height;
    const auto* spec = std::get_if<PlainsRules>(&hr.specialized);
    int octaves = spec ? spec->octaves : 4;

    Noise plainsNoise(ctx.context.planetSeed);
    plainsNoise.SetNoiseType(NoiseType::OpenSimplex2);
    plainsNoise.SetFractalType(FractalType::FBm);
    plainsNoise.SetFractalOctaves(octaves);
    plainsNoise.SetFrequency(hr.common.frequency);

    for (int z = 0; z < ctx.context.voxelResolutionZ; ++z) {
        for (int x = 0; x < ctx.context.voxelResolutionX; ++x) {
            int idx2D = z * ctx.context.voxelResolutionX + x;
            glm::vec3 pos = ctx.solver->GetVoxelSpherePos(ctx.context, x, 0, z);

            // World-coordinate based sampling
            float sampleX = pos.x * hr.common.macroScale;
            float sampleY = pos.y * hr.common.macroScale;
            float sampleZ = pos.z * hr.common.macroScale;

            // Base coherent fractal field (approx range [-1, 1])
            float n = plainsNoise.GetNoise(sampleX, sampleY, sampleZ);
            
            // Normalization to [0, 1]
            float normalizedNoise = glm::clamp((n + 1.0f) * 0.5f, 0.0f, 1.0f);
            
            // Plains response: smooth hermite interpolation flattens valleys and peaks, creating broad plains
            float plainsResponse = normalizedNoise * normalizedNoise * (3.0f - 2.0f * normalizedNoise);

            // Transparent amplitude relationship: amplitude is the exact max vertical variation
            float finalHeight = hr.common.baseHeight + (hr.common.amplitude * plainsResponse);
            
            ctx.workspace.surfaceHeights[idx2D] = finalHeight;
            
            ctx.workspace.macroField[idx2D] = n;
            ctx.workspace.regionalField[idx2D] = 0.0f;
            ctx.workspace.detailField[idx2D] = 0.0f;
        }
    }
}

void GenerateMountains(const TerrainAlgorithmContext& ctx) {
    const auto& hr = ctx.rules.rules.height;
    const auto* spec = std::get_if<MountainRules>(&hr.specialized);
    int octaves = spec ? spec->octaves : 4;
    float persistence = spec ? spec->persistence : 0.5f;
    float ridgeStrength = spec ? spec->ridgeStrength : 0.0f;

    Noise mountainNoise(ctx.context.planetSeed);
    mountainNoise.SetNoiseType(NoiseType::OpenSimplex2);
    mountainNoise.SetFractalType(FractalType::Ridged);
    mountainNoise.SetFractalOctaves(octaves);
    mountainNoise.SetFrequency(hr.common.frequency);
    mountainNoise.SetFractalGain(persistence);

    for (int z = 0; z < ctx.context.voxelResolutionZ; ++z) {
        for (int x = 0; x < ctx.context.voxelResolutionX; ++x) {
            int idx2D = z * ctx.context.voxelResolutionX + x;
            glm::vec3 pos = ctx.solver->GetVoxelSpherePos(ctx.context, x, 0, z);

            float n = mountainNoise.GetNoise(pos.x * hr.common.macroScale, pos.y * hr.common.macroScale, pos.z * hr.common.macroScale);
            float normalizedNoise = (n + 1.0f) * 0.5f;

            float ridgeInfluence = glm::mix(1.0f, normalizedNoise, ridgeStrength);
            float finalHeight = hr.common.baseHeight + (normalizedNoise * hr.common.amplitude * ridgeInfluence);
            
            ctx.workspace.surfaceHeights[idx2D] = finalHeight;
            
            ctx.workspace.macroField[idx2D] = n;
            ctx.workspace.ridgeField[idx2D] = ridgeInfluence;
        }
    }
}

void GenerateDunes(const TerrainAlgorithmContext& ctx) {
    const auto& hr = ctx.rules.rules.height;

    Noise duneNoise(ctx.context.planetSeed);
    duneNoise.SetNoiseType(NoiseType::OpenSimplex2);
    duneNoise.SetFractalType(FractalType::DomainWarpProgressive);
    duneNoise.SetDomainWarpAmp(30.0f);
    duneNoise.SetFrequency(hr.common.frequency);

    for (int z = 0; z < ctx.context.voxelResolutionZ; ++z) {
        for (int x = 0; x < ctx.context.voxelResolutionX; ++x) {
            int idx2D = z * ctx.context.voxelResolutionX + x;
            glm::vec3 pos = ctx.solver->GetVoxelSpherePos(ctx.context, x, 0, z);

            float wx = pos.x * hr.common.macroScale;
            float wy = pos.y * hr.common.macroScale;
            float wz = pos.z * hr.common.macroScale;

            duneNoise.DomainWarp(wx, wy, wz);
            
            float duneValue = std::sin(wx * hr.common.frequency + wz * hr.common.frequency);
            duneValue = 1.0f - std::abs(duneValue);
            duneValue = std::pow(duneValue, 2.0f);

            float finalHeight = hr.common.baseHeight + (duneValue * hr.common.amplitude);
            
            ctx.workspace.surfaceHeights[idx2D] = finalHeight;
            ctx.workspace.macroField[idx2D] = duneValue;
        }
    }
}

void GenerateHills(const TerrainAlgorithmContext& ctx) {
    const auto& hr = ctx.rules.rules.height;
    const auto* spec = std::get_if<HillsRules>(&hr.specialized);
    int octaves = spec ? spec->octaves : 4;
    float roundness = spec ? spec->roundness : 0.5f;

    Noise rawNoise(ctx.context.planetSeed);
    rawNoise.SetNoiseType(NoiseType::OpenSimplex2);
    rawNoise.SetFractalType(FractalType::None); 
    rawNoise.SetFrequency(1.0f); // Frequency mapped manually

    // Map roundness [0, 1] to k [0.1, 0.001]
    // A high roundness creates very distinct hills with sharp but mathematically continuous valleys.
    // A low roundness uses a larger k, which flattens the bottom of the valleys, making the terrain broader and softer.
    float clampedRoundness = glm::clamp(roundness, 0.0f, 1.0f);
    float k = glm::mix(0.1f, 0.001f, clampedRoundness);
    
    // maxAbs is the maximum possible value of the smoothed absolute function, occurring when v = 1 or -1.
    float maxAbs = std::sqrt(1.0f + k) - std::sqrt(k);

    for (int z = 0; z < ctx.context.voxelResolutionZ; ++z) {
        for (int x = 0; x < ctx.context.voxelResolutionX; ++x) {
            int idx2D = z * ctx.context.voxelResolutionX + x;
            glm::vec3 pos = ctx.solver->GetVoxelSpherePos(ctx.context, x, 0, z);

            float sampleX = pos.x * hr.common.macroScale;
            float sampleY = pos.y * hr.common.macroScale;
            float sampleZ = pos.z * hr.common.macroScale;

            float freq = hr.common.frequency;
            float amp = 1.0f;
            float n = 0.0f;
            float maxAmplitude = 0.0f;
            float firstOctaveRaw = 0.0f;

            for (int i = 0; i < octaves; ++i) {
                // Get base noise in [-1, 1]
                float v = rawNoise.GetNoise(sampleX * freq, sampleY * freq, sampleZ * freq);
                
                if (i == 0) {
                    firstOctaveRaw = v;
                }
                
                // Smoothed billow response: 
                float smoothedAbs = std::sqrt(v * v + k) - std::sqrt(k);
                float octaveVal = smoothedAbs / maxAbs; // normalized to [0, 1] approx
                
                n += octaveVal * amp;
                maxAmplitude += amp;
                
                freq *= 2.0f;
                amp *= 0.5f;
            }

            // Normalize accumulation to [0, 1]
            float hillsResponse = n / maxAmplitude;

            // Apply transparent amplitude
            float finalHeight = hr.common.baseHeight + (hr.common.amplitude * hillsResponse);
            
            ctx.workspace.surfaceHeights[idx2D] = finalHeight;
            
            // Store fields for diagnostics
            ctx.workspace.macroField[idx2D] = hillsResponse;
            ctx.workspace.detailField[idx2D] = firstOctaveRaw; // Raw noise for diagnostic output
            ctx.workspace.regionalField[idx2D] = 0.0f;
        }
    }
}

} // namespace fw
