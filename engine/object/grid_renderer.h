#pragma once
 
#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>
 
#include "types.h"
#include "../render/render_feature.h"
 
namespace engine3d {
 
class Renderer;
 
// GridRenderer rysuje pomocnicza siatke na plaszczyznie XZ.
// Odpowiednik Plazer::Grid3d ze starego projektu (Grid.h/Grid.cpp).
// Kazda instancja tworzy wlasny pipeline Vulkan i bufor wierzcholkow.
class GridRenderer : public IRenderFeature {
public:
    GridRenderer();
    ~GridRenderer() override;
 
    // Metody IRenderFeature
    void init(Renderer* renderer) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    bool isActive() const override { return enabled_ && ready_; }
    const std::string& getName() const override { static std::string name = "GridRenderer"; return name; }
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
