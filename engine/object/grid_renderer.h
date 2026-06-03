#pragma once

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

#include "core/types.h"
#include "render/render_feature.h"

namespace engine3d {

class Renderer;

/**
 * @brief Sub-renderer odpowiedzialny za rysowanie siatki współrzędnych podłoża (Grid Renderer).
 *
 * Rysuje siatkę pomocniczą na płaszczyźnie poziomej (XZ), ułatwiając orientację przestrzenną
 * w edytorze 3D lub podczas testów sceny.
 *
 * Charakterystyka:
 * - Jest rysowana na etapie Stage::AfterOpaque (po obiektach nieprzezroczystych), dzięki czemu
 *   siatka nakłada się na otoczenie z uwzględnieniem testu głębokości (Depth Test).
 * - Generuje dynamicznie linie siatki na podstawie parametrów `size` i `spacing`.
 */
class GridRenderer : public IRenderFeature {
public:
    GridRenderer();
    ~GridRenderer() override;

    /**
     * @brief Inicjalizuje zasoby Vulkan potrzebne do narysowania siatki (tworzy potok, generuje bufor linii).
     */
    void init(Renderer* renderer) override;
    
    /**
     * @brief Renderuje siatkę w bieżącej klatce GPU.
     */
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    
    bool isActive() const override { return enabled_ && ready_; }
    const std::string& getName() const override { return name_; }
    Stage getStage() const override { return Stage::AfterOpaque; }
 
    /**
     * @brief Konfiguruje rozmiar linii, odstępy oraz kolory siatki pomocniczej.
     */
    void configure(const GridConfig& config, const std::string& shaderDir);
 
    /**
     * @brief Niszczy potok i zwalnia bufory wierzchołków na GPU.
     */
    void cleanup(VkDevice device);
 
    /**
     * @brief Nagrywa komendy wiązania i rysowania linii do bufora komend Vulkana.
     */
    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet descriptorSet,
        uint32_t frameIndex
    ) const;
 
    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }
 
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
    uint32_t vertexCount_ = 0; // Liczba wierzchołków tworzących linie siatki.
};
 
}  // namespace engine3d
