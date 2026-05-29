#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "camera_component.h"
#include "camera_controller.h"
#include "entity.h"
#include "material_manager.h"
#include "platform.h"
#include "scene_manager.h"
#include "shader_manager.h"
#include "texture_manager.h"
#include "types.h"
#include "../render/backend/vulkan_context.h"
#include "../render/material_system.h"
#include "../render/render_pass.h"
#include "../object/mesh_manager.h"
#include "../font/text_overlay_renderer.h"

namespace engine3d {

class Renderer;
class FontManager;
class ShaderHotReload;

class Engine {
public:
    Engine();
    ~Engine();

    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    using TimeDelta = std::chrono::milliseconds;

    bool initialize(const std::string& appName, int width, int height, bool enableValidationLayers = true);
    void run();
    void cleanup();

    Entity* createEntity(const std::string& name);
    Entity* getEntity(const std::string& name);
    const std::vector<std::unique_ptr<Entity>>& getEntities() const { return entities; }
    bool removeEntity(Entity* entity);
    bool removeEntity(const std::string& name);

    void setActiveCamera(CameraComponent* cameraComponent);
    const CameraComponent* getActiveCamera() const;

    Renderer* getRenderer() { return renderer.get(); }
    Platform* getPlatform() { return platform.get(); }
    MaterialManager* getMaterialManager() { return &materialManager; }
    ShaderManager* getShaderManager() { return shaderManager.get(); }
    TextureManager* getTextureManager() { return textureManager.get(); }
    FontManager* getFontManager() { return fontManager.get(); }
    SceneManager* getSceneManager() { return &sceneManager; }
    MaterialSystem* getMaterialSystem() { return materialSystem.get(); }
    MeshManager* getMeshManager() { return meshManager.get(); }
    CameraController* getCameraController() { return &cameraController; }

    float getCurrentFPS() const { return currentFPS; }

    void loadFont(const std::string& name, const std::string& path, uint32_t pixelHeight = 48);
    void setTextLabel(const std::string& text, const std::string& fontName,
                      float x, float y, float scale = 1.0f,
                      const std::array<float, 4>& color = {1.0f, 1.0f, 1.0f, 1.0f});

    void enableShaderHotReload(bool enable);
    void reloadShader(const std::string& shaderName);

    void setShaderManifest(const ShaderManifest& manifest);
    void addShader(const std::string& name, const ShaderProgram& program);

    std::function<void(float, Engine&)> onUpdate;

private:
    std::unique_ptr<Platform> platform;
    std::shared_ptr<VulkanContext> vulkanContext;
    std::unique_ptr<MaterialSystem> materialSystem;
    std::unique_ptr<MeshManager> meshManager;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<ShaderManager> shaderManager;
    std::unique_ptr<TextureManager> textureManager;
    std::unique_ptr<FontManager> fontManager;
    MaterialManager materialManager;
    SceneManager sceneManager;
    RenderScene renderScene;

    mutable std::shared_mutex entitiesMutex;
    std::vector<std::unique_ptr<Entity>> entities;
    std::unordered_map<std::string, Entity*> entityMap;

    CameraComponent* activeCamera = nullptr;

    bool initialized = false;
    bool running = false;

    TimeDelta deltaTimeMs{0};
    uint64_t lastFrameTimeMs = 0;

    uint64_t frameCount = 0;
    float fpsUpdateTimer = 0.0f;
    float currentFPS = 0.0f;
    uint64_t lastFPSUpdateFrame = 0;

    void update(TimeDelta deltaTime);
    void renderFrame();
    TimeDelta calculateDeltaTimeMs();

    CameraController cameraController;

    std::shared_ptr<TextOverlayRenderer> textOverlayRenderer_;
    std::unique_ptr<ShaderHotReload> shaderHotReload_;
};

} // namespace engine3d
