#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "core/math.h"
#include "render/render_feature.h"

namespace engine3d {

class Renderer;

struct LocalFogVolume {
    Vec3 center{0.0f, 0.0f, 0.0f};
    float density = 0.004f;
    Vec3 halfExtents{100.0f, 20.0f, 100.0f};
    float edgeFade = 0.25f;
};

class VolumetricFogRenderer : public IRenderFeature {
public:
    static constexpr uint32_t kFroxelX = 160;
    static constexpr uint32_t kFroxelY = 90;
    static constexpr uint32_t kFroxelZ = 64;
    static constexpr uint32_t kMaxLocalVolumes = 32;

    VolumetricFogRenderer();
    ~VolumetricFogRenderer() override;

    void init(Renderer* renderer) override;
    void preRender(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    bool isActive() const override { return enabled_ && ready_; }
    const std::string& getName() const override { return name_; }
    Stage getStage() const override { return Stage::AfterTransparent; }

    void configure(const std::string& shaderDir);
    void cleanup(VkDevice device);
    void setEnabled(bool e) { enabled_ = e; }
    void setFogDensity(float d) { fogDensity_ = d; }
    void setHeightFalloff(float f) { fogHeightFalloff_ = f; }
    void setSeaLevel(float s) { seaLevel_ = s; }
    void setFogRadius(float r) { fogRadius_ = r; }
    void setLocalVolumes(std::vector<LocalFogVolume> volumes);
    const std::vector<LocalFogVolume>& localVolumes() const { return localVolumes_; }
    float getFogDensity() const { return fogDensity_; }
    float getHeightFalloff() const { return fogHeightFalloff_; }
    float getSeaLevel() const { return seaLevel_; }
    float getFogRadius() const { return fogRadius_; }

private:
    void createVolumeTexture(VkDevice device, VkPhysicalDevice physicalDevice);
    void createLocalVolumeBuffer(VkDevice device, VkPhysicalDevice physicalDevice);
    void createComputePipeline(VkDevice device);
    void createCompositePipeline(VkDevice device);
    void writeDescriptorSets(VkDevice device);

    std::string name_;
    bool enabled_ = false;
    bool ready_ = false;
    Renderer* renderer_ = nullptr;
    std::string shaderDir_;

    VkPipelineLayout computePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline computePipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet computeDescriptorSet_ = VK_NULL_HANDLE;

    VkPipelineLayout compositePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout compositeDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet compositeDescriptorSet_ = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    VkImage volumeImage_ = VK_NULL_HANDLE;
    VkDeviceMemory volumeImageMemory_ = VK_NULL_HANDLE;
    VkImageView volumeImageView_ = VK_NULL_HANDLE;
    VkSampler volumeSampler_ = VK_NULL_HANDLE;
    VkBuffer localVolumeBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory localVolumeBufferMemory_ = VK_NULL_HANDLE;
    void* localVolumeBufferMapped_ = nullptr;

    float fogDensity_ = 0.002f;
    float fogHeightFalloff_ = 0.004f;
    float seaLevel_ = 0.0f;
    float fogRadius_ = 1500.0f;
    std::vector<LocalFogVolume> localVolumes_;
};

} // namespace engine3d
