#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

#include "core/math.h"
#include "core/types.h"
#include "render/render_feature.h"

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

class Renderer;

class SkydomeRenderer : public IRenderFeature {
public:
    SkydomeRenderer();
    ~SkydomeRenderer() override;

    void init(Renderer* renderer) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    bool isActive() const override { return enabled_ && ready_; }
    const std::string& getName() const override { return name_; }
    Stage getStage() const override { return Stage::BeforeOpaque; }

    // Manual config
    void configure(const SkydomeConfig& config, const std::string& shaderDir, const std::string& descName);
    void cleanup(VkDevice device);
    void setEnabled(bool e) { enabled_ = e; }
    void setTime(float t) { time_ = t; }

    VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }

private:
    std::string name_;
    bool enabled_ = false;
    bool ready_ = false;
    SkydomeConfig config_;
    std::string shaderDir_;
    std::string descName_;
    float time_ = 0.0f;
    Renderer* renderer_ = nullptr;
    bool lastWireframe_ = false;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;

    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        const Vec3& cameraPosition,
        float time,
        VkDescriptorSet cloudDescriptorSet,
        const SkydomeConfig& config
    ) const;

    static constexpr int kSegments = 30;
};

}  // namespace engine3d
