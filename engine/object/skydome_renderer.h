#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

#include "math.h"
#include "types.h"

namespace engine3d {

struct SkydomePushConstants {
    float cameraPosition[4];
    float time;
    float domeScale;
    float cloudCoverage;
    float cloudSpeed0;
    float cloudSpeed1;
    float cloudScale0;
    float cloudScale1;
    float sunDirX;
    float sunDirY;
    float sunDirZ;
    float sunSize;
    float _padX;
    float skyTopColor[4];
    float skyHorizonColor[4];
    float sunColor[4];
};

void computeSkyColors(float timeOfDay,
                      std::array<float, 4>& topColor,
                      std::array<float, 4>& horizonColor,
                      std::array<float, 4>& sunColorOut);

class SkydomeRenderer {
public:
    SkydomeRenderer();
    ~SkydomeRenderer();

    void init(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VkExtent2D extent,
        const std::string& shaderDir
    );

    void cleanup(VkDevice device);

    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        const Vec3& cameraPosition,
        float time,
        VkDescriptorSet cloudDescriptorSet,
        const SkydomeConfig& config
    ) const;

    bool isReady() const { return ready_; }
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }

private:
    bool enabled_ = false;
    bool ready_ = false;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;

    static constexpr int kSegments = 30;
};

}  // namespace engine3d
