#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/asset_registry.h"
#include "core/types.h"

namespace engine3d {

/**
 * @brief Menedżer właściwości materiałowych (Material Manager).
 *
 * Klasa przechowuje i zarządza definicjami materiałów (Material) w silniku.
 * Odpowiada za:
 * - Rejestrację nowych materiałów i przydzielanie im unikalnych, bezpiecznych typowanych uchwytów (MaterialHandle).
 * - Szybkie wyszukiwanie właściwości materiału na podstawie nazwy lub uchwytu.
 * - Integrację z rejestrem zasobów (AssetRegistry) w celu zapewnienia spójności identyfikatorów.
 *
 * Rozdzielenie właściwości (Material) od zasobów na GPU pozwala na wygodne edytowanie parametrów
 * powierzchni na CPU (np. kolor, metaliczność) i automatyczną propagację zmian do systemu renderowania.
 */
class MaterialManager {
public:
    MaterialManager() = default;

    /**
     * @brief Podpina rejestr zasobów (AssetRegistry), umożliwiając tłumaczenie nazw na uchwyty i odwrotnie.
     */
    void setAssetRegistry(AssetRegistry* registry) { assetRegistry_ = registry; }

    /**
     * @brief Rejestruje nowy materiał w menedżerze.
     * @param material Struktura z definicją właściwości materiału.
     * @return Unikalny, typowany uchwyt (MaterialHandle) przypisany do zarejestrowanego materiału.
     */
    MaterialHandle add(const Material& material);

    // Pobieranie materiału (zwraca nullptr w razie braku):
    [[nodiscard]] const Material* get(const std::string& name) const;
    [[nodiscard]] const Material* get(MaterialHandle handle) const;
    
    /**
     * @brief Pobiera referencję do materiału lub zgłasza wyjątek std::runtime_error w przypadku jego braku.
     */
    const Material& require(const std::string& name) const;

    // Sprawdza, czy materiał o podanej nazwie jest zarejestrowany.
    bool has(const std::string& name) const;

    /**
     * @brief Pobiera uchwyt (MaterialHandle) zarejestrowanego materiału na podstawie jego nazwy.
     */
    std::optional<MaterialHandle> handle(const std::string& name) const;

private:
    std::unordered_map<std::string, Material> materials_; // Mapa nazw na definicje właściwości materiałów.
    std::vector<Material> handleToMaterial_;              // Wyszukiwanie po uchwycie (O(1)).
    AssetRegistry* assetRegistry_ = nullptr;              // Wskaźnik do rejestru zasobów.
};

}  // namespace engine3d
