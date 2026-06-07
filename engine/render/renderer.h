#pragma once

#include <memory>
#include <string>
#include <functional>

#include "core/types.h"
#include "core/math.h"
#include "render/render_pass.h"

namespace engine3d {

class Entity;
class CameraComponent;
class MaterialManager;
class ShaderManager;
class TextureManager;
class Platform;
class MeshComponent;
class VulkanContext;
class IRenderFeature;
class AssetRegistry;

/**
 * @brief Główny interfejs niskopoziomowego systemu renderowania (Renderer).
 *
 * Renderer pośredniczy w komunikacji z GPU przy użyciu interfejsu API Vulkan. 
 * Odpowiada za:
 * - Zarządzanie łańcuchem wymiany obrazów (Swapchain) i synchronizacją klatek na GPU.
 * - Alokację zasobów, takich jak shadery, tekstury, bufory wierzchołków/indeksów oraz Uniform Buffers (UBO).
 * - Pętlę rysowania sceny trójwymiarowej.
 * - Obsługę technik takich jak mapowanie cieni (Shadow Mapping) i oświetlenie.
 *
 * Wykorzystuje wzorzec projektowy Pimpl (Pointer to Implementation) poprzez wskaźnik impl_,
 * co ukrywa bezpośrednie zależności od wewnętrznych struktur Vulkan i przyspiesza kompilację kodu.
 */
class Renderer {
public:
    static constexpr uint32_t kMaxFrames = 3;

    Renderer(
        Platform* platform,
        MaterialManager* materialManager,
        std::shared_ptr<VulkanContext> context,
        AssetRegistry* assetRegistry);
    ~Renderer();

    // Blokujemy kopiowanie i przenoszenie renderea w celu zachowania spójności zasobów GPU.
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /**
     * @brief Inicjalizuje potok renderowania, ładuje podstawowe rozszerzenia i konfiguruje zasoby GPU.
     */
    bool initialize(const std::string& appName, bool enableValidationLayers = true);
    
    /**
     * @brief Zwalnia wszystkie zasoby przydzielone na GPU.
     */
    void cleanup();
    
    /**
     * @brief Rysuje klatkę sceny na podstawie wyekstrahowanych paczek geometrii i widoku kamery.
     * @param scene Dane geometryczne i światła widoczne w danej klatce.
     * @param view Parametry rzutowania kamery (macierze View i Projection).
     */
    void render(const RenderScene& scene, const RenderView& view);
    
    /**
     * @brief Czeka na zakończenie wszystkich operacji wykonywanych aktualnie przez GPU.
     * Używane przed niszczeniem zasobów lub rekonfiguracją okna.
     */
    void waitIdle();

    /**
     * @brief Rejestruje dodatkową funkcję graficzną (np. Skybox, Ocean, GUI).
     */
    void addFeature(std::shared_ptr<IRenderFeature> feature);
    IRenderFeature* getFeature(const std::string& name);
    void removeFeature(const std::string& name);

    /**
     * @brief Ładuje teksturę z pliku dyskowego do pamięci GPU i zwraca unikalny uchwyt.
     * @param name Identyfikator tekstury.
     * @param path Ścieżka do pliku graficznego.
     * @param mirroredRepeat Czy włączyć tryb powtarzania lustrzanego tekstury (dla np. terenu).
     * @param disableAlpha Czy wyłączyć czytanie kanału alfa tekstury (wymusza pełną nieprzezroczystość).
     */
    TextureHandle loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat = false, bool disableAlpha = false);
    TextureHandle loadTextureFromMemory(const std::string& name, const std::vector<float>& data, uint32_t width, uint32_t height, bool mirroredRepeat = false);
    
    // Konfiguracja koloru tła (czyszczenia ekranu).
    void setClearColor(float r, float g, float b, float a);

    // Konfiguracja trybów synchronizacji (np. V-Sync).
    void setSynchronizationMode(SynchronizationMode mode);
    SynchronizationMode synchronizationMode() const;
    void setPresentMode(PresentMode mode);
    PresentMode presentMode() const;

    // Kontrola generowania cieni w silniku.
    void setShadowsEnabled(bool enabled);
    bool shadowsEnabled() const;
    void setWireframeEnabled(bool enabled);
    bool isWireframeEnabled() const;
    void setTessellationEnabled(bool enabled);
    bool isTessellationEnabled() const;
    bool isTessellationSupported() const;
    void setTessellationLevel(float level);
    float getTessellationLevel() const;
    void setTextureFilter(TextureFilter filter);
    TextureFilter getTextureFilter() const;

    // Konfiguracja ortogonalnej przestrzeni cieniowania światła kierunkowego.
    void configureDirectionalShadow(float halfSize);

    // Włączanie/wyłączanie oświetlenia dynamicznego na scenie.
    void setLightingEnabled(bool enabled);
    bool isLightingEnabled() const;

    // Sterowanie oświetleniem otoczenia (Ambient light).
    void setAmbientStrength(float strength);
    void setAmbientColor(float r, float g, float b);

    // Sterowanie mgłą (Fog)
    void setFogEnabled(bool enabled);
    bool isFogEnabled() const;
    void setFogColor(float r, float g, float b);
    void setFogStart(float start);
    void setFogEnd(float end);
    void setFogDensity(float density);

    // Przesyła do renderera światła wyekstrahowane ze sceny w bieżącej klatce.
    void setLights(const std::vector<SceneLight>& lights);

    // Pobiera wewnętrzny indeks deskryptora tekstury na GPU na podstawie uchwytu.
    uint32_t getTextureIndex(TextureHandle handle) const;
    
    // Przygotowuje dane geometryczne paczki renderowania (np. tworzy i binuje bufory wierzchołków na GPU).
    void prepareRenderPacket(RenderPacket& packet) const;

    // Zwraca szerokość i wysokość okna renderowania.
    uint32_t width() const;
    uint32_t height() const;

    // Zwraca formaty tekstur używane w łańcuchu wymiany.
    VkFormat colorFormat() const;
    VkFormat depthFormat() const;

    // Niskopoziomowy dostęp do zasobów Vulkan na potrzeby sub-rendererów i rozszerzeń:
    VkDevice device() const;
    VkPhysicalDevice physicalDevice() const;
    VkDescriptorSetLayout descriptorSetLayout() const;
    VkCommandPool commandPool() const;
    VkQueue graphicsQueue() const;
    VkExtent2D extent() const;
    VkDescriptorSet getTextureDescriptorSet(const std::string& texName, uint32_t frame) const;
    VkBuffer getUniformBuffer(uint32_t frame) const;
    bool getTextureInfo(const std::string& name, VkImageView& outView, VkSampler& outSampler) const;
    bool getTextureInfo(TextureHandle handle, VkImageView& outView, VkSampler& outSampler) const;

private:
    class RendererImpl;              // Wewnętrzna klasa implementacji niskopoziomowej Vulkana.
    std::unique_ptr<RendererImpl> impl_;
};

} // namespace engine3d
