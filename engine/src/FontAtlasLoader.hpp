#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/Types.hpp"

namespace engine3d {

// Metryki i wspolrzedne atlasu potrzebne do zbudowania geometrii pojedynczego glifu.
struct FontAtlasGlyph {
    float advanceX = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

// Surowe dane atlasu glifow gotowe do uploadu do obrazu Vulkan.
struct FontAtlasData {
    std::string name;
    uint32_t atlasWidth = 0;
    uint32_t atlasHeight = 0;
    uint32_t pixelHeight = 0;
    float lineHeight = 0.0f;
    VkFormat format = VK_FORMAT_R8_UNORM;
    std::vector<std::uint8_t> pixelBytes;
    std::vector<VkBufferImageCopy> copyRegions;
    std::unordered_map<std::uint32_t, FontAtlasGlyph> glyphs;
};

// Laduje font TTF i rasteryzuje podstawowy zestaw glifow ASCII do jednego atlasu.
class FontAtlasLoader {
public:
    // Wczytuje font z dysku i zwraca gotowy atlas wraz z metrykami glifow.
    [[nodiscard]] static FontAtlasData load(const FontAsset& fontAsset, const std::vector<std::uint32_t>& glyphCodepoints);
};

}  // namespace engine3d