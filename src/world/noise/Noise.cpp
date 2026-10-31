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

Noise::Noise(int seed) 
    : m_impl(std::make_unique<FastNoiseLiteImpl>()) {
    m_impl->fnl.SetSeed(seed);
}

Noise::~Noise() = default;

Noise::Noise(const Noise& other)
    : m_impl(other.m_impl ? std::make_unique<FastNoiseLiteImpl>(*other.m_impl) : nullptr) {
}

Noise& Noise::operator=(const Noise& other) {
    if (this != &other) {
        if (other.m_impl) {
            if (!m_impl) {
                m_impl = std::make_unique<FastNoiseLiteImpl>(*other.m_impl);
            } else {
                *m_impl = *other.m_impl;
            }
        } else {
            m_impl.reset();
        }
    }
    return *this;
}

Noise::Noise(Noise&& other) noexcept = default;

Noise& Noise::operator=(Noise&& other) noexcept = default;

bool Noise::IsValid() const {
    return m_impl != nullptr;
}

void Noise::SetSeed(int seed) {
    if (m_impl) {
        m_impl->fnl.SetSeed(seed);
    }
}

void Noise::SetNoiseType(NoiseType type) {
    if (!m_impl) return;
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
    if (m_impl) {
        m_impl->fnl.SetFrequency(frequency);
    }
}

void Noise::SetFractalType(FractalType type) {
    if (!m_impl) return;
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
    if (m_impl) {
        m_impl->fnl.SetFractalOctaves(octaves);
    }
}

void Noise::SetFractalLacunarity(float lacunarity) {
    if (m_impl) {
        m_impl->fnl.SetFractalLacunarity(lacunarity);
    }
}

void Noise::SetFractalGain(float gain) {
    if (m_impl) {
        m_impl->fnl.SetFractalGain(gain);
    }
}

void Noise::SetDomainWarpType(int type) {
    // Left empty or mapped to FNL domain warp types if required later
}

void Noise::SetDomainWarpAmp(float amp) {
    if (m_impl) {
        m_impl->fnl.SetDomainWarpAmp(amp);
    }
}

void Noise::DomainWarp(float& x, float& y) {
    if (m_impl) {
        m_impl->fnl.DomainWarp(x, y);
    }
}

void Noise::DomainWarp(float& x, float& y, float& z) {
    if (m_impl) {
        m_impl->fnl.DomainWarp(x, y, z);
    }
}

float Noise::GetNoise(float x, float y) {
    return m_impl ? m_impl->fnl.GetNoise(x, y) : 0.0f;
}

float Noise::GetNoise(float x, float y, float z) {
    return m_impl ? m_impl->fnl.GetNoise(x, y, z) : 0.0f;
}

} // namespace fw
