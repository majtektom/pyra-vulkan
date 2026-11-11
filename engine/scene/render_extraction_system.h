#pragma once

#include "core/job_system.h"
#include "scene/scene.h"

namespace engine3d {

class MaterialManager;
class MaterialSystem;
class MeshManager;
class TerrainComponent;
class AssetRegistry;

/**
 * @brief System ECS odpowiedzialny za ekstrakcję danych renderowania (Render Extraction System).
 *
 * W nowoczesnych silnikach faza renderowania nie odpytuje bezpośrednio komponentów gry.
 * Zamiast tego, w fazie aktualizacji ECS, RenderExtractionSystem przechodzi przez wszystkie aktywne encje,
 * filtruje obiekty posiadające MeshComponent i tworzy "paczki renderowania" (RenderPackets).
 *
 * Paczka (RenderPacket) to spłaszczona struktura zawierająca surowe dane potrzebne do wywołania
 * rysowania na GPU (wskaźniki do buforów, ID tekstur, ID materiału).
 *
 * Równoległość:
 *  - Faza 1 (sequential): rozwiązanie materiałów (modyfikuje stan komponentu).
 *  - Faza 2 (parallel): zbieranie paczek do thread-local buforów przez JobSystem.
 *  - Faza 3 (sequential): scalenie buforów do wektora sceny.
 */
class RenderExtractionSystem final : public ISceneSystem {
public:
    RenderExtractionSystem(
        MaterialManager& materialManager,
        MaterialSystem& materialSystem,
        MeshManager& meshManager,
        AssetRegistry& assetRegistry,
        JobSystem& jobs,
        const class Renderer& renderer);

    void update(Scene& scene, float deltaTime) override;
    const char* name() const override { return "RenderExtractionSystem"; }

private:
    void resolveMaterialIfNeeded(class MeshComponent& meshComponent);
    void resolveMaterialIfNeeded(class TerrainComponent& terrainComponent);
    void resolvePacketAssetHandles(RenderPacket& packet);

    MaterialManager& materialManager_;
    MaterialSystem& materialSystem_;
    MeshManager& meshManager_;
    AssetRegistry& assetRegistry_;
    JobSystem& jobs_;
    const class Renderer& renderer_;
    std::vector<std::vector<RenderPacket>> localBuffers_;   // paczki z każdego wątku (pamięć między klatkami)
};

} // namespace engine3d