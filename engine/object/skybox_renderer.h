#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "core/types.h"
#include "render/render_feature.h"

namespace engine3d {

class Renderer;

class SkyboxRenderer : public IRenderFeature {
public:
    SkyboxRenderer();
    ~SkyboxRenderer() override;

    void init(Renderer* renderer) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    bool isActive() const override { return enabled_ && ready_; }
    const std::string& getName() const override { return name_; }
    Stage getStage() const override { return Stage::BeforeOpaque; }
 
    // Konfiguracja
    void configure(const SkyboxConfig& config, const std::string& shaderDir);
 
    // Niszczy wszystkie zasoby.
    void cleanup(VkDevice device);
 
    // Nagrywa polecenia rysowania skyboxa.
    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout skyboxPipelineLayout,
        const Vec3& cameraPosition,
        const uint32_t indices[3]
    ) const;
 
    void setEnabled(bool e) { enabled_ = e; }
    [[nodiscard]] bool isEnabled() const { return enabled_; }
    [[nodiscard]] bool isReady() const { return ready_; }
 
    void setExposure(float e) { exposure_ = e; }
    [[nodiscard]] float getExposure() const { return exposure_; }
    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }
 
private:
    std::string name_;
    bool enabled_ = false;
    bool ready_ = false;
    Renderer* renderer_ = nullptr;
    SkyboxConfig config_;
    std::string shaderDir_;
 
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;
 
    struct PushConstants {
        float cameraPosition[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float exposure = 1.0f;
        uint32_t skyboxIndex = 0;
        uint32_t pad[2];
    };
 
    float exposure_ = 0.7f;
 
    static constexpr int kFaceMap[5] = {0, 1, 0, 1, 2};
};
 
}  // namespace engine3d
