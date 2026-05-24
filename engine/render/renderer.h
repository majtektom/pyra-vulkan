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

// Renderer — rdzeń Vulkan: swapchain, descriptor sety, UBO, tekstury, rysowanie sceny.
// Nie zawiera skyboxa, grida, tekstu ani fontów — tym zarządza Engine.
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
    void render(const RenderScene& scene, CameraComponent* camera,
                std::function<void(VkCommandBuffer, uint32_t)> onAfterOpaque = nullptr,
                std::function<void(VkCommandBuffer, uint32_t)> onAfterTransparent = nullptr);
    void waitIdle();

    void loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat = false);
    void linkNormalMap(const std::string& diffuseName, const std::string& normalName);
    void setClearColor(float r, float g, float b, float a);

    uint32_t width() const;
    uint32_t height() const;

    // Vulkan — potrzebne do inicjalizacji sub-rendererów (skybox, grid, text)
    VkDevice device() const;
    VkPhysicalDevice physicalDevice() const;
    VkRenderPass renderPass() const;
    VkDescriptorSetLayout descriptorSetLayout() const;
    VkCommandPool commandPool() const;
    VkQueue graphicsQueue() const;
    VkExtent2D extent() const;
    VkDescriptorSet getTextureDescriptorSet(const std::string& texName, uint32_t frame) const;

private:
    class RendererImpl;
    std::unique_ptr<RendererImpl> impl_;
};

} // namespace engine3d
