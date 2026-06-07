#pragma once

#include <string>
#include <vector>
#include <memory>
#include <array>

#include "core/component.h"
#include "core/types.h"
#include "render/render_types.h"

namespace engine3d {

class Terrain;

/**
 * @brief Komponent terenu 3D (Terrain Component) przechowujący właściwości materiałowe i dane terenu.
 */
class TerrainComponent : public Component {
public:
    TerrainComponent();
    explicit TerrainComponent(std::shared_ptr<Terrain> terrainPtr);

    void collectRenderPackets(std::vector<RenderPacket>& packets, MeshManager& meshManager);

    std::shared_ptr<Terrain> terrain;
    MaterialHandle materialHandle{};
    std::shared_ptr<MaterialInstance> materialInstance;
    bool castShadows = true;
    bool layerBlendEnabled = false;
    uint32_t layerCount = 0;
    std::array<TextureHandle, kMaxTerrainLayers> layerTextureHandles{};
    std::array<TextureHandle, kMaxTerrainLayers> layerMaskHandles{};
    float layerWorldSize = 1.0f;
};

} // namespace engine3d
