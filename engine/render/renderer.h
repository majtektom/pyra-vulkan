#pragma once

#include <memory>
#include <string>
#include <vector>

#include "types.h"
#include "math.h"
#include "render_pass.h"

namespace engine3d {

class Entity;
class CameraComponent;
class MaterialManager;
class ShaderManager;
class TextureManager;
class FontManager;
class Platform;
class MeshComponent;
class TransformComponent;
class VulkanContext;

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
    void render(const RenderScene& scene, CameraComponent* camera);
    void waitIdle();

    void setCamera(CameraComponent* camera);
    void setShaderManager(ShaderManager* manager);
    void setTextureManager(TextureManager* manager);
    void setFontManager(FontManager* manager);

    void loadMesh(const std::string& name, const MeshData& meshData);
    void loadDynamicMesh(const std::string& name, const MeshData& meshData);
    void updateDynamicMesh(const std::string& name, const MeshData& meshData);
    void setDynamicMeshTexture(const std::string& meshName, const std::string& textureName);
    void loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat = false);
    void loadFont(const std::string& name, const std::string& path, uint32_t pixelHeight = 48);
    void loadShader(const std::string& name, const ShaderProgram& program);

    void setGridEnabled(bool enabled);
    void setSkyboxEnabled(bool enabled, const std::string& directory = "");
    void setClearColor(float r, float g, float b, float a);
    void setViewportSize(uint32_t width, uint32_t height);
    void setTextLabel(const std::string& text, const std::string& fontName,
                      float x, float y, float scale = 1.0f,
                      const std::array<float, 4>& color = {1.0f, 1.0f, 1.0f, 1.0f});

    uint32_t width() const;
    uint32_t height() const;
    VkRenderPass getRenderPass() const;

private:
    class RendererImpl;
    std::unique_ptr<RendererImpl> impl_;
};

} // namespace engine3d
