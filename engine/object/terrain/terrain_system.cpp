#include "object/terrain/terrain_system.h"
#include "object/terrain/terrain_component.h"
#include "camera/camera_component.h"
#include "object/terrain/terrain.h"
#include "core/entity.h"

namespace engine3d {

TerrainSystem::TerrainSystem(MeshManager& meshManager, MaterialManager& materialManager, MaterialSystem& materialSystem)
    : meshManager_(meshManager), materialManager_(materialManager), materialSystem_(materialSystem) {}

void TerrainSystem::update(Scene& scene, float) {
    CameraComponent* camera = scene.activeCamera();
    if (!camera) return;

    for (const auto& entity : scene.getEntities()) {
        if (!entity->isActive()) continue;
        
        auto* tc = entity->getComponent<TerrainComponent>();
        if (!tc || !tc->terrain) continue;

        // Aktualizuj LOD terenu na podstawie pozycji kamery
        tc->terrain->update(camera, meshManager_, tc->terrain->meshName());
    }
}

} // namespace engine3d
