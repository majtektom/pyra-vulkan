#include "font_atlas_loader.h"

#include "utf8.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

FontAtlasData FontAtlasLoader::load(const FontAsset& fontAsset, const std::vector<std::uint32_t>& glyphCodepoints) {
    TTF_Font* font = TTF_OpenFont(fontAsset.path.c_str(), static_cast<float>(fontAsset.pixelHeight));
    if (!font) {
        throw std::runtime_error("failed to open TTF font: " + fontAsset.path + " — " + SDL_GetError());
    }

    const auto cleanup = [&]() { TTF_CloseFont(font); };

    constexpr uint32_t padding = 1;

    std::vector<std::uint32_t> requestedGlyphs = glyphCodepoints;
    if (requestedGlyphs.empty()) {
        // ASCII printable (32-126)
        for (std::uint32_t cp = 32; cp <= 126; ++cp) requestedGlyphs.push_back(cp);
        // Polish diacritics (Latin Extended-A + Latin-1 Supplement)
        const std::uint32_t polishGlyphs[] = {
            0x0104, 0x0105, 0x0106, 0x0107, 0x0118, 0x0119,  // ĄąĆćĘę
            0x0141, 0x0142, 0x0143, 0x0144,                   // ŁłŃń
            0x00D3, 0x00F3,                                     // Óó
            0x015A, 0x015B,                                     // Śś
            0x0179, 0x017A, 0x017B, 0x017C,                   // ŹźŻż
        };
        for (auto cp : polishGlyphs) utf8::appendUnique(requestedGlyphs, cp);
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
        char utf8[5] = {};
        if (codepoint < 0x80) {
            utf8[0] = static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            utf8[0] = static_cast<char>(0xC0 | (codepoint >> 6));
            utf8[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            utf8[0] = static_cast<char>(0xE0 | (codepoint >> 12));
            utf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            utf8[0] = static_cast<char>(0xF0 | (codepoint >> 18));
            utf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            utf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        }

        SDL_Surface* glyphSurf = TTF_RenderText_Blended(font, utf8, 0, SDL_Color{255, 255, 255, 255});
        if (!glyphSurf) continue;

        int minX, maxX, minY, maxY, advance;
        if (!TTF_GetGlyphMetrics(font, codepoint, &minX, &maxX, &minY, &maxY, &advance)) {
            advance = glyphSurf->w;
            minX = 0; maxX = glyphSurf->w;
            minY = -TTF_GetFontDescent(font);
            maxY = TTF_GetFontAscent(font);
        }

        const uint32_t gw = static_cast<uint32_t>(glyphSurf->w);
        const uint32_t gh = static_cast<uint32_t>(glyphSurf->h);

        if (penX + gw + padding > atlasWidth) {
            penX = padding;
            penY += rowHeight + padding;
            rowHeight = 0;
        }

        RasterizedGlyph rg{};
        rg.codepoint = codepoint;
        rg.width = static_cast<int>(gw);
        rg.height = static_cast<int>(gh);
        rg.bearingX = minX;
        rg.bearingY = TTF_GetFontAscent(font);
        rg.advanceX = advance;
        rg.atlasX = penX;
        rg.atlasY = penY;

        rg.pixels.resize(static_cast<size_t>(gw) * gh);
        uint8_t* src = static_cast<uint8_t*>(glyphSurf->pixels);
        for (uint32_t y = 0; y < gh; ++y) {
            const uint8_t* row = src + static_cast<size_t>(y) * glyphSurf->pitch;
            for (uint32_t x = 0; x < gw; ++x) {
                rg.pixels[static_cast<size_t>(y) * gw + x] = row[x * 4 + 3];
            }
        }

        rasterizedGlyphs.push_back(std::move(rg));
        penX += gw + padding;
        rowHeight = std::max(rowHeight, gh);

        SDL_DestroySurface(glyphSurf);
    }

    const uint32_t atlasHeight = std::max(penY + rowHeight + padding, fontAsset.pixelHeight + 2 * padding);
    FontAtlasData atlas;
    atlas.name = fontAsset.name;
    atlas.atlasWidth = atlasWidth;
    atlas.atlasHeight = atlasHeight;
    atlas.pixelHeight = fontAsset.pixelHeight;
    atlas.lineHeight = static_cast<float>(TTF_GetFontHeight(font));
    if (atlas.lineHeight <= 0.0f) {
        atlas.lineHeight = static_cast<float>(fontAsset.pixelHeight);
    }
    atlas.pixelBytes.assign(static_cast<std::size_t>(atlasWidth) * atlasHeight, 0);

    for (const auto& rg : rasterizedGlyphs) {
        for (int row = 0; row < rg.height; ++row) {
            const std::size_t dst = static_cast<std::size_t>(rg.atlasY + static_cast<uint32_t>(row)) * atlasWidth + rg.atlasX;
            const std::size_t src = static_cast<std::size_t>(row) * static_cast<std::size_t>(rg.width);
            std::copy_n(rg.pixels.data() + src, rg.width, atlas.pixelBytes.data() + dst);
        }

        FontAtlasGlyph g{};
        g.advanceX = static_cast<float>(rg.advanceX);
        g.bearingX = static_cast<float>(rg.bearingX);
        g.bearingY = static_cast<float>(rg.bearingY);
        g.width = static_cast<float>(rg.width);
        g.height = static_cast<float>(rg.height);
        g.u0 = static_cast<float>(rg.atlasX) / static_cast<float>(atlasWidth);
        g.v0 = static_cast<float>(rg.atlasY) / static_cast<float>(atlasHeight);
        g.u1 = static_cast<float>(rg.atlasX + static_cast<uint32_t>(rg.width)) / static_cast<float>(atlasWidth);
        g.v1 = static_cast<float>(rg.atlasY + static_cast<uint32_t>(rg.height)) / static_cast<float>(atlasHeight);
        atlas.glyphs.emplace(rg.codepoint, g);
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
}

}  // namespace engine3d
