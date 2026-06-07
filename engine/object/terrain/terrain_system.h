#pragma once

#include "scene/scene.h"
#include "core/mesh_manager.h"
#include "render/material_manager.h"
#include "render/material_system.h"

namespace engine3d {

/**
 * @brief System ECS aktualizujący poziomy szczegółowości terenu (Terrain System).
 */
class TerrainSystem : public ISceneSystem {
public:
    TerrainSystem(MeshManager& meshManager, MaterialManager& materialManager, MaterialSystem& materialSystem);

    void update(Scene& scene, float deltaTime) override;
    const char* name() const override { return "TerrainSystem"; }

private:
    MeshManager& meshManager_;
    MaterialManager& materialManager_;
    MaterialSystem& materialSystem_;
};

} // namespace engine3d
