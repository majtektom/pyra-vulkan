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
    void setFogColor(const Vec3& color) { fogColor_ = color; }
    // Kierunek promieni słońca (od słońca do sceny), jak SceneLight::direction światła kierunkowego.
    void setSunDirection(const Vec3& direction) { sunDirection_ = normalize(direction); }
    void setLocalVolumes(std::vector<LocalFogVolume> volumes);
    const std::vector<LocalFogVolume>& localVolumes() const { return localVolumes_; }
    const Vec3& getSunDirection() const { return sunDirection_; }

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
    Vec3 fogColor_{0.575f, 0.68f, 0.88f};
    Vec3 sunDirection_ = normalize(Vec3{0.32f, -0.77f, 0.56f}); // ~50° nad horyzontem
    std::vector<LocalFogVolume> localVolumes_;
    bool localVolumesDirty_ = true; // bufor GPU wymaga ponownego wypełnienia
};

} // namespace engine3d
