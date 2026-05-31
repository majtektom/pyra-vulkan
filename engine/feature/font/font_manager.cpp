#include <stdexcept>
#include "feature/font/font_manager.h"

namespace engine3d {

FontManager::FontManager(const std::vector<FontAsset>& fonts) {
    for (const auto& font : fonts) {
        add(font);
    }
}

void FontManager::add(const std::string& name, const std::string& path, uint32_t pixelHeight) {
    fonts_[name] = {name, path, pixelHeight};
}

void FontManager::add(const FontAsset& font) {
    fonts_[font.name] = font;
}

const FontAsset* FontManager::get(const std::string& name) const {
    auto it = fonts_.find(name);
    return it != fonts_.end() ? &it->second : nullptr;
}

const FontAsset& FontManager::require(const std::string& name) const {
    auto it = fonts_.find(name);
    if (it == fonts_.end()) {
        throw std::runtime_error("font not found: " + name);
    }
    return it->second;
}

} // namespace engine3d
