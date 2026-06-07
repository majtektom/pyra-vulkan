#pragma once

#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include "core/asset_registry.h"
#include "core/math.h"
#include "core/types.h"

namespace engine3d {

constexpr uint32_t kMaxTerrainLayers = 12;

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
    virtual TextureHandle getDiffuseHandle() const = 0;
    virtual float getBumpIntensity() const = 0;
    virtual std::array<float,4> getColor() const = 0;
    virtual int getRenderPriority() const = 0;
    virtual float getMetallicFactor() const = 0;
    virtual float getRoughnessFactor() const = 0;
    virtual TextureHandle getMetallicRoughnessHandle() const = 0;
    virtual TextureHandle getOcclusionHandle() const = 0;
    virtual TextureHandle getEmissiveHandle() const = 0;
    virtual TextureHandle getNormalHandle() const = 0;
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
    MaterialHandle materialHandle{};
    TextureHandle diffuseTextureHandle{};
    uint32_t diffuseTextureIndex = 0;
    TextureHandle normalTextureHandle{};
    uint32_t normalTextureIndex = 0;
    TextureHandle metallicRoughnessTextureHandle{};
    uint32_t metallicRoughnessTextureIndex = 0;
    TextureHandle occlusionTextureHandle{};
    uint32_t occlusionTextureIndex = 0;
    TextureHandle emissiveTextureHandle{};
    uint32_t emissiveTextureIndex = 0;
    bool layerBlendEnabled = false;
    uint32_t layerCount = 0;
    std::array<TextureHandle, kMaxTerrainLayers> layerTextureHandles{};
    std::array<TextureHandle, kMaxTerrainLayers> layerMaskHandles{};
    std::array<uint32_t, kMaxTerrainLayers> layerTextureIndices{};
    std::array<uint32_t, kMaxTerrainLayers> layerMaskIndices{};
    float layerWorldSize = 1.0f;
    float bumpIntensity = 1.0f;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    std::array<float,4> materialColor{1,1,1,1};
    TextureHandle heightmapTextureHandle{};
    uint32_t heightmapTextureIndex = 0;
    float scaleY = 1.0f;
    float tessellationFactor = 1.0f;
    bool heightmapEnabled = false;
    VkDescriptorSet boneDescriptorSet = VK_NULL_HANDLE;
    float distanceToCamera = 0.0f;
    int renderPriority = 0;
    bool forwardOpaquePass = false;
    bool castShadows = true;
};

struct RenderView {
    Matrix4 view;
    Matrix4 projection;
    Vec3 cameraPosition;
    Vec3 cameraRight;
    Vec3 cameraUp;
};

class MeshManager;

} // namespace engine3d
