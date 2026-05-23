#include "font_atlas_loader.h"

#include "utf8.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef ENGINE3D_HAS_FREETYPE
#define ENGINE3D_HAS_FREETYPE 0
#endif

#if ENGINE3D_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace engine3d {
namespace {

struct RasterizedGlyph {
    std::uint32_t codepoint = 0;
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advanceX = 0;
    uint32_t atlasX = 0;
    uint32_t atlasY = 0;
    std::vector<std::uint8_t> pixels;
};

}  // namespace

// Wczytuje TTF przez FreeType i zamienia wskazany zestaw znakow Unicode na atlas jednokanalowy.
FontAtlasData FontAtlasLoader::load(const FontAsset& fontAsset, const std::vector<std::uint32_t>& glyphCodepoints) {
#if !ENGINE3D_HAS_FREETYPE
    (void)fontAsset;
    (void)glyphCodepoints;
    throw std::runtime_error(
        "TTF font support requires FreeType development headers. Install freetype-devel and rebuild the project."
    );
#else
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        throw std::runtime_error("failed to initialize FreeType library");
    }

    FT_Face face = nullptr;
    const auto cleanup = [&]() {
        if (face != nullptr) {
            FT_Done_Face(face);
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    };

    if (FT_New_Face(library, fontAsset.path.c_str(), 0, &face) != 0) {
        cleanup();
        throw std::runtime_error("failed to open TTF font: " + fontAsset.path);
    }

    if (FT_Set_Pixel_Sizes(face, 0, fontAsset.pixelHeight) != 0) {
        cleanup();
        throw std::runtime_error("failed to set TTF pixel size for font: " + fontAsset.name);
    }

    constexpr uint32_t padding = 1;

    std::vector<std::uint32_t> requestedGlyphs = glyphCodepoints;
    if (requestedGlyphs.empty()) {
        for (std::uint32_t codepoint = 32; codepoint <= 126; ++codepoint) {
            requestedGlyphs.push_back(codepoint);
        }
    }

    utf8::appendUnique(requestedGlyphs, static_cast<std::uint32_t>(' '));
    utf8::appendUnique(requestedGlyphs, static_cast<std::uint32_t>('?'));

    const uint32_t atlasWidth = std::max(256u, fontAsset.pixelHeight * 16u);
    uint32_t penX = padding;
    uint32_t penY = padding;
    uint32_t rowHeight = 0;
    std::vector<RasterizedGlyph> rasterizedGlyphs;
    rasterizedGlyphs.reserve(requestedGlyphs.size());

    for (std::uint32_t codepoint : requestedGlyphs) {
        if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) {
            cleanup();
            throw std::runtime_error("failed to rasterize glyph for codepoint: " + std::to_string(codepoint));
        }

        const FT_GlyphSlot glyph = face->glyph;
        const uint32_t glyphWidth = static_cast<uint32_t>(glyph->bitmap.width);
        const uint32_t glyphHeight = static_cast<uint32_t>(glyph->bitmap.rows);

        if (penX + glyphWidth + padding > atlasWidth) {
            penX = padding;
            penY += rowHeight + padding;
            rowHeight = 0;
        }

        RasterizedGlyph rasterized{};
        rasterized.codepoint = codepoint;
        rasterized.width = static_cast<int>(glyphWidth);
        rasterized.height = static_cast<int>(glyphHeight);
        rasterized.bearingX = glyph->bitmap_left;
        rasterized.bearingY = glyph->bitmap_top;
        rasterized.advanceX = static_cast<int>(glyph->advance.x >> 6);
        rasterized.atlasX = penX;
        rasterized.atlasY = penY;
        rasterized.pixels.assign(glyph->bitmap.buffer, glyph->bitmap.buffer + glyphWidth * glyphHeight);
        rasterizedGlyphs.push_back(std::move(rasterized));

        penX += glyphWidth + padding;
        rowHeight = std::max(rowHeight, glyphHeight);
    }

    const uint32_t atlasHeight = std::max(penY + rowHeight + padding, fontAsset.pixelHeight + 2 * padding);
    FontAtlasData atlas;
    atlas.name = fontAsset.name;
    atlas.atlasWidth = atlasWidth;
    atlas.atlasHeight = atlasHeight;
    atlas.pixelHeight = fontAsset.pixelHeight;
    atlas.lineHeight = static_cast<float>(face->size->metrics.height >> 6);
    if (atlas.lineHeight <= 0.0f) {
        atlas.lineHeight = static_cast<float>(fontAsset.pixelHeight);
    }
    atlas.pixelBytes.assign(static_cast<std::size_t>(atlasWidth) * atlasHeight, 0);

    for (const auto& glyph : rasterizedGlyphs) {
        for (int row = 0; row < glyph.height; ++row) {
            const std::size_t dstOffset = static_cast<std::size_t>(glyph.atlasY + static_cast<uint32_t>(row)) * atlasWidth + glyph.atlasX;
            const std::size_t srcOffset = static_cast<std::size_t>(row) * static_cast<std::size_t>(glyph.width);
            std::copy_n(glyph.pixels.data() + srcOffset, glyph.width, atlas.pixelBytes.data() + dstOffset);
        }

        FontAtlasGlyph atlasGlyph{};
        atlasGlyph.advanceX = static_cast<float>(glyph.advanceX);
        atlasGlyph.bearingX = static_cast<float>(glyph.bearingX);
        atlasGlyph.bearingY = static_cast<float>(glyph.bearingY);
        atlasGlyph.width = static_cast<float>(glyph.width);
        atlasGlyph.height = static_cast<float>(glyph.height);
        atlasGlyph.u0 = static_cast<float>(glyph.atlasX) / static_cast<float>(atlasWidth);
        atlasGlyph.v0 = static_cast<float>(glyph.atlasY) / static_cast<float>(atlasHeight);
        atlasGlyph.u1 = static_cast<float>(glyph.atlasX + static_cast<uint32_t>(glyph.width)) / static_cast<float>(atlasWidth);
        atlasGlyph.v1 = static_cast<float>(glyph.atlasY + static_cast<uint32_t>(glyph.height)) / static_cast<float>(atlasHeight);
        atlas.glyphs.emplace(glyph.codepoint, atlasGlyph);
    }

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {atlasWidth, atlasHeight, 1};
    atlas.copyRegions.push_back(region);

    cleanup();
    return atlas;
#endif
}

}  // namespace engine3d