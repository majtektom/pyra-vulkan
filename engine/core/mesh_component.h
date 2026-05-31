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
    std::string textureName;
    std::string materialName;

    std::shared_ptr<MaterialInstance> materialInstance;
    VkBuffer skinVertexBuffer = VK_NULL_HANDLE;
    VkDescriptorSet boneDescriptorSet = VK_NULL_HANDLE;
};

} // namespace engine3d
