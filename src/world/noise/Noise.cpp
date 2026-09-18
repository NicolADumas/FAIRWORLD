#include "Noise.h"

// Disable warnings for third party headers if necessary
#pragma warning(push, 0)
#define FASTNOISELITE_IMPLEMENTATION
#include "../../../third_party/FastNoiseLite.h"
#pragma warning(pop)

namespace fw {

class Noise::FastNoiseLiteImpl {
public:
    FastNoiseLite fnl;
};

Noise::Noise(int seed) {
    m_impl = new FastNoiseLiteImpl();
    m_impl->fnl.SetSeed(seed);
}

Noise::~Noise() {
    delete m_impl;
}

void Noise::SetSeed(int seed) {
    m_impl->fnl.SetSeed(seed);
}

void Noise::SetNoiseType(NoiseType type) {
    switch (type) {
        case NoiseType::OpenSimplex2: m_impl->fnl.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2); break;
        case NoiseType::OpenSimplex2S: m_impl->fnl.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S); break;
        case NoiseType::Cellular: m_impl->fnl.SetNoiseType(FastNoiseLite::NoiseType_Cellular); break;
        case NoiseType::Perlin: m_impl->fnl.SetNoiseType(FastNoiseLite::NoiseType_Perlin); break;
        case NoiseType::ValueCubic: m_impl->fnl.SetNoiseType(FastNoiseLite::NoiseType_ValueCubic); break;
        case NoiseType::Value: m_impl->fnl.SetNoiseType(FastNoiseLite::NoiseType_Value); break;
    }
}

void Noise::SetFrequency(float frequency) {
    m_impl->fnl.SetFrequency(frequency);
}

void Noise::SetFractalType(FractalType type) {
    switch (type) {
        case FractalType::None: m_impl->fnl.SetFractalType(FastNoiseLite::FractalType_None); break;
        case FractalType::FBm: m_impl->fnl.SetFractalType(FastNoiseLite::FractalType_FBm); break;
        case FractalType::Ridged: m_impl->fnl.SetFractalType(FastNoiseLite::FractalType_Ridged); break;
        case FractalType::PingPong: m_impl->fnl.SetFractalType(FastNoiseLite::FractalType_PingPong); break;
        case FractalType::DomainWarpProgressive: m_impl->fnl.SetFractalType(FastNoiseLite::FractalType_DomainWarpProgressive); break;
        case FractalType::DomainWarpIndependent: m_impl->fnl.SetFractalType(FastNoiseLite::FractalType_DomainWarpIndependent); break;
    }
}

void Noise::SetFractalOctaves(int octaves) {
    m_impl->fnl.SetFractalOctaves(octaves);
}

void Noise::SetFractalLacunarity(float lacunarity) {
    m_impl->fnl.SetFractalLacunarity(lacunarity);
}

void Noise::SetFractalGain(float gain) {
    m_impl->fnl.SetFractalGain(gain);
}

void Noise::SetDomainWarpType(int type) {
    // Left empty or mapped to FNL domain warp types if required later
}

void Noise::SetDomainWarpAmp(float amp) {
    m_impl->fnl.SetDomainWarpAmp(amp);
}

void Noise::DomainWarp(float& x, float& y) {
    m_impl->fnl.DomainWarp(x, y);
}

void Noise::DomainWarp(float& x, float& y, float& z) {
    m_impl->fnl.DomainWarp(x, y, z);
}

float Noise::GetNoise(float x, float y) {
    return m_impl->fnl.GetNoise(x, y);
}

float Noise::GetNoise(float x, float y, float z) {
    return m_impl->fnl.GetNoise(x, y, z);
}

} // namespace fw
