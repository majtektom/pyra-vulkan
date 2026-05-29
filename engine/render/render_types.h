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
    virtual float getBumpIntensity() const = 0;
    virtual std::array<float,4> getColor() const = 0;
    virtual int getRenderPriority() const = 0;
};

struct RenderPacket {
    VkBuffer vertexBuffer;
    VkBuffer indexBuffer;
    VkBuffer skinVertexBuffer = VK_NULL_HANDLE;
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    uint32_t instanceCount = 0;
    uint32_t indexCount;
    Matrix4 transform;
    std::shared_ptr<MaterialInstance> material;
    std::string textureName;
    uint32_t diffuseTextureIndex = 0;
    uint32_t normalTextureIndex = 0;
    float bumpIntensity = 1.0f;
    std::array<float,4> materialColor{1,1,1,1};
    VkDescriptorSet boneDescriptorSet = VK_NULL_HANDLE;
    float distanceToCamera = 0.0f;
    int renderPriority = 0;
};

struct RenderView {
    Matrix4 view;
    Matrix4 projection;
    Vec3 cameraPosition;
    Vec3 cameraRight;
    Vec3 cameraUp;
};

class MeshManager;

class IRenderable {
public:
    virtual ~IRenderable() = default;
    virtual void collectRenderPackets(std::vector<RenderPacket>& packets, MeshManager& meshManager) = 0;
};

} // namespace engine3d
