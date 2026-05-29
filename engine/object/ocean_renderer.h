#pragma once

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

#include "math.h"
#include "types.h"
#include "../render/render_feature.h"

namespace engine3d {

class Renderer;

class OceanRenderer : public IRenderFeature {
public:
    OceanRenderer();
    ~OceanRenderer() override;

    void init(Renderer* renderer) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    bool isActive() const override { return enabled_ && ready_; }
    const std::string& getName() const override { static std::string name = "OceanRenderer"; return name; }
    Stage getStage() const override { return Stage::AfterTransparent; }

    void configure(const OceanConfig& config, const std::string& shaderDir, const std::string& descName);
    void cleanup(VkDevice device);

    void setEnabled(bool e) { enabled_ = e; }
    void setTime(float t) { time_ = t; }
    bool isReady() const { return ready_; }

private:
    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        const Vec3& cameraPosition,
        float time,
        VkDescriptorSet descriptorSet,
        const OceanConfig& config
    ) const;

    bool enabled_ = false;
    bool ready_ = false;
    Renderer* renderer_ = nullptr;
    OceanConfig config_;
    std::string shaderDir_;
    std::string descName_;
    float time_ = 0.0f;

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
