#pragma once

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

#include "core/types.h"
#include "render/render_feature.h"

namespace engine3d {

class Renderer;

class GridRenderer : public IRenderFeature {
public:
    GridRenderer();
    ~GridRenderer() override;

    void init(Renderer* renderer) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    bool isActive() const override { return enabled_ && ready_; }
    const std::string& getName() const override { return name_; }
    Stage getStage() const override { return Stage::AfterOpaque; }
 
    // Konfiguracja
    void configure(const GridConfig& config, const std::string& shaderDir);
 
    // Niszczy wszystkie zasoby Vulkan.
    void cleanup(VkDevice device);
 
    // Nagrywa polecenia rysowania linii siatki.
    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet descriptorSet,
        uint32_t frameIndex
    ) const;
 
    // Zwraca layout pipeline'u (potrzebny do wiazania deskryptorow).
    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }
 
    // Czy siatka jest wlaczona i zainicjalizowana?
    [[nodiscard]] bool isReady() const { return ready_; }
    [[nodiscard]] bool isEnabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }
 
private:
    std::string name_;
    bool enabled_ = false;
    bool ready_ = false;
    Renderer* renderer_ = nullptr;
    GridConfig config_;
    std::string shaderDir_;
 
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t vertexCount_ = 0;
};
 
}  // namespace engine3d
