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

// Renderer — rdzeń Vulkan: swapchain, descriptor sety, UBO, tekstury, rysowanie sceny.
class Renderer {
public:
    Renderer(Platform* platform, MaterialManager* materialManager, std::shared_ptr<VulkanContext> context);
    ~Renderer();

    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool initialize(const std::string& appName, bool enableValidationLayers = true);
    void cleanup();
    void render(const RenderScene& scene, const RenderView& view);
    void waitIdle();

    void addFeature(std::shared_ptr<IRenderFeature> feature);
    IRenderFeature* getFeature(const std::string& name);

    void loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat = false);
    void linkNormalMap(const std::string& diffuseName, const std::string& normalName);
    void setClearColor(float r, float g, float b, float a);

    void setSynchronizationMode(SynchronizationMode mode);
    SynchronizationMode synchronizationMode() const;
    void setPresentMode(PresentMode mode);
    PresentMode presentMode() const;

    void setLightDirection(float x, float y, float z, float ambient = 0.25f);
    void setLightDirection(const std::array<float, 4>& dir);

    void setShadowsEnabled(bool enabled);
    bool shadowsEnabled() const;

    void setLightColor(float r, float g, float b);

    void setLightingEnabled(bool enabled);
    bool isLightingEnabled() const;

    void setAmbientStrength(float strength);
    void setAmbientColor(float r, float g, float b);

    void clearLights();
    uint32_t addDirectionalLight(float dx, float dy, float dz, float r, float g, float b, float intensity);
    uint32_t addPointLight(float px, float py, float pz, float r, float g, float b, float intensity, float range);
    uint32_t addSpotLight(float px, float py, float pz, float dx, float dy, float dz,
                          float r, float g, float b, float intensity, float range,
                          float innerAngleDeg, float outerAngleDeg);

    uint32_t getTextureIndex(const std::string& name) const;
    void prepareRenderPacket(RenderPacket& packet) const;

    uint32_t width() const;
    uint32_t height() const;

    VkFormat colorFormat() const;
    VkFormat depthFormat() const;

    // Vulkan — potrzebne do inicjalizacji sub-rendererów (skybox, grid, text)
    VkDevice device() const;
    VkPhysicalDevice physicalDevice() const;
    VkDescriptorSetLayout descriptorSetLayout() const;
    VkCommandPool commandPool() const;
    VkQueue graphicsQueue() const;
    VkExtent2D extent() const;
    VkDescriptorSet getTextureDescriptorSet(const std::string& texName, uint32_t frame) const;
    VkBuffer getUniformBuffer(uint32_t frame) const;
    bool getTextureInfo(const std::string& name, VkImageView& outView, VkSampler& outSampler) const;

private:
    class RendererImpl;
    std::unique_ptr<RendererImpl> impl_;
};

} // namespace engine3d
