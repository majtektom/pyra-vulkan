#include "core/engine.h"
#include "render/renderer.h"
#include "feature/font/font_manager.h"
#include "core/mesh_component.h"
#include "render/backend/vulkan_context.h"
#include "render/material_system.h"
#include "core/mesh_manager.h"
#include "render/shader_hot_reload.h"
#include "scene/camera_extraction_system.h"
#include "scene/light_system.h"
#include "scene/render_frame_prep_system.h"
#include "scene/render_extraction_system.h"
#include "scene/scene.h"
#include "core/entity.h"
#include "core/logger.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

namespace engine3d {

Engine::Engine() = default;
Engine::~Engine() { cleanup(); }

bool Engine::initialize(const std::string& appName, int width, int height, bool enableValidationLayers) {
    try {
        platform = std::make_unique<Platform>(appName, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        if (!platform) {
            LOG_ERROR("Failed to create platform");
            return false;
        }

        vulkanContext = std::make_shared<VulkanContext>();
        if (!vulkanContext->initialize(appName)) {
            LOG_ERROR("Failed to initialize Vulkan Context");
            return false;
        }
        vulkanContext->createSurface(platform->nativeWindow());

        materialSystem = std::make_unique<MaterialSystem>(vulkanContext, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED);
        assetRegistry = std::make_unique<AssetRegistry>();
        meshManager = std::make_unique<MeshManager>(vulkanContext);

        renderer = std::make_unique<Renderer>(platform.get(), &materialManager, vulkanContext, assetRegistry.get());
        shaderManager = std::make_unique<ShaderManager>();
        textureManager = std::make_unique<TextureManager>();
        fontManager = std::make_unique<FontManager>();

        materialManager.setAssetRegistry(assetRegistry.get());
        meshManager->setAssetRegistry(assetRegistry.get());
        textureManager->setAssetRegistry(assetRegistry.get());

        if (!renderer->initialize(appName, enableValidationLayers)) {
            LOG_ERROR("Failed to initialize renderer");
            return false;
        }

        materialSystem->setFormats(renderer->colorFormat(), renderer->depthFormat());

        textOverlayRenderer_ = std::make_shared<TextOverlayRenderer>();
        renderer->addFeature(textOverlayRenderer_);

        sceneManager.createScene("default");
        sceneManager.setActiveScene("default");
        if (auto* scene = sceneManager.activeScene()) {
            configureScene(*scene);
        }

        initialized = true;
        LOG_INFO("Engine initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_CRITICAL("Engine initialization failed: {}", e.what());
        return false;
    }
}

void Engine::run() {
    if (!initialized) {
        LOG_ERROR("Engine not initialized!");
        return;
    }

    platform->processEvents();
    currentInput_ = platform->pollInput();
    previousInput_ = currentInput_;
    platform->grabPointer(true);
    running = true;
    lastFrameTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Główna pętla gry (Main Game Loop)
    // Jest to serce każdego silnika. Jej zadaniem jest synchronizacja 
    // wejścia użytkownika, symulacji świata oraz renderowania.
    while (running && !platform->shouldClose()) {
        const auto frameStart = std::chrono::steady_clock::now();
        
        // 1. Obsługa zdarzeń systemu operacyjnego i wejścia (Input)
        platform->processEvents();
        previousInput_ = currentInput_;
        currentInput_ = platform->pollInput();
        if (platform->shouldClose()) break;

        // 2. Obliczenie Delta Time
        // Delta Time (dt) to czas, jaki upłynął od poprzedniej klatki.
        // Używamy go, aby ruch obiektów był niezależny od liczby FPS.
        // Bez dt, na szybszym komputerze postać poruszałaby się szybciej.
        TimeDelta dt = calculateDeltaTimeMs();
        
        // 3. Aktualizacja logiki (Update)
        update(dt);
        
        // 4. Renderowanie (Draw)
        renderFrame();

        // Opcjonalne ograniczenie liczby klatek na sekundę (Frame Limiter)
        if (frameRateLimit_ > 0) {
            const auto frameElapsed = std::chrono::steady_clock::now() - frameStart;
            const auto targetFrame = std::chrono::duration<double>(1.0 / static_cast<double>(frameRateLimit_));
            if (frameElapsed < targetFrame) {
                std::this_thread::sleep_for(targetFrame - frameElapsed);
            }
        }
    }

    renderer->waitIdle();
}

void Engine::cleanup() {
    if (renderer) {
        renderer->cleanup();
        renderer.reset();
    }

    textOverlayRenderer_.reset();
    meshManager.reset();
    assetRegistry.reset();
    materialSystem.reset();
    vulkanContext.reset();
    platform.reset();
    initialized = false;
    running = false;
}

Scene* Engine::activeScene() {
    return sceneManager.activeScene();
}

const Scene* Engine::activeScene() const {
    return sceneManager.activeScene();
}

void Engine::setShaderManifest(const ShaderManifest& manifest) {
    if (shaderManager) {
        for (const auto& entry : manifest.entries) {
            shaderManager->add(entry.name, entry.program);
        }
    }
}

void Engine::addShader(const std::string& name, const ShaderProgram& program) {
    if (shaderManager) {
        shaderManager->add(name, program);
    }
}

void Engine::configureScene(Scene& scene) {
    if (!scene.hasSystem("RenderExtractionSystem")) {
        scene.addSystem(std::make_unique<RenderExtractionSystem>(
            materialManager, *materialSystem, *meshManager, *assetRegistry));
    }
    if (!scene.hasSystem("LightSystem")) {
        scene.addSystem(std::make_unique<LightSystem>());
    }
    if (!scene.hasSystem("CameraExtractionSystem")) {
        scene.addSystem(std::make_unique<CameraExtractionSystem>());
    }
    if (!scene.hasSystem("RenderFramePrepSystem")) {
        scene.addSystem(std::make_unique<RenderFramePrepSystem>());
    }
}

void Engine::update(TimeDelta deltaTime) {
    float dtSeconds = std::chrono::duration<float>(deltaTime).count();

    // Callback użytkownika (np. skryptowanie w pętli głównej)
    if (onUpdate) {
        onUpdate(dtSeconds, *this);
    }

    if (shaderHotReload_) shaderHotReload_->check();

    if (auto* scene = activeScene()) {
        // Logika ECS (Entity-Component-System):
        // 1. Najpierw komponenty aktualizują swój stan (np. animacja, fizyka).
        scene->updateEntities(deltaTime);
        
        // 2. Następnie systemy przetwarzają całą scenę (np. przeliczanie hierarchii transformacji).
        // Rozdzielenie tych kroków pozwala na lepszą optymalizację i porządek w danych.
    }

    sceneManager.update(dtSeconds);
    layerManager.update(dtSeconds);
}

void Engine::renderFrame() {
    renderScene.clear();
    RenderView renderView{};
    bool hasRenderView = false;

    if (const auto* scene = activeScene()) {
        // Proces Ekstrakcji Danych (Render Extraction):
        // W nowoczesnych silnikach faza renderowania nie odczytuje danych bezpośrednio z komponentów gry
        // w trakcie rysowania. Zamiast tego "ekstrahuje" niezbędne informacje (paczki renderowania)
        // do oddzielnej struktury (renderScene).
        // Dlaczego to robimy? Pozwala to na renderowanie poprzedniej klatki podczas obliczania następnej (Multithreading).

        // Renderer otrzymuje komplet swiatel przygotowany przez ECS.
        renderer->setLights(scene->frameLights());
        hasRenderView = scene->hasFrameView();
        if (hasRenderView) {
            renderView = scene->frameView();
        }

        // Kopiujemy packet, bo ponizej uzupelniamy pola zalezne od kamery i renderera.
        for (auto packet : scene->renderPackets()) {
            renderer->prepareRenderPacket(packet);
            renderScene.addPacket(packet);
        }
    } else {
        // Gdy nie ma aktywnej sceny, przekazujemy pusta liste, aby uniknac
        // pozostawienia swiatel z poprzedniej klatki.
        renderer->setLights({});
        hasRenderView = false;
    }

    // Wywołanie niskopoziomowych komend rysujących (Vulkan/DirectX/OpenGL).
    renderer->render(renderScene, renderView);
}

Engine::TimeDelta Engine::calculateDeltaTimeMs() {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    deltaTimeMs = TimeDelta(static_cast<int64_t>(now - lastFrameTimeMs));
    lastFrameTimeMs = now;
    frameCount++;

    float dtSec = static_cast<float>(deltaTimeMs.count()) / 1000.0f;
    fpsUpdateTimer += dtSec;
    if (fpsUpdateTimer >= 0.5f) {
        currentFPS = static_cast<float>(frameCount - lastFPSUpdateFrame) / fpsUpdateTimer;
        lastFPSUpdateFrame = frameCount;
        fpsUpdateTimer = 0.0f;
    }

    return deltaTimeMs;
}

void Engine::loadFont(const std::string& name, const std::string& path, uint32_t pixelHeight) {
    if (!textOverlayRenderer_ || !textOverlayRenderer_->isPipelineReady()) return;
    FontAsset asset{name, path, pixelHeight};
    textOverlayRenderer_->loadFont(asset);
    if (fontManager) fontManager->add(asset);
}

void Engine::setTextLabel(const std::string& text, const std::string&, float x, float y, float scale,
                          const std::array<float, 4>& color, float depth) {
    if (!textOverlayRenderer_ || !textOverlayRenderer_->isReady()) return;
    float sw = renderer ? static_cast<float>(renderer->width()) : 1024.0f;
    float sh = renderer ? static_cast<float>(renderer->height()) : 768.0f;
    textOverlayRenderer_->addText(text, x, y, scale, color, sw, sh, depth);
}

void Engine::reloadShader(const std::string&) {
    if (!materialSystem) return;

    for (const auto& [name, scene] : sceneManager.scenes()) {
        if (!scene) continue;
        for (const auto& entity : scene->getEntities()) {
            auto* mc = entity->getComponent<MeshComponent>();
            if (!mc || !mc->materialInstance || !mc->materialHandle.isValid()) {
                continue;
            }

            const Material* mat = materialManager.get(mc->materialHandle);

            if (mat) {
                mc->materialInstance = materialSystem->getOrCreateMaterialInstance(*mat);
            }
        }
    }
}

void Engine::enableShaderHotReload(bool enable) {
    if (enable) {
        shaderHotReload_ = std::make_unique<ShaderHotReload>(
            "assets/shaders", "bin/assets/shaders");
        shaderHotReload_->setOnChanged([this](const std::string& shaderName) {
            if (materialSystem) materialSystem->invalidatePipelinesForShader(shaderName);
            reloadShader(shaderName);
        });
    } else {
        shaderHotReload_.reset();
    }
}

void Engine::setSynchronizationMode(SynchronizationMode mode) {
    if (renderer) renderer->setSynchronizationMode(mode);
}

SynchronizationMode Engine::synchronizationMode() const {
    return renderer ? renderer->synchronizationMode() : SynchronizationMode::Standard;
}

void Engine::setPresentMode(PresentMode mode) {
    if (renderer) renderer->setPresentMode(mode);
}

void Engine::setFrameRateLimit(int fps) {
    frameRateLimit_ = fps > 0 ? fps : 0;
}

PresentMode Engine::presentMode() const {
    return renderer ? renderer->presentMode() : PresentMode::Fifo;
}

void Engine::setFullscreen(bool fullscreen) {
    if (platform) platform->setFullscreen(fullscreen);
}

bool Engine::isFullscreen() const {
    return platform ? platform->isFullscreen() : false;
}

void Engine::setShadowsEnabled(bool enabled) {
    if (renderer) renderer->setShadowsEnabled(enabled);
}

bool Engine::shadowsEnabled() const {
    return renderer ? renderer->shadowsEnabled() : false;
}

void Engine::setWireframeEnabled(bool enabled) {
    if (renderer) {
        renderer->waitIdle();
        renderer->setWireframeEnabled(enabled);
    }
    if (materialSystem) {
        materialSystem->setWireframe(enabled);
    }
    reloadShader("");
}

bool Engine::isWireframeEnabled() const {
    return materialSystem ? materialSystem->isWireframe() : false;
}

void Engine::configureDirectionalShadow(float halfSize) {
    if (renderer) renderer->configureDirectionalShadow(halfSize);
}

void Engine::setLightingEnabled(bool enabled) {
    if (renderer) renderer->setLightingEnabled(enabled);
}

bool Engine::isLightingEnabled() const {
    return renderer ? renderer->isLightingEnabled() : false;
}

void Engine::setAmbientStrength(float strength) {
    if (renderer) renderer->setAmbientStrength(strength);
}

void Engine::setAmbientColor(float r, float g, float b) {
    if (renderer) renderer->setAmbientColor(r, g, b);
}

void Engine::bindActionKey(std::string_view action, int scancode) {
    inputActions_.bindKey(action, scancode);
}

bool Engine::actionDown(std::string_view action) const {
    return inputActions_.isDown(currentInput_, action);
}

bool Engine::actionPressed(std::string_view action) const {
    return inputActions_.wasPressed(previousInput_, currentInput_, action);
}

bool Engine::actionReleased(std::string_view action) const {
    return inputActions_.wasReleased(previousInput_, currentInput_, action);
}

} // namespace engine3d
