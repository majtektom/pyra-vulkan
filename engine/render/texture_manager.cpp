#include <stdexcept>

#include "render/texture_manager.h"
namespace engine3d {

TextureManager::TextureManager(const std::vector<TextureAsset>& textures) {
    for (const auto& texture : textures) {
        add(texture);
    }
}

void TextureManager::add(const std::string& name, const std::string& path, bool mirroredRepeat) {
    textures_[name] = {name, path, mirroredRepeat};
}

void TextureManager::add(const TextureAsset& texture) {
    textures_[texture.name] = texture;
}

const TextureAsset* TextureManager::get(const std::string& name) const {
    auto it = textures_.find(name);
    return it != textures_.end() ? &it->second : nullptr;
}

const TextureAsset& TextureManager::require(const std::string& name) const {
    auto it = textures_.find(name);
    if (it == textures_.end()) {
        throw std::runtime_error("texture not found: " + name);
    }
    return it->second;
}

} // namespace engine3d
