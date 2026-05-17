#include "engine/FontManager.hpp"

#include <stdexcept>

namespace engine3d {

// Buduje mape fontow, aby renderer mogl odwolywac sie do TTF po nazwie logicznej.
FontManager::FontManager(const std::vector<FontAsset>& fonts) {
    for (const auto& font : fonts) {
        if (font.name.empty()) {
            throw std::runtime_error("font manifest contains an empty font name");
        }
        if (font.path.empty()) {
            throw std::runtime_error("font manifest contains an empty font path for: " + font.name);
        }

        fonts_.emplace(font.name, font);
    }
}

// Zwraca opis fontu albo przerywa start silnika, gdy konfiguracja wskazuje nieistniejacy asset.
const FontAsset& FontManager::require(const std::string& name) const {
    const auto iterator = fonts_.find(name);
    if (iterator == fonts_.end()) {
        throw std::runtime_error("font not found in manifest: " + name);
    }
    return iterator->second;
}

}  // namespace engine3d