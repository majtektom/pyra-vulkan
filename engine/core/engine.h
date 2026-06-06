#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "camera/camera_component.h"
#include "camera/camera_controller.h"
#include "render/material_manager.h"
#include "platform/platform.h"
#include "scene/layer_manager.h"
#include "scene/scene_manager.h"
#include "render/shader_manager.h"
#include "render/texture_manager.h"
#include "render/backend/vulkan_context.h"
#include "render/material_system.h"
#include "render/render_pass.h"
#include "core/mesh_manager.h"
#include "core/asset_registry.h"
#include "core/input_actions.h"
#include "core/job_system.h"
#include "feature/font/text_overlay_renderer.h"
#include "core/types.h"

namespace engine3d {
class Terrain;
class SkydomeRenderer;
class OceanRenderer;
struct Md2ModelData;
struct Md2AnimationState;
class GltfSkinnedModel;
class GltfSkinnedInstance;

class Renderer;
class FontManager;
class ShaderHotReload;

/**
 * @brief Główna klasa silnika (Orkiestrator).
 * 
 * Klasa Engine spina wszystkie podsystemy silnika (renderowanie, zasoby, sceny, wejście).
 * Jej głównym zadaniem jest zarządzanie cyklem życia aplikacji oraz sterowanie 
 * główną pętlą (Game Loop).
 * 
 * Wzorzec Orkiestratora:
 * Engine nie wykonuje całej pracy sam, ale deleguje ją do wyspecjalizowanych menedżerów:
 * - Renderer: odpowiada za komunikację z GPU.
 * - SceneManager: zarządza obiektami w świecie gry.
 * - AssetRegistry: kontroluje ładowanie tekstur i modeli.
 */
class Engine {
public:
    Engine();
    ~Engine();

    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    using TimeDelta = std::chrono::milliseconds;

    /**
     * @brief Inicjalizuje okno, kontekst graficzny i menedżery zasobów.
     */
    bool initialize(const std::string& appName, int width, int height, bool enableValidationLayers = true);

    /**
     * @brief Uruchamia główną pętlę silnika.
     * Pętla działa do momentu zamknięcia okna lub wywołania sygnału wyjścia.
     */
    void run();

    /**
     * @brief Zwalnia zasoby GPU i niszczy podsystemy w odpowiedniej kolejności.
     */
    void cleanup();

    Renderer* getRenderer() { return renderer.get(); }
    Platform* getPlatform() { return platform.get(); }
    MaterialManager* getMaterialManager() { return &materialManager; }
    AssetRegistry* getAssetRegistry() { return assetRegistry.get(); }
    ShaderManager* getShaderManager() { return shaderManager.get(); }
    TextureManager* getTextureManager() { return textureManager.get(); }
    FontManager* getFontManager() { return fontManager.get(); }
    SceneManager* getSceneManager() { return &sceneManager; }
    LayerManager* getLayerManager() { return &layerManager; }
    MaterialSystem* getMaterialSystem() { return materialSystem.get(); }
    MeshManager* getMeshManager() { return meshManager.get(); }
    CameraController* getCameraController() { return &cameraController; }

    float getCurrentFPS() const { return currentFPS; }
    const InputSnapshot& input() const { return currentInput_; }
    void bindActionKey(std::string_view action, int scancode);
    bool actionDown(std::string_view action) const;
    bool actionPressed(std::string_view action) const;
    bool actionReleased(std::string_view action) const;
    InputActionMap& inputActions() { return inputActions_; }
    const InputActionMap& inputActions() const { return inputActions_; }

    void loadFont(const std::string& name, const std::string& path, uint32_t pixelHeight = 48);
    void setTextLabel(const std::string& text, const std::string& fontName,
                      float x, float y, float scale = 1.0f,
                      const std::array<float, 4>& color = {1.0f, 1.0f, 1.0f, 1.0f},
                      float depth = 0.0f);

    void enableShaderHotReload(bool enable);
    void reloadShader(const std::string& shaderName);

    void setShaderManifest(const ShaderManifest& manifest);
    void addShader(const std::string& name, const ShaderProgram& program);
    void configureScene(Scene& scene);

    void setSynchronizationMode(SynchronizationMode mode);
    SynchronizationMode synchronizationMode() const;
    void setFrameRateLimit(int fps);
    int frameRateLimit() const { return frameRateLimit_; }
    void setPresentMode(PresentMode mode);
    PresentMode presentMode() const;

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const;

    void setShadowsEnabled(bool enabled);
    bool shadowsEnabled() const;

    void setWireframeEnabled(bool enabled);
    bool isWireframeEnabled() const;

    void configureDirectionalShadow(float halfSize);

    void setLightingEnabled(bool enabled);
    bool isLightingEnabled() const;

    void setAmbientStrength(float strength);
    void setAmbientColor(float r, float g, float b);

    void setFogEnabled(bool enabled);
    bool isFogEnabled() const;
    void setFogColor(float r, float g, float b);
    void setFogStart(float start);
    void setFogEnd(float end);
    void setFogDensity(float density);

    bool loadMap(const std::string& path, Scene* scene);
    void registerTerrainRoot(Entity* entity, float yOffset);
    std::shared_ptr<Terrain> getTerrain() const { return currentTerrain_; }
    std::shared_ptr<SkydomeRenderer> getSkydome() const { return currentSkydome_; }
    std::shared_ptr<OceanRenderer> getOcean() const { return currentOcean_; }
    std::shared_ptr<Md2ModelData> getMd2Model() const { return currentMd2Model_; }
    std::shared_ptr<Md2AnimationState> getMd2Anim() const { return currentMd2Anim_; }
    GltfSkinnedModel* getBrainModel() const { return currentBrainModel_.get(); }
    const std::vector<std::unique_ptr<GltfSkinnedInstance>>& getBrainInstances() const { return currentBrainInstances_; }

    struct TerrainRootRef {
        Entity* entity;
        float yOffset;
        mutable float lastX = -999999.0f;
        mutable float lastZ = -999999.0f;
    };

    std::function<void(float, Engine&)> onUpdate;

private:
    std::unique_ptr<Platform> platform;
    std::shared_ptr<VulkanContext> vulkanContext;
    std::unique_ptr<MaterialSystem> materialSystem;
    std::unique_ptr<AssetRegistry> assetRegistry;
    std::unique_ptr<MeshManager> meshManager;
    std::unique_ptr<Renderer> renderer;
    // Pula wątków roboczych (Job System) - jeden na cały silnik, żyje do końca aplikacji.
    // Inicjalizowana z liczbą rdzeni logicznych - 1 (main thread + worker threads).
    JobSystem jobSystem_;
    std::unique_ptr<ShaderManager> shaderManager;
    std::unique_ptr<TextureManager> textureManager;
    std::unique_ptr<FontManager> fontManager;
    MaterialManager materialManager;
    SceneManager sceneManager;
    LayerManager layerManager;
    RenderScene renderScene;

    bool initialized = false;
    bool running = false;

    TimeDelta deltaTimeMs{0};
    uint64_t lastFrameTimeMs = 0;

    uint64_t frameCount = 0;
    float fpsUpdateTimer = 0.0f;
    float currentFPS = 0.0f;
    uint64_t lastFPSUpdateFrame = 0;

    Scene* activeScene();
    const Scene* activeScene() const;

    void update(TimeDelta deltaTime);
    void renderFrame();
    TimeDelta calculateDeltaTimeMs();

    CameraController cameraController;
    InputSnapshot currentInput_{};
    InputSnapshot previousInput_{};
    InputActionMap inputActions_;
    int frameRateLimit_ = 0;

    std::shared_ptr<TextOverlayRenderer> textOverlayRenderer_;
    std::unique_ptr<ShaderHotReload> shaderHotReload_;

    std::shared_ptr<Terrain> currentTerrain_;
    std::shared_ptr<SkydomeRenderer> currentSkydome_;
    std::shared_ptr<OceanRenderer> currentOcean_;
    std::shared_ptr<Md2ModelData> currentMd2Model_;
    std::shared_ptr<Md2AnimationState> currentMd2Anim_;
    std::unique_ptr<GltfSkinnedModel> currentBrainModel_;
    std::vector<std::unique_ptr<GltfSkinnedInstance>> currentBrainInstances_;
    std::vector<TerrainRootRef> terrainRoots_;

    struct Md2Instance {
        std::string meshName;
        std::shared_ptr<Md2ModelData> model;
        std::shared_ptr<Md2AnimationState> anim;
    };
    std::vector<Md2Instance> md2Instances_;
};

} // namespace engine3d
