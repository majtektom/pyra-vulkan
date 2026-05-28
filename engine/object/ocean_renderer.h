#pragma once

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

#include "math.h"

#include "types.h"

namespace engine3d {

class OceanRenderer {
public:
    OceanRenderer();
    ~OceanRenderer();

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
        VkDescriptorSet descriptorSet,
        const OceanConfig& config
    ) const;

    bool isReady() const { return ready_; }
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }

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

    static constexpr int kGridSize = 256;
    static constexpr float kOceanSize = 5500.0f;
    static constexpr float kOceanY = -18.0f;
};

}  // namespace engine3d
