#include "object/terrain/terrain_component.h"
#include "core/entity.h"
#include "core/mesh_manager.h"
#include "core/math.h"
#include "object/terrain/terrain.h"

namespace engine3d {

TerrainComponent::TerrainComponent() : Component("Terrain") {}

TerrainComponent::TerrainComponent(std::shared_ptr<Terrain> terrainPtr)
    : Component("Terrain"), terrain(terrainPtr) {}

void TerrainComponent::collectRenderPackets(std::vector<RenderPacket>& packets, MeshManager& meshManager, bool tessellationActive, float tessellationLevel) {
    if (!isActive() || !owner || !terrain) return;

    // Zbierz standardowe pakiety terenu
    terrain->collectRenderPackets(packets, meshManager, owner->transform().getWorldMatrix(),
                                 materialHandle, materialInstance, castShadows,
                                 layerBlendEnabled, layerCount, layerTextureHandles,
                                 layerMaskHandles, layerWorldSize,
                                 heightmapTextureHandle, heightmapEnabled, tessellationActive, tessellationLevel);

}

} // namespace engine3d
