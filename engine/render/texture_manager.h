#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "types.h"

namespace engine3d {

class TextureManager {
public:
    TextureManager() = default;
    explicit TextureManager(const std::vector<TextureAsset>& textures);

    void add(const std::string& name, const std::string& path, bool mirroredRepeat = false);
    void add(const TextureAsset& texture);
    [[nodiscard]] const TextureAsset* get(const std::string& name) const;
    [[nodiscard]] const TextureAsset& require(const std::string& name) const;

private:
    std::unordered_map<std::string, TextureAsset> textures_;
};

} // namespace engine3d
