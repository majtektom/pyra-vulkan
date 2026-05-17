#pragma once

#include <string>
#include <unordered_map>

#include "engine/Types.hpp"

namespace engine3d {

// Odpowiada za szybkie wyszukiwanie assetow tekstur po nazwie logicznej.
class TextureManager {
public:
    // Wczytuje opis wszystkich tekstur z konfiguracji aplikacji.
    explicit TextureManager(const std::vector<TextureAsset>& textures);

    // Zwraca teksture o podanej nazwie albo przerywa start komunikatem o bledzie.
    [[nodiscard]] const TextureAsset& require(const std::string& name) const;

private:
    // Mapa oddziela logiczna nazwe tekstury od sciezki na dysku.
    std::unordered_map<std::string, TextureAsset> textures_;
};

}  // namespace engine3d