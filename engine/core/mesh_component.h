#pragma once

#include <string>
#include <vector>
#include <memory>

#include "core/component.h"
#include "core/types.h"
#include "render/render_types.h"

namespace engine3d {

class MeshComponent : public Component {
public:
    MeshComponent();
    explicit MeshComponent(const std::string& meshName);
    explicit MeshComponent(const MeshData& meshData);

    void collectRenderPackets(std::vector<RenderPacket>& packets, MeshManager& meshManager);

    std::string meshName;
    MeshData meshData;
    MaterialHandle materialHandle{};
    bool castShadows = true;
    bool layerBlendEnabled = false;
    uint32_t layerCount = 0;
    std::array<TextureHandle, kMaxTerrainLayers> layerTextureHandles{};
    std::array<TextureHandle, kMaxTerrainLayers> layerMaskHandles{};
    float layerWorldSize = 1.0f;

    std::shared_ptr<MaterialInstance> materialInstance;
    VkBuffer skinVertexBuffer = VK_NULL_HANDLE;
    VkDescriptorSet boneDescriptorSet = VK_NULL_HANDLE;

private:
    std::shared_ptr<TerrainSurface> surface_;   // warstwy tekstur dla paczek (gdy layerBlendEnabled)
};

} // namespace engine3d
