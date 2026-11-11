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

/**
 * @brief Sub-renderer odpowiedzialny za renderowanie tła nieba (Skybox).
 *
 * Skybox to duży sześcian otaczający całą scenę, na którego ścianach wyrenderowane są tekstury otoczenia.
 * Aby stworzyć złudzenie nieskończonej odległości nieba:
 * 1. Skybox jest rysowany przed obiektami nieprzezroczystymi (Stage::BeforeOpaque).
 * 2. W shaderze wierzchołków jego pozycja jest przesunięta o pozycję kamery, tak że kamera zawsze znajduje się w jego geometrycznym środku.
 * 3. Zapis do bufora głębokości (Depth Write) jest wyłączony, dzięki czemu nie zasłania on rysowanej później geometrii świata.
 *
 * Klasa dziedziczy z IRenderFeature i zarządza własnym logicznym potokiem (VkPipeline).
 */
class SkyboxRenderer : public IRenderFeature {
public:
    SkyboxRenderer();
    ~SkyboxRenderer() override;

    /**
     * @brief Inicjalizuje zasoby Vulkan dla skyboxa (tworzy potok Pipeline, bufor wierzchołków sześcianu).
     */
    void init(Renderer* renderer) override;
    
    /**
     * @brief Wywołanie rysujące skyboxa w bieżącej klatce GPU.
     */
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    
    bool isActive() const override { return enabled_ && ready_; }
    const std::string& getName() const override { return name_; }
    Stage getStage() const override { return Stage::BeforeOpaque; }
 
    /**
     * @brief Konfiguruje rozmiar i właściwości skyboxa na podstawie dostarczonej konfiguracji.
     */
    void configure(const SkyboxConfig& config, const std::string& shaderDir);
 
    /**
     * @brief Zwalnia bufory i zasoby potoku na GPU.
     */
    void cleanup(VkDevice device);
 
    /**
     * @brief Nagrywa komendy rysowania do bufora komend (Command Buffer) Vulkana.
     */
    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout skyboxPipelineLayout,
        const Vec3& cameraPosition,
        const uint32_t indices[3]
    ) const;
 
    void setEnabled(bool e) { enabled_ = e; }
    [[nodiscard]] bool isReady() const { return ready_; }
 
    void setExposure(float e) { exposure_ = e; }
    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }

    /**
     * @brief Ustawia uchwyty do tekstur odpowiadających za ściany skyboxa.
     * @param fb Tekstura przód/tył (front/back).
     * @param lr Tekstura lewo/prawo (left/right).
     * @param top Tekstura górna (top).
     */
    void setTextureHandles(TextureHandle fb, TextureHandle lr, TextureHandle top);
 
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
 
    // Struktura stałych wypychanych (Push Constants) przesyłanych bezpośrednio i bardzo szybko na GPU przed rysowaniem:
    struct PushConstants {
        float cameraPosition[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // Pozycja kamery (środek skyboxa).
        float exposure = 1.0f;                               // Jasność/Ekspozycja nieba.
        uint32_t skyboxIndex = 0;                            // Indeks używanej tekstury.
        uint32_t pad[2];                                     // Dopełnienie do 16 bajtów (wymóg wyrównania danych w Vulkanie).
    };
 
    float exposure_ = 0.7f;
    TextureHandle textureFb_{};
    TextureHandle textureLr_{};
    TextureHandle textureTop_{};
 
    static constexpr int kFaceMap[5] = {0, 1, 0, 1, 2}; // Mapowanie ścian sześcianu na indeksy tekstur.
};
 
}  // namespace engine3d
