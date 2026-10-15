#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace fw {

// =============================================================================
// SHA-256 IMPLEMENTATION (FIPS PUB 180-4 Standard)
// Autonoma, senza dipendenze esterne (OpenSSL/networking non ammessi).
// Determinismo rigoroso e portable per integrità crittografica degli asset.
// =============================================================================
class Sha256 {
public:
    Sha256() {
        Reset();
    }

    void Reset() {
        m_state[0] = 0x6a09e667;
        m_state[1] = 0xbb67ae85;
        m_state[2] = 0x3c6ef372;
        m_state[3] = 0xa54ff53a;
        m_state[4] = 0x510e527f;
        m_state[5] = 0x9b05688c;
        m_state[6] = 0x1f83d9ab;
        m_state[7] = 0x5be0cd19;
        m_bufferLen = 0;
        m_totalBits = 0;
    }

    void Update(const void* data, size_t lengthBytes) {
        if (!data || lengthBytes == 0) return;
        const uint8_t* bytePtr = reinterpret_cast<const uint8_t*>(data);

        m_totalBits += static_cast<uint64_t>(lengthBytes) * 8ULL;

        while (lengthBytes > 0) {
            size_t toCopy = std::min(lengthBytes, size_t(64 - m_bufferLen));
            std::memcpy(m_buffer.data() + m_bufferLen, bytePtr, toCopy);
            m_bufferLen += toCopy;
            bytePtr += toCopy;
            lengthBytes -= toCopy;

            if (m_bufferLen == 64) {
                Transform(m_buffer.data());
                m_bufferLen = 0;
            }
        }
    }

    void Update(const std::string& str) {
        Update(str.data(), str.size());
    }

    std::array<uint8_t, 32> Finalize() {
        Sha256 copy = *this;
        return copy.FinalizeInternal();
    }

    std::string FinalizeHex() {
        auto d = Finalize();
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint8_t b : d) {
            oss << std::setw(2) << static_cast<unsigned int>(b);
        }
        return oss.str();
    }

private:
    std::array<uint8_t, 32> FinalizeInternal() {
        // Padding standard: 1 bit '1' (0x80) poi zeri fino a len == 56 mod 64
        m_buffer[m_bufferLen++] = 0x80;

        if (m_bufferLen > 56) {
            std::memset(m_buffer.data() + m_bufferLen, 0, 64 - m_bufferLen);
            Transform(m_buffer.data());
            m_bufferLen = 0;
        }

        std::memset(m_buffer.data() + m_bufferLen, 0, 56 - m_bufferLen);

        // Ultimi 8 byte: lunghezza totale in bit in big-endian
        for (int i = 7; i >= 0; --i) {
            m_buffer[56 + (7 - i)] = static_cast<uint8_t>((m_totalBits >> (i * 8)) & 0xFF);
        }
        Transform(m_buffer.data());

        std::array<uint8_t, 32> digest{};
        for (size_t i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>((m_state[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((m_state[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((m_state[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(m_state[i] & 0xFF);
        }

        return digest;
    }

    // Metodi statici di convenienza
    static std::string ComputeHex(const void* data, size_t lengthBytes) {
        Sha256 ctx;
        ctx.Update(data, lengthBytes);
        return ctx.FinalizeHex();
    }

    static std::string ComputeHex(const std::string& str) {
        return ComputeHex(str.data(), str.size());
    }

    static std::string ComputeFileHex(const std::string& filepath, bool* outSuccess = nullptr) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            if (outSuccess) *outSuccess = false;
            return "";
        }

        Sha256 ctx;
        char buffer[8192];
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
            ctx.Update(buffer, static_cast<size_t>(file.gcount()));
        }

        if (outSuccess) *outSuccess = true;
        return ctx.FinalizeHex();
    }

private:
    static inline uint32_t RotR(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    static inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (~x & z);
    }

    static inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    static inline uint32_t Ep0(uint32_t x) {
        return RotR(x, 2) ^ RotR(x, 13) ^ RotR(x, 22);
    }

    static inline uint32_t Ep1(uint32_t x) {
        return RotR(x, 6) ^ RotR(x, 11) ^ RotR(x, 25);
    }

    static inline uint32_t Sig0(uint32_t x) {
        return RotR(x, 7) ^ RotR(x, 18) ^ (x >> 3);
    }

    static inline uint32_t Sig1(uint32_t x) {
        return RotR(x, 17) ^ RotR(x, 19) ^ (x >> 10);
    }

    void Transform(const uint8_t* block) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8)  |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = Sig1(w[i - 2]) + w[i - 7] + Sig0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];
        uint32_t f = m_state[5];
        uint32_t g = m_state[6];
        uint32_t h = m_state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + Ep1(e) + Ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = Ep0(a) + Maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    uint32_t m_state[8];
    std::array<uint8_t, 64> m_buffer{};
    size_t m_bufferLen = 0;
    uint64_t m_totalBits = 0;
};

} // namespace fw
