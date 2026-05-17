#include "engine/TextureManager.hpp"

#include <stdexcept>

namespace engine3d {

// Buduje mape nazw tekstur, aby renderer mogl pracowac na prostych identyfikatorach logicznych.
TextureManager::TextureManager(const std::vector<TextureAsset>& textures) {
    for (const auto& texture : textures) {
        if (texture.name.empty()) {
            throw std::runtime_error("texture manifest contains an empty texture name");
        }

        // Zapamietujemy caly opis tekstury, a nie tylko sciezke, bo w przyszlosci moga dojsc kolejne pola.
        textures_.emplace(texture.name, texture);
    }
}

// Wymaga istnienia tekstury o podanej nazwie, bo brak assetu jest bledem konfiguracji sceny.
const TextureAsset& TextureManager::require(const std::string& name) const {
    const auto iterator = textures_.find(name);
    if (iterator == textures_.end()) {
        throw std::runtime_error("texture not found in manifest: " + name);
    }
    return iterator->second;
}

}  // namespace engine3d