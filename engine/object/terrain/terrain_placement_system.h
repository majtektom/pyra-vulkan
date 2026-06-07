#pragma once

#include "scene/scene.h"

namespace engine3d {

/**
 * @brief System ECS dopasowujący wysokość obiektów do rzeźby terenu (Terrain Placement System).
 */
class TerrainPlacementSystem : public ISceneSystem {
public:
    TerrainPlacementSystem() = default;

    /**
     * @brief Wyznacza nową wysokość dla wszystkich encji posiadających TerrainPlacementComponent. Wywoływane co klatkę.
     */
    void update(Scene& scene, float deltaTime) override;
    
    const char* name() const override { return "TerrainPlacementSystem"; }
};

} // namespace engine3d
