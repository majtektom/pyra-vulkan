#pragma once
#include "core/component.h"

namespace engine3d {

/**
 * @brief Komponent ECS odpowiadający za automatyczne przyklejanie obiektu do wysokości terenu (Terrain Placement).
 */
class TerrainPlacementComponent : public Component {
public:
    TerrainPlacementComponent() : Component("TerrainPlacement") {}
    explicit TerrainPlacementComponent(float offset) : Component("TerrainPlacement"), yOffset(offset) {}

    float yOffset = 0.0f;
};

} // namespace engine3d
