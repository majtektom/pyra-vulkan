#pragma once

#include "scene/scene.h"

namespace engine3d {

class MaterialManager;
class MaterialSystem;
class MeshManager;
class AssetRegistry;

/**
 * @brief System ECS odpowiedzialny za ekstrakcję danych renderowania (Render Extraction System).
 *
 * W nowoczesnych silnikach faza renderowania nie odpytuje bezpośrednio komponentów gry.
 * Zamiast tego, w fazie aktualizacji ECS, RenderExtractionSystem przechodzi przez wszystkie aktywne encje,
 * filtruje obiekty posiadające MeshComponent i tworzy "paczki renderowania" (RenderPackets).
 *
 * Paczka (RenderPacket) to spłaszczona struktura zawierająca surowe dane potrzebne do wywołania
 * rysowania na GPU (wskaźniki do buforów, ID tekstur, ID materiału). Separacja ta pozwala na:
 * 1. Zrównoleglenie: GPU może renderować klatkę N, podczas gdy CPU wylicza klatkę N+1.
 * 2. Wydajność cache: Dane są zwarte i ułożone w pamięci sekwencyjnie.
 */
class RenderExtractionSystem final : public ISceneSystem {
public:
    RenderExtractionSystem(
        MaterialManager& materialManager,
        MaterialSystem& materialSystem,
        MeshManager& meshManager,
        AssetRegistry& assetRegistry);

    /**
     * @brief Zbiera dane renderowania dla bieżącej klatki.
     * Przeszukuje encje w poszukiwaniu komponentów siatek i generuje z nich RenderPackets.
     */
    void update(Scene& scene, float deltaTime) override;

    // Nazwa systemu.
    const char* name() const override { return "RenderExtractionSystem"; }

private:
    // Pobiera lub tworzy instancję materiału powiązaną z komponentem siatki.
    void resolveMaterialIfNeeded(class MeshComponent& meshComponent);

    // Przypisuje unikalne identyfikatory zasobów (tekstur PBR) na podstawie ich nazw w materiale.
    void resolvePacketAssetHandles(RenderPacket& packet);

    MaterialManager& materialManager_;
    MaterialSystem& materialSystem_;
    MeshManager& meshManager_;
    AssetRegistry& assetRegistry_;
};

} // namespace engine3d