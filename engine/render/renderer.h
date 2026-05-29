#pragma once

#include <memory>
#include <string>
#include <functional>

#include "types.h"
#include "math.h"
#include "render_pass.h"

namespace engine3d {

class Entity;
class CameraComponent;
class MaterialManager;
class ShaderManager;
class TextureManager;
class Platform;
class MeshComponent;
class TransformComponent;
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
