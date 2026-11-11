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
#include "feature/fog/volumetric_fog_renderer.h"
#include "core/types.h"

namespace engine3d {
class SkydomeRenderer;
class OceanRenderer;
struct Md2ModelData;
struct Md2AnimationState;
class GltfSkinnedModel;
class GltfSkinnedInstance;

class Renderer;
class FontManager;
class ShaderHotReload;
class AudioSystem;

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
    SceneManager* getSceneManager() { return &sceneManager; }
    MaterialSystem* getMaterialSystem() { return materialSystem.get(); }
    MeshManager* getMeshManager() { return meshManager.get(); }
    // Pula wątków silnika - do równoległych obliczeń gry (parallelFor) zamiast tworzenia wątków co klatkę.
    JobSystem& getJobSystem() { return jobSystem_; }
    CameraController* getCameraController() { return &cameraController; }
    // System dźwięku (zawsze dostępny; gdy brak urządzenia audio - wywołania są no-op).
    AudioSystem* getAudio() { return audio_.get(); }

    float getCurrentFPS() const { return currentFPS; }
    const InputSnapshot& input() const { return currentInput_; }
    bool actionDown(std::string_view action) const;
    bool actionPressed(std::string_view action) const;

    // Stan przycisków myszy: 0 = lewy, 1 = prawy, 2 = środkowy.
    bool mouseDown(int button) const;
    bool mousePressed(int button) const;   // wciśnięty w tej klatce
    bool mouseReleased(int button) const;  // puszczony w tej klatce
    InputActionMap& inputActions() { return inputActions_; }
    const InputActionMap& inputActions() const { return inputActions_; }

    void loadFont(const std::string& name, const std::string& path, uint32_t pixelHeight = 48);
    void setTextLabel(const std::string& text, const std::string& fontName,
                      float x, float y, float scale = 1.0f,
                      const std::array<float, 4>& color = {1.0f, 1.0f, 1.0f, 1.0f},
                      float depth = 0.0f);

    // Szerokość napisu w pikselach przy danej skali (do wyrównywania tekstu HUD).
    float measureText(const std::string& text, float scale = 1.0f) const;

    void enableShaderHotReload(bool enable);
    void reloadShader(const std::string& shaderName);

    void configureScene(Scene& scene);

    void setSynchronizationMode(SynchronizationMode mode);
    SynchronizationMode synchronizationMode() const;
    void setFrameRateLimit(int fps);
    void setPresentMode(PresentMode mode);
    PresentMode presentMode() const;

    void setFullscreen(bool fullscreen);
    bool isFullscreen() const;
    // Rozdzielczość okna (tryb okienkowy) i użyteczny rozmiar ekranu.
    void setWindowSize(uint32_t width, uint32_t height);
    bool getDisplaySize(uint32_t& width, uint32_t& height) const;

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

    // Zasięg cieni słońca; startDistance - od jakiej odległości od kamery zaczynają się kaskady;
    // splitLambda - rozkład kaskad (0 równomierny .. 1 logarytmiczny).
    void configureDirectionalShadow(float halfSize, float startDistance = 0.0f, float splitLambda = 0.6f);

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

    void setWindTime(float time);
    float getWindTime() const;
    void setWindStrength(float strength);
    float getWindStrength() const;

    bool loadMap(const std::string& path, Scene* scene);
    std::shared_ptr<SkydomeRenderer> getSkydome() const { return currentSkydome_; }
    std::shared_ptr<OceanRenderer> getOcean() const { return currentOcean_; }
    std::shared_ptr<Md2ModelData> getMd2Model() const { return currentMd2Model_; }
    std::shared_ptr<Md2AnimationState> getMd2Anim() const { return currentMd2Anim_; }

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
    std::shared_ptr<VolumetricFogRenderer> volumetricFogRenderer_;
    std::unique_ptr<ShaderHotReload> shaderHotReload_;
    std::unique_ptr<AudioSystem> audio_;

    std::shared_ptr<SkydomeRenderer> currentSkydome_;
    std::shared_ptr<OceanRenderer> currentOcean_;
    std::shared_ptr<Md2ModelData> currentMd2Model_;
    std::shared_ptr<Md2AnimationState> currentMd2Anim_;
    std::unique_ptr<GltfSkinnedModel> currentBrainModel_;
    std::vector<std::unique_ptr<GltfSkinnedInstance>> currentBrainInstances_;

    struct Md2Instance {
        std::string meshName;
        std::shared_ptr<Md2ModelData> model;
        std::shared_ptr<Md2AnimationState> anim;
    };
    std::vector<Md2Instance> md2Instances_;
    std::vector<MeshData> md2Results_;   // wyniki animacji MD2 z bieżącej klatki (pamięć zostaje między klatkami)

    float tessellationLevel_ = 4.0f;
};

} // namespace engine3d
