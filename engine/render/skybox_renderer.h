#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "types.h"

namespace engine3d {

// SkyboxRenderer rysuje niebo jako piec scian (brak podlogi) z teksturami DDS.
// Odpowiednik Plazer::CSkyBox ze starego projektu (skyBox.h/skyBox.cpp).
// Kazda instancja tworzy wlasny pipeline Vulkan, bufory vertex/index i zarzadza teksturami.
class SkyboxRenderer {
public:
    SkyboxRenderer();
    ~SkyboxRenderer();

    // Tworzy pipeline Vulkan, bufory geometrii.
    // device, physicalDevice, renderPass, descriptorSetLayout - podstawowe zasoby Vulkan.
    // commandPool, graphicsQueue - do uploadu danych na GPU.
    // extent - rozmiar swapchaina.
    // config - ustawienia skyboxa (katalog, rozmiar).
    // shaderDir - katalog ze skybox.vert.spv / skybox.frag.spv.
    void init(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VkExtent2D extent,
        const SkyboxConfig& config,
        const std::string& shaderDir
    );

    // Niszczy wszystkie zasoby.
    void cleanup(VkDevice device);

    // Nagrywa polecenia rysowania skyboxa.
    // Nalezy wywolac wewnatrz aktywnego render passu, PRZED obiektami sceny.
    // skyboxPipelineLayout - layout pipeline'u.
    // faceSets - deskryptory dla trzech par scian (front+back, left+right, top)
    //            z UBO i tekstura dla BIEZACEJ klatki.
    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout skyboxPipelineLayout,
        const Vec3& cameraPosition,
        const std::array<VkDescriptorSet, 3>& faceSets
    ) const;

    [[nodiscard]] bool isReady() const { return ready_; }
    [[nodiscard]] bool isEnabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }

    void setExposure(float e) { exposure_ = e; }
    [[nodiscard]] float getExposure() const { return exposure_; }
    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }

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

    struct PushConstants {
        alignas(16) float cameraPosition[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float exposure = 1.0f;
    };

    float exposure_ = 0.7f;

    // W Vulkanowym ukladzie sceny to mapowanie daje poprawniejsze sasiedztwo scian
    // niz doslowne przepisanie starego switch(i) z OpenGL.
    // front=0, back=1 -> box_12, left=2, right=3 -> box_34, top=4 -> box_5.
    static constexpr int kFaceMap[5] = {0, 1, 0, 1, 2};
};

}  // namespace engine3d
