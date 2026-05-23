#pragma once

#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include "math.h"
#include "types.h"

namespace engine3d {

struct VulkanPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

class MaterialInstance {
public:
    virtual ~MaterialInstance() = default;
    virtual void bind(VkCommandBuffer commandBuffer, uint32_t currentFrame) = 0;
    virtual const VulkanPipeline& getPipeline() const = 0;
    virtual bool isTransparent() const = 0;
    virtual const std::string& getTextureName() const = 0;
};

struct RenderPacket {
    VkBuffer vertexBuffer;
    VkBuffer indexBuffer;
    uint32_t indexCount;
    Matrix4 transform;
    std::shared_ptr<MaterialInstance> material;
    std::string textureName;
    // For sorting
    float distanceToCamera = 0.0f;
};

class MeshManager;

class IRenderable {
public:
    virtual ~IRenderable() = default;
    virtual void collectRenderPackets(std::vector<RenderPacket>& packets, MeshManager& meshManager) = 0;
};

} // namespace engine3d
