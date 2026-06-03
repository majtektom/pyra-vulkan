#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/asset_registry.h"
#include "core/types.h"

namespace engine3d {

/**
 * @brief Menedżer właściwości tekstur (Texture Manager).
 *
 * TextureManager przechowuje definicje i ścieżki dostępnych w grze plików graficznych (tekstur).
 * Pomaga w mapowaniu nazwy tekstury na bezpieczny uchwyt zasobu (TextureHandle) oraz rejestrowaniu
 * plików w AssetRegistry, aby niskopoziomowa warstwa Vulkana mogła je załadować na GPU.
 */
class TextureManager {
public:
    TextureManager() = default;
    explicit TextureManager(const std::vector<TextureAsset>& textures);

    /**
     * @brief Podpina rejestr zasobów (AssetRegistry).
     */
    void setAssetRegistry(AssetRegistry* registry) { assetRegistry_ = registry; }

    /**
     * @brief Rejestruje teksturę o podanych parametrach.
     * @param name Identyfikator/Nazwa tekstury.
     * @param path Ścieżka do pliku graficznego.
     * @param mirroredRepeat Czy włączyć kafelkowanie z lustrzanym odbiciem.
     */
    void add(const std::string& name, const std::string& path, bool mirroredRepeat = false);
    
    /**
     * @brief Rejestruje teksturę przy użyciu kompletnej struktury definicji.
     */
    void add(const TextureAsset& texture);
    
    // Wyszukiwanie tekstury po nazwie (zwraca nullptr w razie braku):
    [[nodiscard]] const TextureAsset* get(const std::string& name) const;
    
    /**
     * @brief Pobiera referencję do struktury tekstury lub zgłasza wyjątek std::runtime_error.
     */
    [[nodiscard]] const TextureAsset& require(const std::string& name) const;

    /**
     * @brief Pobiera unikalny uchwyt tekstury (TextureHandle) na podstawie jej nazwy.
     */
    std::optional<TextureHandle> handle(const std::string& name) const;

private:
    std::unordered_map<std::string, TextureAsset> textures_; // Rejestr właściwości tekstur.
    AssetRegistry* assetRegistry_ = nullptr;                 // Wskaźnik do rejestru zasobów.
};

} // namespace engine3d
