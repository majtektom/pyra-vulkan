#pragma once

#include <string>
#include <unordered_map>

#include "engine/Types.hpp"

namespace engine3d {

// Odpowiada za szybkie wyszukiwanie assetow fontow po nazwie logicznej.
class FontManager {
public:
    // Buduje mape nazw fontow na podstawie konfiguracji aplikacji.
    explicit FontManager(const std::vector<FontAsset>& fonts);

    // Zwraca font o podanej nazwie albo zglasza czytelny blad konfiguracji.
    [[nodiscard]] const FontAsset& require(const std::string& name) const;

private:
    // Mapa pozwala rozdzielic logiczna nazwe fontu od jego sciezki i parametrow rasteryzacji.
    std::unordered_map<std::string, FontAsset> fonts_;
};

}  // namespace engine3d