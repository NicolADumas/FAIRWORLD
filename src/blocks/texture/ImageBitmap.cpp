#include "pch.h"
#include "ImageBitmap.h"
#include <iostream>
#include <fstream>
#include <limits>
#include <cstring>

// Librerie di decodifica/codifica già disponibili nel progetto
#include "stb_image.h"
#include "stb_image_write.h"

namespace fw {

// =============================================================================
// BM-TX2.2: IMAGE BITMAP IMPLEMENTATION
// =============================================================================

ImageBitmap::ImageBitmap()
    : m_width(0), m_height(0), m_format(TexturePixelFormat::RGBA8), m_colorSpace(TextureColorSpace::sRGB), m_revision(1) {
}

ImageBitmap::ImageBitmap(uint32_t width, uint32_t height, 
                         TexturePixelFormat format, 
                         TextureColorSpace colorSpace, 
                         const glm::vec4& fillColor)
    : m_width(width), m_height(height), m_format(format), m_colorSpace(colorSpace), m_revision(1) {
    if (ValidateDimensions(width, height)) {
        AllocateBuffer();
        Fill(fillColor);
    } else {
        m_width = 0;
        m_height = 0;
    }
}

ImageBitmap::ImageBitmap(const ImageBitmap& other)
    : m_width(other.m_width),
      m_height(other.m_height),
      m_format(other.m_format),
      m_colorSpace(other.m_colorSpace),
      m_pixels(other.m_pixels),
      m_revision(other.m_revision) {
}

ImageBitmap::ImageBitmap(ImageBitmap&& other) noexcept
    : m_width(other.m_width),
      m_height(other.m_height),
      m_format(other.m_format),
      m_colorSpace(other.m_colorSpace),
      m_pixels(std::move(other.m_pixels)),
      m_revision(other.m_revision) {
    other.m_width = 0;
    other.m_height = 0;
}

ImageBitmap& ImageBitmap::operator=(const ImageBitmap& other) {
    if (this != &other) {
        m_width = other.m_width;
        m_height = other.m_height;
        m_format = other.m_format;
        m_colorSpace = other.m_colorSpace;
        m_pixels = other.m_pixels;
        m_revision = other.m_revision + 1; // Incrementa revisione su copia
    }
    return *this;
}

ImageBitmap& ImageBitmap::operator=(ImageBitmap&& other) noexcept {
    if (this != &other) {
        m_width = other.m_width;
        m_height = other.m_height;
        m_format = other.m_format;
        m_colorSpace = other.m_colorSpace;
        m_pixels = std::move(other.m_pixels);
        m_revision = other.m_revision + 1;
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

bool ImageBitmap::IsValid() const {
    if (m_width == 0 || m_height == 0) return false;
    size_t expectedBytes = static_cast<size_t>(m_width) * m_height * GetBytesPerPixel();
    return m_pixels.size() == expectedBytes;
}

bool ImageBitmap::IsEmpty() const {
    return m_width == 0 || m_height == 0 || m_pixels.empty();
}

void ImageBitmap::Clear() {
    m_width = 0;
    m_height = 0;
    m_pixels.clear();
    IncrementRevision();
}

bool ImageBitmap::ValidateDimensions(uint32_t width, uint32_t height, uint32_t maxDimension) {
    if (width == 0 || height == 0) return false;
    if (width > maxDimension || height > maxDimension) return false;
    // Protezione da overflow aritmetico a 64-bit
    uint64_t totalPixels = static_cast<uint64_t>(width) * height;
    if (totalPixels > (std::numeric_limits<size_t>::max() / 4)) return false;
    return true;
}

void ImageBitmap::AllocateBuffer() {
    size_t totalBytes = static_cast<size_t>(m_width) * m_height * GetBytesPerPixel();
    m_pixels.assign(totalBytes, 0);
}

glm::vec4 ImageBitmap::GetPixel(uint32_t x, uint32_t y) const {
    if (!IsValid() || x >= m_width || y >= m_height) {
        return glm::vec4(0.0f);
    }

    size_t bpp = GetBytesPerPixel();
    size_t idx = (static_cast<size_t>(y) * m_width + x) * bpp;

    if (m_format == TexturePixelFormat::RGBA8) {
        float r = m_pixels[idx + 0] / 255.0f;
        float g = m_pixels[idx + 1] / 255.0f;
        float b = m_pixels[idx + 2] / 255.0f;
        float a = m_pixels[idx + 3] / 255.0f;
        return glm::vec4(r, g, b, a);
    } else { // R8
        float r = m_pixels[idx] / 255.0f;
        return glm::vec4(r, r, r, 1.0f);
    }
}

bool ImageBitmap::SetPixel(uint32_t x, uint32_t y, const glm::vec4& color) {
    if (!IsValid() || x >= m_width || y >= m_height) return false;

    size_t bpp = GetBytesPerPixel();
    size_t idx = (static_cast<size_t>(y) * m_width + x) * bpp;

    auto toByte = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
    };

    if (m_format == TexturePixelFormat::RGBA8) {
        m_pixels[idx + 0] = toByte(color.r);
        m_pixels[idx + 1] = toByte(color.g);
        m_pixels[idx + 2] = toByte(color.b);
        m_pixels[idx + 3] = toByte(color.a);
    } else { // R8
        m_pixels[idx] = toByte(color.r);
    }
    return true;
}

void ImageBitmap::Fill(const glm::vec4& color) {
    if (!IsValid()) return;

    size_t bpp = GetBytesPerPixel();
    auto toByte = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
    };

    uint8_t r = toByte(color.r);
    uint8_t g = toByte(color.g);
    uint8_t b = toByte(color.b);
    uint8_t a = toByte(color.a);

    if (m_format == TexturePixelFormat::RGBA8) {
        for (size_t i = 0; i < m_pixels.size(); i += 4) {
            m_pixels[i + 0] = r;
            m_pixels[i + 1] = g;
            m_pixels[i + 2] = b;
            m_pixels[i + 3] = a;
        }
    } else {
        std::fill(m_pixels.begin(), m_pixels.end(), r);
    }
    IncrementRevision();
}

static float WrapCoord(float coord, TextureWrapMode wrap) {
    if (wrap == TextureWrapMode::Repeat) {
        float wrapped = std::fmod(coord, 1.0f);
        if (wrapped < 0.0f) wrapped += 1.0f;
        return wrapped;
    }
    if (wrap == TextureWrapMode::MirroredRepeat) {
        float c = std::fabs(coord);
        int period = static_cast<int>(std::floor(c));
        float rem = c - static_cast<float>(period);
        return (period % 2 == 0) ? rem : (1.0f - rem);
    }
    return std::clamp(coord, 0.0f, 1.0f);
}

glm::vec4 ImageBitmap::SampleNearest(glm::vec2 uv, TextureWrapMode wrap) const {
    if (!IsValid()) return glm::vec4(0.0f);

    float u = WrapCoord(uv.x, wrap);
    float v = WrapCoord(uv.y, wrap);

    uint32_t x = std::min(static_cast<uint32_t>(u * m_width), m_width - 1);
    uint32_t y = std::min(static_cast<uint32_t>(v * m_height), m_height - 1);

    return GetPixel(x, y);
}

glm::vec4 ImageBitmap::SampleBilinear(glm::vec2 uv, TextureWrapMode wrap) const {
    if (!IsValid()) return glm::vec4(0.0f);

    float u = WrapCoord(uv.x, wrap) * m_width - 0.5f;
    float v = WrapCoord(uv.y, wrap) * m_height - 0.5f;

    int32_t x0 = static_cast<int32_t>(std::floor(u));
    int32_t y0 = static_cast<int32_t>(std::floor(v));
    int32_t x1 = x0 + 1;
    int32_t y1 = y0 + 1;

    float fx = u - x0;
    float fy = v - y0;

    auto clampOrWrap = [this, wrap](int32_t c, uint32_t maxVal) -> uint32_t {
        if (wrap == TextureWrapMode::Repeat) {
            int32_t m = static_cast<int32_t>(maxVal);
            int32_t res = c % m;
            return static_cast<uint32_t>(res < 0 ? res + m : res);
        }
        return static_cast<uint32_t>(std::clamp(c, 0, static_cast<int32_t>(maxVal - 1)));
    };

    uint32_t cx0 = clampOrWrap(x0, m_width);
    uint32_t cx1 = clampOrWrap(x1, m_width);
    uint32_t cy0 = clampOrWrap(y0, m_height);
    uint32_t cy1 = clampOrWrap(y1, m_height);

    glm::vec4 p00 = GetPixel(cx0, cy0);
    glm::vec4 p10 = GetPixel(cx1, cy0);
    glm::vec4 p01 = GetPixel(cx0, cy1);
    glm::vec4 p11 = GetPixel(cx1, cy1);

    glm::vec4 top = glm::mix(p00, p10, fx);
    glm::vec4 bottom = glm::mix(p01, p11, fx);
    return glm::mix(top, bottom, fy);
}

bool ImageBitmap::BlitRect(const ImageBitmap& src, uint32_t dstX, uint32_t dstY, 
                           uint32_t srcX, uint32_t srcY, uint32_t rectW, uint32_t rectH) {
    if (!IsValid() || !src.IsValid()) return false;
    if (srcX >= src.m_width || srcY >= src.m_height) return false;
    if (dstX >= m_width || dstY >= m_height) return false;

    uint32_t actualW = std::min({rectW, src.m_width - srcX, m_width - dstX});
    uint32_t actualH = std::min({rectH, src.m_height - srcY, m_height - dstY});
    if (actualW == 0 || actualH == 0) return true;

    for (uint32_t r = 0; r < actualH; ++r) {
        for (uint32_t c = 0; c < actualW; ++c) {
            glm::vec4 p = src.GetPixel(srcX + c, srcY + r);
            SetPixel(dstX + c, dstY + r, p);
        }
    }

    IncrementRevision();
    return true;
}

ImageBitmap ImageBitmap::Resample(uint32_t newWidth, uint32_t newHeight, TextureFilterMode filter) const {
    if (!IsValid() || !ValidateDimensions(newWidth, newHeight)) {
        return ImageBitmap();
    }

    ImageBitmap result(newWidth, newHeight, m_format, m_colorSpace);

    for (uint32_t y = 0; y < newHeight; ++y) {
        float v = (y + 0.5f) / static_cast<float>(newHeight);
        for (uint32_t x = 0; x < newWidth; ++x) {
            float u = (x + 0.5f) / static_cast<float>(newWidth);
            glm::vec4 sample = (filter == TextureFilterMode::Bilinear) 
                               ? SampleBilinear(glm::vec2(u, v), TextureWrapMode::ClampToEdge)
                               : SampleNearest(glm::vec2(u, v), TextureWrapMode::ClampToEdge);
            result.SetPixel(x, y, sample);
        }
    }

    return result;
}

float ImageBitmap::sRGBToLinear(float val) {
    val = std::clamp(val, 0.0f, 1.0f);
    return (val <= 0.04045f) ? (val / 12.92f) : std::pow((val + 0.055f) / 1.055f, 2.4f);
}

float ImageBitmap::LinearTosRGB(float val) {
    val = std::clamp(val, 0.0f, 1.0f);
    return (val <= 0.0031308f) ? (val * 12.92f) : (1.055f * std::pow(val, 1.0f / 2.4f) - 0.055f);
}

void ImageBitmap::ConvertColorSpace(TextureColorSpace targetColorSpace) {
    if (!IsValid() || m_colorSpace == targetColorSpace) return;

    if (m_format == TexturePixelFormat::RGBA8) {
        if (targetColorSpace == TextureColorSpace::Linear && m_colorSpace == TextureColorSpace::sRGB) {
            // sRGB -> Linear
            for (size_t i = 0; i < m_pixels.size(); i += 4) {
                float r = sRGBToLinear(m_pixels[i + 0] / 255.0f);
                float g = sRGBToLinear(m_pixels[i + 1] / 255.0f);
                float b = sRGBToLinear(m_pixels[i + 2] / 255.0f);
                m_pixels[i + 0] = static_cast<uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
                m_pixels[i + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
                m_pixels[i + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
                // Alpha rimane sempre lineare
            }
        } else if (targetColorSpace == TextureColorSpace::sRGB && m_colorSpace == TextureColorSpace::Linear) {
            // Linear -> sRGB
            for (size_t i = 0; i < m_pixels.size(); i += 4) {
                float r = LinearTosRGB(m_pixels[i + 0] / 255.0f);
                float g = LinearTosRGB(m_pixels[i + 1] / 255.0f);
                float b = LinearTosRGB(m_pixels[i + 2] / 255.0f);
                m_pixels[i + 0] = static_cast<uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
                m_pixels[i + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
                m_pixels[i + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
            }
        }
    }
    m_colorSpace = targetColorSpace;
    IncrementRevision();
}

void ImageBitmap::ConvertPixelFormat(TexturePixelFormat targetFormat) {
    if (!IsValid() || m_format == targetFormat) return;

    if (m_format == TexturePixelFormat::RGBA8 && targetFormat == TexturePixelFormat::R8) {
        // RGBA8 -> R8 (luminanza standard)
        std::vector<uint8_t> newPixels(m_width * m_height);
        for (size_t i = 0, j = 0; i < m_pixels.size(); i += 4, ++j) {
            float r = m_pixels[i + 0];
            float g = m_pixels[i + 1];
            float b = m_pixels[i + 2];
            newPixels[j] = static_cast<uint8_t>(0.2126f * r + 0.7152f * g + 0.0722f * b + 0.5f);
        }
        m_pixels = std::move(newPixels);
        m_format = TexturePixelFormat::R8;
    } else if (m_format == TexturePixelFormat::R8 && targetFormat == TexturePixelFormat::RGBA8) {
        // R8 -> RGBA8
        std::vector<uint8_t> newPixels(static_cast<size_t>(m_width) * m_height * 4);
        for (size_t i = 0, j = 0; i < m_pixels.size(); ++i, j += 4) {
            uint8_t val = m_pixels[i];
            newPixels[j + 0] = val;
            newPixels[j + 1] = val;
            newPixels[j + 2] = val;
            newPixels[j + 3] = 255;
        }
        m_pixels = std::move(newPixels);
        m_format = TexturePixelFormat::RGBA8;
    }
    IncrementRevision();
}

uint64_t ImageBitmap::ComputeContentHash() const {
    if (!IsValid()) return 0;

    // FNV-1a 64-bit deterministico
    uint64_t hash = 14695981039346656037ULL;
    auto addBytes = [&hash](const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }
    };

    uint32_t meta[4] = {
        m_width,
        m_height,
        static_cast<uint32_t>(m_format),
        static_cast<uint32_t>(m_colorSpace)
    };
    addBytes(reinterpret_cast<const uint8_t*>(meta), sizeof(meta));
    addBytes(m_pixels.data(), m_pixels.size());

    return hash;
}

// --- DECODIFICA & CODIFICA ---

bool ImageBitmap::DecodeFromMemory(const uint8_t* data, size_t sizeBytes, TextureColorSpace assumedColorSpace) {
    if (!data || sizeBytes == 0) {
        Clear();
        return false;
    }

    int w = 0, h = 0, channels = 0;
    // Forza caricamento in 4 canali RGBA
    stbi_uc* decoded = stbi_load_from_memory(data, static_cast<int>(sizeBytes), &w, &h, &channels, STBI_rgb_alpha);
    if (!decoded) {
        Clear();
        return false;
    }

    if (w <= 0 || h <= 0 || !ValidateDimensions(static_cast<uint32_t>(w), static_cast<uint32_t>(h))) {
        stbi_image_free(decoded);
        Clear();
        return false;
    }

    m_width = static_cast<uint32_t>(w);
    m_height = static_cast<uint32_t>(h);
    m_format = TexturePixelFormat::RGBA8;
    m_colorSpace = assumedColorSpace;

    size_t totalBytes = static_cast<size_t>(m_width) * m_height * 4;
    m_pixels.assign(decoded, decoded + totalBytes);

    stbi_image_free(decoded);
    IncrementRevision();
    return true;
}

std::vector<uint8_t> ImageBitmap::EncodeToPngMemory() const {
    if (!IsValid()) return {};

    int len = 0;
    int channels = (m_format == TexturePixelFormat::RGBA8) ? 4 : 1;
    int stride = m_width * channels;

    unsigned char* pngData = stbi_write_png_to_mem(
        m_pixels.data(),
        stride,
        static_cast<int>(m_width),
        static_cast<int>(m_height),
        channels,
        &len
    );

    if (!pngData || len <= 0) return {};

    std::vector<uint8_t> result(pngData, pngData + len);
    free(pngData);
    return result;
}

bool ImageBitmap::LoadFromFile(const std::string& filepath, TextureColorSpace assumedColorSpace) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    if (size <= 0) return false;

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return false;
    }

    return DecodeFromMemory(buffer.data(), buffer.size(), assumedColorSpace);
}

bool ImageBitmap::SaveToFile(const std::string& filepath) const {
    auto pngBytes = EncodeToPngMemory();
    if (pngBytes.empty()) return false;

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    file.write(reinterpret_cast<const char*>(pngBytes.data()), pngBytes.size());
    return file.good();
}

} // namespace fw
