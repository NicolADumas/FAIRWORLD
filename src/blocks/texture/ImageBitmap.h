#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace fw {

// =============================================================================
// BM-TX2.2 — IMAGE BITMAP: CONTRATTO DATI CPU PURO
// =============================================================================

enum class TexturePixelFormat : uint8_t {
    RGBA8 = 4,   // 4 canali: Red, Green, Blue, Alpha (32 bit per pixel)
    R8 = 1       // 1 canale: scala di grigi / maschera alfanumerica (8 bit per pixel)
};

enum class TextureColorSpace : uint8_t {
    sRGB = 0,    // Gamma corretta (~2.2), adatta per Albedo/BaseColor ed Emissive
    Linear = 1   // Dati fisici lineari (1.0), obbligatorio per Normal, Roughness, Metallic, AO, ORM
};

enum class TextureWrapMode : uint8_t {
    Repeat = 0,
    ClampToEdge = 1,
    MirroredRepeat = 2
};

enum class TextureFilterMode : uint8_t {
    Nearest = 0,
    Bilinear = 1
};

class ImageBitmap {
public:
    ImageBitmap();
    ImageBitmap(uint32_t width, uint32_t height, 
                TexturePixelFormat format = TexturePixelFormat::RGBA8, 
                TextureColorSpace colorSpace = TextureColorSpace::sRGB, 
                const glm::vec4& fillColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    ~ImageBitmap() = default;

    // Semantica di copia e spostamento profonda ed esplicita
    ImageBitmap(const ImageBitmap& other);
    ImageBitmap(ImageBitmap&& other) noexcept;
    ImageBitmap& operator=(const ImageBitmap& other);
    ImageBitmap& operator=(ImageBitmap&& other) noexcept;

    // --- Validazione e Dimensioni ---
    bool IsValid() const;
    bool IsEmpty() const;
    void Clear();

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    TexturePixelFormat GetPixelFormat() const { return m_format; }
    TextureColorSpace GetColorSpace() const { return m_colorSpace; }
    uint32_t GetBytesPerPixel() const { return static_cast<uint32_t>(m_format); }
    size_t GetTotalBytes() const { return m_pixels.size(); }

    const uint8_t* GetData() const { return m_pixels.data(); }
    uint8_t* GetDataMutable() { return m_pixels.data(); }
    const std::vector<uint8_t>& GetPixelVector() const { return m_pixels; }

    static bool ValidateDimensions(uint32_t width, uint32_t height, uint32_t maxDimension = 4096);

    // --- Accesso e Modifica Pixel ---
    glm::vec4 GetPixel(uint32_t x, uint32_t y) const;
    bool SetPixel(uint32_t x, uint32_t y, const glm::vec4& color);

    // Campionamento continuo normalizzato [0, 1]
    glm::vec4 SampleNearest(glm::vec2 uv, TextureWrapMode wrap = TextureWrapMode::Repeat) const;
    glm::vec4 SampleBilinear(glm::vec2 uv, TextureWrapMode wrap = TextureWrapMode::Repeat) const;

    void Fill(const glm::vec4& color);

    // Blit rettangolare tra bitmap
    bool BlitRect(const ImageBitmap& src, uint32_t dstX, uint32_t dstY, 
                  uint32_t srcX, uint32_t srcY, uint32_t rectW, uint32_t rectH);

    // --- Trasformazioni & Resampling ---
    ImageBitmap Resample(uint32_t newWidth, uint32_t newHeight, 
                         TextureFilterMode filter = TextureFilterMode::Bilinear) const;

    void ConvertColorSpace(TextureColorSpace targetColorSpace);
    void ConvertPixelFormat(TexturePixelFormat targetFormat);

    // --- Fingerprinting & Revision Tracking ---
    uint64_t GetRevision() const { return m_revision; }
    void IncrementRevision() { m_revision++; }

    // FNV-1a 64-bit deterministico calcolato su metadati e buffer pixel
    uint64_t ComputeContentHash() const;

    // --- Decodifica e Codifica (PNG / Raw Buffer) ---
    bool DecodeFromMemory(const uint8_t* data, size_t sizeBytes, 
                          TextureColorSpace assumedColorSpace = TextureColorSpace::sRGB);
    std::vector<uint8_t> EncodeToPngMemory() const;

    bool LoadFromFile(const std::string& filepath, 
                      TextureColorSpace assumedColorSpace = TextureColorSpace::sRGB);
    bool SaveToFile(const std::string& filepath) const;

    // Conversioni gamma veloci
    static float sRGBToLinear(float val);
    static float LinearTosRGB(float val);

private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    TexturePixelFormat m_format = TexturePixelFormat::RGBA8;
    TextureColorSpace m_colorSpace = TextureColorSpace::sRGB;
    std::vector<uint8_t> m_pixels;
    uint64_t m_revision = 1;

    void AllocateBuffer();
};

} // namespace fw
