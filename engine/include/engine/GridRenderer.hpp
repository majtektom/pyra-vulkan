#pragma once

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

#include "engine/Types.hpp"

namespace engine3d {

// GridRenderer rysuje pomocnicza siatke na plaszczyznie XZ.
// Odpowiednik Plazer::Grid3d ze starego projektu (Grid.h/Grid.cpp).
// Kazda instancja tworzy wlasny pipeline Vulkan i bufor wierzcholkow.
class GridRenderer {
public:
    GridRenderer();
    ~GridRenderer();

    // Tworzy pipeline, layout i bufor wierzcholkow na GPU.
    // device, physicalDevice, renderPass, commandPool, graphicsQueue - zasoby Vulkan.
    // extent - rozmiar swapchaina (do viewportu).
    // config - ustawienia siatki (rozmiar, kolor).
    // shaderDir - katalog ze skompilowanymi shaderami grid.vert.spv / grid.frag.spv.
    void init(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VkExtent2D extent,
        const GridConfig& config,
        const std::string& shaderDir
    );

    // Niszczy wszystkie zasoby Vulkan.
    void cleanup(VkDevice device);

    // Nagrywa polecenia rysowania linii siatki.
    // Nalezy wywolac wewnatrz aktywnego render passu.
    // pipelineLayout - layout pipeline'u (musi zgadzac sie z descriptorSetLayout z init).
    // descriptorSet - deskryptor z UBO (binding 0) + tekstura (binding 1, moze byc biala).
    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
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

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t vertexCount_ = 0;
};

}  // namespace engine3d
