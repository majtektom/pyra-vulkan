#pragma once

#include <string>
#include <vector>
#include <memory>

#include "component.h"
#include "types.h"
#include "../render/render_types.h"

namespace engine3d {

class MeshComponent : public Component, public IRenderable {
public:
    MeshComponent();
    explicit MeshComponent(const std::string& meshName);
    explicit MeshComponent(const MeshData& meshData);

    void collectRenderPackets(std::vector<RenderPacket>& packets, MeshManager& meshManager) override;

    std::string meshName;
    MeshData meshData;
    std::string textureName;
    std::string materialName;
    
    std::shared_ptr<MaterialInstance> materialInstance;
    VkBuffer skinVertexBuffer = VK_NULL_HANDLE;
    VkDescriptorSet boneDescriptorSet = VK_NULL_HANDLE;
};

} // namespace engine3d
