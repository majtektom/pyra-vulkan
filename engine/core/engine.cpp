#include "core/engine.h"
#include "core/crash_handler.h"
#include "feature/audio/audio_system.h"
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
#include "object/model_md2.h"
#include "object/gltf/model_gltf.h"
#include "object/skydome_renderer.h"
#include "object/ocean_renderer.h"
#include "object/terrain/terrain.h"
#include "object/terrain/terrain_component.h"

#include "core/profiler.h"
#include <cstdlib>

#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

namespace engine3d {

Engine::Engine() = default;
Engine::~Engine() { cleanup(); }

bool Engine::initialize(const std::string& appName, int width, int height, bool enableValidationLayers) {
    // Awaria (błąd pamięci, abort, nieobsłużony wyjątek) - opis ze stosem wywołań do crash/<program>_<data>.log.
    installCrashHandler("crash");
    // Warstwy walidacji Vulkana sprawdzają KAŻDE polecenie na CPU - przy tysiącu rysowań na klatkę zjadają
    // większość czasu klatki. To narzędzie programisty: włączone w buildzie Debug albo z ENGINE_VALIDATION=1
    // (np. po zmianach w rendererze), w zwykłej grze (Release) - wyłączone. ENGINE_VALIDATION=0 wyłącza zawsze.
    {
#ifdef NDEBUG
        bool wanted = false;
#else
        bool wanted = true;
#endif
        if (const char* v = std::getenv("ENGINE_VALIDATION")) wanted = std::string(v) != "0";
        enableValidationLayers = enableValidationLayers && wanted;
        LOG_INFO("Walidacja Vulkan: {}", enableValidationLayers ? "WŁĄCZONA (wolniej)" : "wyłączona (ENGINE_VALIDATION=1 włącza)");
    }
    try {
        platform = std::make_unique<Platform>(appName, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        if (!platform) {
            LOG_ERROR("nie udało się utworzyć okna (platformy)");
            return false;
        }

        // Dźwięk jest opcjonalny - brak urządzenia audio nie blokuje startu silnika.
        audio_ = std::make_unique<AudioSystem>();
        audio_->init();

        vulkanContext = std::make_shared<VulkanContext>();
        if (!vulkanContext->initialize(appName, enableValidationLayers)) {
            LOG_ERROR("nie udało się zainicjalizować Vulkana");
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
            LOG_ERROR("nie udało się zainicjalizować renderera");
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
        LOG_INFO("Silnik gotowy");
        return true;
    } catch (const std::exception& e) {
        LOG_CRITICAL("Engine initialization failed: {}", e.what());
        return false;
    }
}

void Engine::run() {
    if (!initialized) {
        LOG_ERROR("silnik nie jest zainicjalizowany");
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
        Profiler::get().beginFrame();   // ENGINE_PROFILE=1: czasy sekcji klatki w logu
        
        // 1. Obsługa zdarzeń systemu operacyjnego i wejścia (Input)
        platform->processEvents();
        previousInput_ = currentInput_;
        currentInput_ = platform->pollInput();
        if (platform->shouldClose()) break;
        Profiler::get().lap("wejście");

        // 2. Obliczenie Delta Time
        // Delta Time (dt) to czas, jaki upłynął od poprzedniej klatki.
        // Używamy go, aby ruch obiektów był niezależny od liczby FPS.
        // Bez dt, na szybszym komputerze postać poruszałaby się szybciej.
        TimeDelta dt = calculateDeltaTimeMs();
        
        // 3. Aktualizacja logiki (Update)
        update(dt);
        Profiler::get().lap("update");

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
        Profiler::get().lap("limiter FPS");
        Profiler::get().endFrame();
    }

    renderer->waitIdle();
}

void Engine::cleanup() {
    // Audio przed platformą (SDL_Quit) - wątek miksera musi się zatrzymać wcześniej.
    if (audio_) audio_->shutdown();

    // Zwolnienie zasobów mapy przed zniszczeniem renderera i kontekstu Vulkan
    clearMapFeatures();

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

void Engine::configureScene(Scene& scene) {
    if (!scene.hasSystem("RenderExtractionSystem")) {
        scene.addSystem(std::make_unique<RenderExtractionSystem>(
            materialManager, *materialSystem, *meshManager, *assetRegistry, jobSystem_, *renderer));
    }
    if (!scene.hasSystem("LightSystem")) {
        scene.addSystem(std::make_unique<LightSystem>());
    }
    if (!scene.hasSystem("CameraExtractionSystem")) {
        scene.addSystem(std::make_unique<CameraExtractionSystem>());
    }
    if (!scene.hasSystem("RenderFramePrepSystem")) {
        scene.addSystem(std::make_unique<RenderFramePrepSystem>(jobSystem_));
    }
}

void Engine::update(TimeDelta deltaTime) {
    float dtSeconds = std::chrono::duration<float>(deltaTime).count();

    // Odliczanie wieku buforów zastąpionych w poprzednich klatkach (bezpieczne ponowne użycie).
    if (meshManager) meshManager->beginFrame();

    // Proporcje kamery zgodne z oknem (przed onUpdate, aby picking myszą liczył się poprawnie).
    if (renderer && renderer->height() > 0) {
        const float windowAspect = static_cast<float>(renderer->width()) / static_cast<float>(renderer->height());
        for (const auto& [sceneName, scenePtr] : sceneManager.scenes()) {
            if (!scenePtr) continue;
            if (auto* cam = scenePtr->activeCamera(); cam && cam->autoAspect) {
                cam->aspectRatio = windowAspect;
            }
        }
    }

    // ENGINE_CRASH_TEST=segv|abort|throw: celowa awaria po 60 klatkach - sprawdzenie raportu w crash/.
    if (frameCount == 60) {
        if (const char* crash = std::getenv("ENGINE_CRASH_TEST")) {
            LOG_WARN("ENGINE_CRASH_TEST={} - celowa awaria (test raportu awarii)", crash);
            if (std::string(crash) == "abort") std::abort();
            if (std::string(crash) == "throw") throw std::runtime_error("test nieobsłużonego wyjątku");
            volatile int* nothing = nullptr;
            *nothing = 42;   // segv
        }
    }

    // Callback użytkownika (np. skryptowanie w pętli głównej)
    if (onUpdate) {
        PROFILE_SCOPE("update.gra (onUpdate)");
        onUpdate(dtSeconds, *this);
    }

    if (shaderHotReload_) shaderHotReload_->check();

    // Aktualizacja animacji szkieletowych GLTF + morphingowych MD2 równolegle (jeden parallelFor po obu listach).
    // GLTF i MD2 działają na niezależnych danych (różne bufory GPU) - brak wyścigów. Wyniki MD2 trafiają do
    // md2Results_ (pole silnika - pamięć wierzchołków zostaje między klatkami), a do GPU wysyła je wątek główny.
    {
        const size_t gltfCount = currentBrainInstances_.size();
        const size_t total = gltfCount + md2Instances_.size();
        md2Results_.resize(md2Instances_.size());
        jobSystem_.parallelFor(total, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (i < gltfCount) {
                    // Każda instancja GLTF posiada własną pamięć GPU (boneMemory_) - thread-safe.
                    if (auto& inst = currentBrainInstances_[i]) inst->update(dtSeconds, 0, 1.0f);
                    continue;
                }
                auto& inst = md2Instances_[i - gltfCount];
                MeshData& out = md2Results_[i - gltfCount];
                out.vertices.clear();
                if (inst.model && inst.anim) animateMd2(*inst.model, *inst.anim, dtSeconds, out);
            }
        }, 1);
        // updateMesh tylko z jednego wątku (Vulkan).
        for (size_t i = 0; i < md2Instances_.size(); ++i) {
            if (!md2Results_[i].vertices.empty()) meshManager->updateMesh(md2Instances_[i].meshName, md2Results_[i]);
        }
    }

    if (auto* scene = activeScene()) {
        // Logika ECS (Entity-Component-System):
        // 1. Najpierw komponenty aktualizują swój stan (np. animacja, fizyka).
        scene->updateEntities(deltaTime);
        
        // 2. Następnie systemy przetwarzają całą scenę (np. przeliczanie hierarchii transformacji).
        // Rozdzielenie tych kroków pozwala na lepszą optymalizację i porządek w danych.
    }

    {
        PROFILE_SCOPE("update.scena (ekstrakcja paczek)");
        sceneManager.update(dtSeconds);
    }
}

void Engine::renderFrame() {
    renderScene.clear();
    renderScene.setCollectShadows(renderer && renderer->shadowsEnabled());   // cienie wyłączone - bez listy cieni
    RenderView renderView{};
    bool hasRenderView = false;

    if (auto* scene = activeScene()) {
        // Proces Ekstrakcji Danych (Render Extraction):
        // W nowoczesnych silnikach faza renderowania nie odczytuje danych bezpośrednio z komponentów gry
        // w trakcie rysowania. Zamiast tego "ekstrahuje" niezbędne informacje (paczki renderowania)
        // do oddzielnej struktury (renderScene).
        // Dlaczego to robimy? Pozwala to na renderowanie poprzedniej klatki podczas obliczania następnej (Multithreading).

        // Renderer otrzymuje komplet swiatel przygotowany przez ECS.
        renderer->setLights(scene->frameLights());

        // Mgła wolumetryczna rozprasza światło słońca — kierunek z pierwszego światła kierunkowego.
        if (volumetricFogRenderer_) {
            for (const auto& light : scene->frameLights()) {
                if (light.type == SceneLightType::Directional) {
                    volumetricFogRenderer_->setSunDirection(light.direction);
                    break;
                }
            }
        }
        hasRenderView = scene->hasFrameView();
        if (hasRenderView) {
            renderView = scene->frameView();
        }

        // Paczki zostały przygotowane równolegle w RenderExtractionSystem.
        // Przenosimy je bez kopiowania do renderScene.
        for (auto& packet : scene->mutableRenderPackets()) {
            renderScene.addPacket(std::move(packet));
        }
    } else {
        // Gdy nie ma aktywnej sceny, przekazujemy pusta liste, aby uniknac
        // pozostawienia swiatel z poprzedniej klatki.
        renderer->setLights({});
        hasRenderView = false;
    }

    // Nowe siatki statyczne: kopie z buforów pośrednich do pamięci karty - przed renderem, na tej samej kolejce.
    if (meshManager) meshManager->flushUploads();
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

float Engine::measureText(const std::string& text, float scale) const {
    if (!textOverlayRenderer_ || !textOverlayRenderer_->isReady()) return 0.0f;
    return textOverlayRenderer_->measureText(text, scale);
}

void Engine::reloadShader(const std::string&) {
    if (!materialSystem) return;

    for (const auto& [name, scene] : sceneManager.scenes()) {
        if (!scene) continue;
        for (const auto& entity : scene->getEntities()) {
            auto* mc = entity->getComponent<MeshComponent>();
            if (mc && mc->materialInstance && mc->materialHandle.isValid()) {
                const Material* mat = materialManager.get(mc->materialHandle);
                if (mat) {
                    mc->materialInstance = materialSystem->getOrCreateMaterialInstance(*mat);
                }
            }

            auto* tc = entity->getComponent<TerrainComponent>();
            if (tc && tc->materialHandle.isValid()) {
                tc->materialInstance.reset();
                const Material* mat = materialManager.get(tc->materialHandle);
                if (mat) {
                    tc->materialInstance = materialSystem->getOrCreateMaterialInstance(*mat);
                }
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

void Engine::setWindowSize(uint32_t width, uint32_t height) {
    if (platform) platform->setWindowSize(width, height);
}

bool Engine::getDisplaySize(uint32_t& width, uint32_t& height) const {
    return platform && platform->usableDisplaySize(width, height);
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

void Engine::setTessellationEnabled(bool enabled) {
    if (renderer) {
        renderer->waitIdle();
        renderer->setTessellationEnabled(enabled);
    }
    if (materialSystem) {
        materialSystem->setTessellationEnabled(enabled);
    }
    reloadShader("");
}

bool Engine::isTessellationEnabled() const {
    return renderer ? renderer->isTessellationEnabled() : true;
}

bool Engine::isTessellationSupported() const {
    return renderer ? renderer->isTessellationSupported() : false;
}

void Engine::setTessellationLevel(float level) {
    tessellationLevel_ = level;
    if (renderer) {
        renderer->setTessellationLevel(level);
    }
}

float Engine::getTessellationLevel() const {
    return tessellationLevel_;
}

void Engine::setTextureFilter(TextureFilter filter) {
    if (renderer) {
        renderer->waitIdle();
        renderer->setTextureFilter(filter);
    }
}

TextureFilter Engine::getTextureFilter() const {
    return renderer ? renderer->getTextureFilter() : TextureFilter::Anisotropic16x;
}

void Engine::configureDirectionalShadow(float halfSize, float startDistance, float splitLambda) {
    if (renderer) renderer->configureDirectionalShadow(halfSize, startDistance, splitLambda);
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

void Engine::setFogEnabled(bool enabled) {
    if (renderer) renderer->setFogEnabled(enabled);
}

bool Engine::isFogEnabled() const {
    return renderer ? renderer->isFogEnabled() : false;
}

void Engine::setFogColor(float r, float g, float b) {
    if (renderer) renderer->setFogColor(r, g, b);
}

void Engine::setFogStart(float start) {
    if (renderer) renderer->setFogStart(start);
}

void Engine::setFogEnd(float end) {
    if (renderer) renderer->setFogEnd(end);
}

void Engine::setFogDensity(float density) {
    if (renderer) renderer->setFogDensity(density);
}

void Engine::setWindTime(float time) { if (renderer) renderer->setWindTime(time); }
float Engine::getWindTime() const { return renderer ? renderer->getWindTime() : 0.0f; }
void Engine::setWindStrength(float strength) { if (renderer) renderer->setWindStrength(strength); }
float Engine::getWindStrength() const { return renderer ? renderer->getWindStrength() : 0.3f; }

bool Engine::actionDown(std::string_view action) const {
    return inputActions_.isDown(currentInput_, action);
}

bool Engine::actionPressed(std::string_view action) const {
    return inputActions_.wasPressed(previousInput_, currentInput_, action);
}

bool Engine::mouseDown(int button) const {
    if (button < 0 || button >= 5) return false;
    return currentInput_.mouseButtons[button];
}

bool Engine::keyPressed(int scancode) const {
    if (scancode < 0 || scancode >= 256) return false;
    return currentInput_.keys[scancode] && !previousInput_.keys[scancode];
}

bool Engine::mousePressed(int button) const {
    if (button < 0 || button >= 5) return false;
    return currentInput_.mouseButtons[button] && !previousInput_.mouseButtons[button];
}

bool Engine::mouseReleased(int button) const {
    if (button < 0 || button >= 5) return false;
    return !currentInput_.mouseButtons[button] && previousInput_.mouseButtons[button];
}

} // namespace engine3d
