#include "object/terrain/terrain_placement_system.h"
#include "object/terrain/terrain_component.h"
#include "object/terrain/terrain_placement_component.h"
#include "object/terrain/terrain.h"
#include "core/entity.h"

namespace engine3d {

void TerrainPlacementSystem::update(Scene& scene, float) {
    // Znajdź TerrainComponent na scenie
    TerrainComponent* tc = nullptr;
    for (const auto& entity : scene.getEntities()) {
        if (entity->isActive()) {
            tc = entity->getComponent<TerrainComponent>();
            if (tc && tc->terrain) {
                break;
            }
        }
    }

    if (!tc || !tc->terrain) {
        return; // Brak terenu na scenie
    }

    // Dopasuj wysokość wszystkich encji posiadających TerrainPlacementComponent
    for (const auto& entity : scene.getEntities()) {
        if (!entity->isActive()) continue;
        
        if (auto* tpc = entity->getComponent<TerrainPlacementComponent>()) {
            auto& transform = entity->transform();
            transform.position.y = tc->terrain->getHeight(transform.position.x, transform.position.z) + tpc->yOffset;
        }
    }
}

} // namespace engine3d
