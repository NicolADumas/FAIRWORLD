#include "render/TexturePacker.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

struct TempRoot {
    fs::path path;
    TempRoot() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("fairworld-pbr-cache-tests-" + std::to_string(stamp));
        fs::create_directories(path / "assets/definitions");
    }
    ~TempRoot() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    Require(static_cast<bool>(input), "cannot open " + path.string());
    const auto size = input.tellg();
    Require(size >= 0, "cannot size " + path.string());
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(input), "cannot read " + path.string());
    return bytes;
}

void WriteBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(output), "cannot write " + path.string());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(output), "short write " + path.string());
}

void WriteBmp1x1(const fs::path& path, uint8_t r, uint8_t g, uint8_t b) {
    // Minimal 1x1 uncompressed 24-bit BMP; each row is padded to four bytes.
    std::vector<uint8_t> bmp(58, 0);
    bmp[0] = 'B'; bmp[1] = 'M';
    bmp[2] = 58; bmp[10] = 54;
    bmp[14] = 40; bmp[18] = 1; bmp[22] = 1;
    bmp[26] = 1; bmp[28] = 24; bmp[34] = 4;
    bmp[54] = b; bmp[55] = g; bmp[56] = r;
    WriteBytes(path, bmp);
}

std::vector<fw::PBRMaterialDef> Materials() {
    std::vector<fw::PBRMaterialDef> materials(13);
    for (size_t i = 0; i < materials.size(); ++i) {
        materials[i].target_block_id = static_cast<uint8_t>(i);
        materials[i].baseColorFallback = glm::vec3(0.25f, 0.5f, 0.75f);
    }
    materials[12].baseColorFallback = glm::vec3(0.0f, 0.024768f, 1.0f);
    return materials;
}

void WriteDefinitions(const fs::path& root, const std::string& content = "{\"materials\":[]}") {
    const fs::path path = root / "assets/definitions/materials.json";
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

fw::PackedTextureData Pack(const fs::path& root, const std::vector<fw::PBRMaterialDef>& materials) {
    fw::TexturePacker packer(2, root);
    return packer.PackMaterials(materials);
}

bool TestUnchangedCacheReuse() {
    TempRoot root;
    WriteDefinitions(root.path);
    auto materials = Materials();
    const auto first = Pack(root.path, materials);
    const auto cacheBefore = ReadBytes(root.path / "assets/texture_cache.bin");
    const auto second = Pack(root.path, materials);
    return first.albedoData == second.albedoData && first.normalData == second.normalData &&
        first.ormData == second.ormData && cacheBefore == ReadBytes(root.path / "assets/texture_cache.bin");
}

bool TestDefinitionContentInvalidates() {
    TempRoot root;
    WriteDefinitions(root.path);
    auto materials = Materials();
    Pack(root.path, materials);
    const auto before = ReadBytes(root.path / "assets/texture_cache.bin");
    WriteDefinitions(root.path, "{ \"materials\" : [], \"revision\": 2 }");
    Pack(root.path, materials);
    return before != ReadBytes(root.path / "assets/texture_cache.bin");
}

bool TestTextureBytesInvalidateWithUnchangedTimestamp(const std::string& channel) {
    TempRoot root;
    WriteDefinitions(root.path);
    auto materials = Materials();
    const fs::path texture = root.path / "assets/textures/channel.bmp";
    if (channel == "albedo") materials[12].albedoPath = "assets/textures/channel.bmp";
    if (channel == "normal") materials[12].normalPath = "assets/textures/channel.bmp";
    if (channel == "orm") materials[12].ormPath = "assets/textures/channel.bmp";
    WriteBmp1x1(texture, 220, 30, 10);
    const auto fixedTime = fs::file_time_type::clock::now() - std::chrono::hours(24);
    fs::last_write_time(texture, fixedTime);
    const auto first = Pack(root.path, materials);
    WriteBmp1x1(texture, 10, 40, 230);
    fs::last_write_time(texture, fixedTime);
    const auto second = Pack(root.path, materials);
    const auto& firstData = channel == "albedo" ? first.albedoData : (channel == "normal" ? first.normalData : first.ormData);
    const auto& secondData = channel == "albedo" ? second.albedoData : (channel == "normal" ? second.normalData : second.ormData);
    return firstData != secondData;
}

bool TestMissingAppearsAndRemoved() {
    TempRoot root;
    WriteDefinitions(root.path);
    auto materials = Materials();
    materials[12].albedoPath = "assets/textures/later.bmp";
    const auto missing = Pack(root.path, materials);
    const fs::path texture = root.path / "assets/textures/later.bmp";
    WriteBmp1x1(texture, 15, 160, 40);
    const auto appeared = Pack(root.path, materials);
    fs::remove(texture);
    const auto removed = Pack(root.path, materials);
    const size_t pixel = 12 * 2 * 2 * 4;
    return missing.albedoData[pixel] == 0 && missing.albedoData[pixel + 1] == 6 && missing.albedoData[pixel + 2] == 255 &&
        appeared.albedoData[pixel] == 15 && appeared.albedoData[pixel + 1] == 160 && appeared.albedoData[pixel + 2] == 40 &&
        removed.albedoData[pixel] == 0 && removed.albedoData[pixel + 1] == 6 && removed.albedoData[pixel + 2] == 255;
}

bool TestTruncatedAndCorruptPayloadRegenerate() {
    TempRoot root;
    WriteDefinitions(root.path);
    auto materials = Materials();
    const auto expected = Pack(root.path, materials);
    const fs::path cache = root.path / "assets/texture_cache.bin";
    auto bytes = ReadBytes(cache);
    bytes.resize(24);
    WriteBytes(cache, bytes);
    if (Pack(root.path, materials).albedoData != expected.albedoData) return false;
    bytes = ReadBytes(cache);
    bytes.back() ^= 0x7f;
    WriteBytes(cache, bytes);
    return Pack(root.path, materials).ormData == expected.ormData && ReadBytes(cache).back() != bytes.back();
}

bool TestIncompatibleVersionRegenerates() {
    TempRoot root;
    WriteDefinitions(root.path);
    auto materials = Materials();
    Pack(root.path, materials);
    const fs::path cache = root.path / "assets/texture_cache.bin";
    auto bytes = ReadBytes(cache);
    bytes[8] = 99; bytes[9] = 0; bytes[10] = 0; bytes[11] = 0;
    WriteBytes(cache, bytes);
    Pack(root.path, materials);
    const auto regenerated = ReadBytes(cache);
    return regenerated[8] == 2 && regenerated[9] == 0 && regenerated[10] == 0 && regenerated[11] == 0;
}

bool TestLayerMappingAndId12Fallback() {
    TempRoot root;
    WriteDefinitions(root.path);
    auto materials = Materials();
    materials[1].baseColorFallback = glm::vec3(1.0f, 0.0f, 0.0f);
    materials[3].baseColorFallback = glm::vec3(0.0f, 1.0f, 0.0f);
    const auto packed = Pack(root.path, materials);
    const size_t pixelsPerLayer = 2 * 2 * 4;
    const size_t id1 = 1 * pixelsPerLayer;
    const size_t id3 = 3 * pixelsPerLayer;
    const size_t id12 = 12 * pixelsPerLayer;
    return packed.layerCount == 13 && packed.albedoData[id1] == 255 && packed.albedoData[id1 + 1] == 0 &&
        packed.albedoData[id3] == 0 && packed.albedoData[id3 + 1] == 255 &&
        packed.albedoData[id12] == 0 && packed.albedoData[id12 + 1] == 6 && packed.albedoData[id12 + 2] == 255;
}

bool TestResolutionAndLayerCountInvalidate() {
    TempRoot root;
    WriteDefinitions(root.path);
    auto materials = Materials();
    Pack(root.path, materials);
    const auto firstCache = ReadBytes(root.path / "assets/texture_cache.bin");
    fw::TexturePacker differentResolution(3, root.path);
    differentResolution.PackMaterials(materials);
    const auto resolutionCache = ReadBytes(root.path / "assets/texture_cache.bin");
    materials.pop_back();
    Pack(root.path, materials);
    const auto layerCache = ReadBytes(root.path / "assets/texture_cache.bin");
    return firstCache != resolutionCache && resolutionCache != layerCache;
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, bool (*)()>> tests = {
        {"unchanged cache reuse", TestUnchangedCacheReuse},
        {"materials.json content invalidation", TestDefinitionContentInvalidates},
        {"albedo content invalidation despite unchanged timestamp", [] { return TestTextureBytesInvalidateWithUnchangedTimestamp("albedo"); }},
        {"normal content invalidation despite unchanged timestamp", [] { return TestTextureBytesInvalidateWithUnchangedTimestamp("normal"); }},
        {"ORM content invalidation despite unchanged timestamp", [] { return TestTextureBytesInvalidateWithUnchangedTimestamp("orm"); }},
        {"missing texture appearance and removal", TestMissingAppearsAndRemoved},
        {"truncated and payload-corrupt cache regeneration", TestTruncatedAndCorruptPayloadRegenerate},
        {"incompatible cache version regeneration", TestIncompatibleVersionRegenerates},
        {"ID-to-layer mapping and ID 12 fallback pixels", TestLayerMappingAndId12Fallback},
        {"resolution and layer-count invalidation", TestResolutionAndLayerCountInvalidate}
    };
    size_t passed = 0;
    for (const auto& [name, test] : tests) {
        bool ok = false;
        try { ok = test(); } catch (const std::exception& e) { std::cerr << "[DETAIL] " << name << ": " << e.what() << '\n'; }
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
        passed += ok ? 1 : 0;
    }
    std::cout << "TOTAL: " << tests.size() << ", PASSED: " << passed << ", FAILED: " << (tests.size() - passed) << '\n';
    return passed == tests.size() ? 0 : 1;
}
