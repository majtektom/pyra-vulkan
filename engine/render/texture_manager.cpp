#include <stdexcept>

#include "render/texture_manager.h"

namespace engine3d {

TextureManager::TextureManager(const std::vector<TextureAsset>& textures) {
    for (const auto& texture : textures) {
        add(texture);
    }
}

void TextureManager::add(const std::string& name, const std::string& path, bool mirroredRepeat) {
    // Zapisujemy parametry tekstury.
    textures_[name] = {name, path, mirroredRepeat};
    
    // Rejestrujemy teksturę w AssetRegistry, co inicjuje przesłanie danych na GPU w tle lub przed klatką.
    if (assetRegistry_) {
        assetRegistry_->registerTexture(name);
    }
}

void TextureManager::add(const TextureAsset& texture) {
    textures_[texture.name] = texture;
    if (assetRegistry_) {
        assetRegistry_->registerTexture(texture.name);
    }
}

const TextureAsset* TextureManager::get(const std::string& name) const {
    auto it = textures_.find(name);
    return it != textures_.end() ? &it->second : nullptr;
}

const TextureAsset& TextureManager::require(const std::string& name) const {
    auto it = textures_.find(name);
    if (it == textures_.end()) {
        throw std::runtime_error("nie znaleziono tekstury: " + name);
    }
    return it->second;
}

std::optional<TextureHandle> TextureManager::handle(const std::string& name) const {
    if (!assetRegistry_) {
        return std::nullopt;
    }
    return assetRegistry_->textureHandle(name);
}

} // namespace engine3d
