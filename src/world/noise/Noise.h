#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace fw {

enum class NoiseType {
    OpenSimplex2,
    OpenSimplex2S,
    Cellular,
    Perlin,
    ValueCubic,
    Value
};

enum class FractalType {
    None,
    FBm,
    Ridged,
    PingPong,
    DomainWarpProgressive,
    DomainWarpIndependent
};

class Noise {
public:
    Noise(int seed = 1337);
    ~Noise();

    // Rule of Five (MEM-02: Strict ownership semantics & memory safety)
    Noise(const Noise& other);
    Noise& operator=(const Noise& other);
    Noise(Noise&& other) noexcept;
    Noise& operator=(Noise&& other) noexcept;

    bool IsValid() const;

    void SetSeed(int seed);
    void SetNoiseType(NoiseType type);
    
    void SetFrequency(float frequency);
    
    // Fractal settings
    void SetFractalType(FractalType type);
    void SetFractalOctaves(int octaves);
    void SetFractalLacunarity(float lacunarity);
    void SetFractalGain(float gain); // Persistance

    void SetDomainWarpType(int type); // Using int to abstract FNL domain warp type if needed, or we can expose it better.
    void SetDomainWarpAmp(float amp);
    void DomainWarp(float& x, float& y);
    void DomainWarp(float& x, float& y, float& z);

    // Get Noise
    float GetNoise(float x, float y);
    float GetNoise(float x, float y, float z);

private:
    class FastNoiseLiteImpl;
    std::unique_ptr<FastNoiseLiteImpl> m_impl;
};

} // namespace fw
