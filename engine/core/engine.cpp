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
#include "object/skydome_renderer.h"
#include "object/ocean_renderer.h"
#include "object/model_3ds.h"
#include "object/model_md2.h"
#include "object/gltf/model_gltf.h"
#include "object/terrain/terrain.h"
#include "object/terrain/terrain_component.h"
#include "object/terrain/terrain_placement_component.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>

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
        if (!vulkanContext->initialize(appName, enableValidationLayers)) {
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
    // Zwolnienie zasobów mapy przed zniszczeniem renderera i kontekstu Vulkan
    if (renderer) {
        renderer->waitIdle();
        renderer->removeFeature("SkydomeRenderer");
        renderer->removeFeature("OceanRenderer");
        renderer->removeFeature("VolumetricFogRenderer");
    }

    currentBrainInstances_.clear();
    currentBrainModel_.reset();
    currentMd2Anim_.reset();
    currentMd2Model_.reset();
    currentOcean_.reset();
    currentSkydome_.reset();
    volumetricFogRenderer_.reset();

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

    // Callback użytkownika (np. skryptowanie w pętli głównej)
    if (onUpdate) {
        onUpdate(dtSeconds, *this);
    }

    if (shaderHotReload_) shaderHotReload_->check();

    // Aktualizacja animacji szkieletowych GLTF + morphingowych MD2 równolegle.
    // GLTF i MD2 działają na niezależnych danych (różne bufory GPU) - brak wyścigów.
    {
        // Uruchamiamy GLTF asynchronicznie na wątkach roboczych.
        std::vector<std::future<void>> gltfFutures;
        gltfFutures.reserve(currentBrainInstances_.size());
        for (auto& inst : currentBrainInstances_) {
            if (inst) {
                // Każda instancja GLTF posiada własną pamięć GPU (boneMemory_) - thread-safe.
                gltfFutures.push_back(jobSystem_.submit([&inst, dtSeconds]() {
                    inst->update(dtSeconds, 0, 1.0f);
                }));
            }
        }

        // MD2 również równolegle - każda instancja ma własny MeshData i meshName.
        // Zbieramy pary (meshName, MeshData) tylko dla aktywnych instancji z zachowaniem kolejności.
        struct Md2Job {
            std::string meshName;
            MeshData result;
            std::future<void> fut;
        };
        std::vector<Md2Job> md2Jobs;
        md2Jobs.reserve(md2Instances_.size());

        for (auto& inst : md2Instances_) {
            if (!inst.model || !inst.anim) continue;
            md2Jobs.emplace_back();
            auto& job = md2Jobs.back();
            job.meshName = inst.meshName;
            // Animacja obliczana na wątku roboczym - każda instancja ma osobny stan i bufor wyjściowy.
            job.fut = jobSystem_.submit([&inst, &out = job.result, dtSeconds]() {
                animateMd2(*inst.model, *inst.anim, dtSeconds, out);
            });
        }

        // Czekamy na ukończenie GLTF.
        for (auto& f : gltfFutures) f.get();

        // Czekamy na MD2 i przesyłamy wyniki do GPU (updateMesh musi być single-threaded - Vulkan).
        for (auto& job : md2Jobs) {
            job.fut.get();
            if (!job.result.vertices.empty()) {
                meshManager->updateMesh(job.meshName, job.result);
            }
        }
    }

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


bool Engine::loadMap(const std::string& path, Scene* scene) {
    if (!scene) {
        LOG_ERROR("Scene is null in loadMap");
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open map file: {}", path);
        return false;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse map file JSON: {}", e.what());
        return false;
    }

    // Czyszczenie starej mapy z silnika
    if (renderer) {
        renderer->waitIdle();
        renderer->removeFeature("SkydomeRenderer");
        renderer->removeFeature("OceanRenderer");
        renderer->removeFeature("VolumetricFogRenderer");
    }

    currentBrainInstances_.clear();
    currentBrainModel_.reset();
    currentMd2Anim_.reset();
    currentMd2Model_.reset();
    md2Instances_.clear();
    currentOcean_.reset();
    currentSkydome_.reset();
    volumetricFogRenderer_.reset();



    // 1. Oświetlenie otoczenia (Ambient)
    if (root.contains("ambient")) {
        auto anode = root["ambient"];
        setAmbientStrength(anode.value("strength", 0.28f));
        if (anode.contains("color")) {
            auto c = anode["color"];
            setAmbientColor(c.value("r", 0.35f), c.value("g", 0.38f), c.value("b", 0.45f));
        }
    }

    // 2. Mgła (Fog)
    if (root.contains("fog")) {
        auto fnode = root["fog"];
        setFogEnabled(fnode.value("enabled", true));
        if (fnode.contains("color")) {
            auto c = fnode["color"];
            setFogColor(c.value("r", 0.575f), c.value("g", 0.68f), c.value("b", 0.88f));
        }
        setFogStart(fnode.value("start", 150.0f));
        setFogEnd(fnode.value("end", 3500.0f));
        setFogDensity(fnode.value("density", 0.83f));
    }

    // 3. Atmosfera i Skydome i Ocean
    if (root.contains("skydome")) {
        auto snode = root["skydome"];
        if (snode.value("enabled", false)) {
            currentSkydome_ = std::make_shared<SkydomeRenderer>();
            SkydomeConfig skydomeCfg;
            skydomeCfg.enabled = true;
            skydomeCfg.domeScale = snode.value("domeScale", 1.6f);
            skydomeCfg.timeOfDay = snode.value("timeOfDay", 12.0f);
            skydomeCfg.cloudCoverage = snode.value("cloudCoverage", 0.35f);
            skydomeCfg.cloudSpeed0 = snode.value("cloudSpeed0", 0.25f);
            skydomeCfg.cloudSpeed1 = snode.value("cloudSpeed1", 0.12f);
            skydomeCfg.cloudScale0 = snode.value("cloudScale0", 1200.0f);
            skydomeCfg.cloudScale1 = snode.value("cloudScale1", 820.0f);
            skydomeCfg.sunElevation = snode.value("sunElevation", 50.0f);
            skydomeCfg.enableSunLight = snode.value("enableSunLight", true);
            currentSkydome_->configure(skydomeCfg, "bin/assets/shaders/skydome", "skydome");
            currentSkydome_->setEnabled(true);
            renderer->addFeature(currentSkydome_);
        }
    }

    if (root.contains("ocean")) {
        auto onode = root["ocean"];
        if (onode.value("enabled", false)) {
            currentOcean_ = std::make_shared<OceanRenderer>();
            OceanConfig oceanCfg;
            oceanCfg.enabled = true;
            oceanCfg.waterLevel = onode.value("waterLevel", 20.0f);
            oceanCfg.waveAmplitude = onode.value("waveAmplitude", 2.3f);
            oceanCfg.waveSpeed = onode.value("waveSpeed", 0.9f);
            oceanCfg.waveFrequency = onode.value("waveFrequency", 9.0f);
            oceanCfg.foamThreshold = onode.value("foamThreshold", 15.9f);
            currentOcean_->configure(oceanCfg, "bin/assets/shaders/ocean", "ocean");
            currentOcean_->setEnabled(true);
            renderer->addFeature(currentOcean_);
        }
    }

    // 4b. Volumetric Fog (only if enabled in map)
    if (root.contains("fog") && root["fog"].value("enabled", true)) {
        const auto& fogNode = root["fog"];
        volumetricFogRenderer_ = std::make_shared<VolumetricFogRenderer>();
        volumetricFogRenderer_->configure("bin/assets/shaders/fog");
        volumetricFogRenderer_->setFogDensity(fogNode.value("volumetricDensity", 0.002f));
        volumetricFogRenderer_->setHeightFalloff(fogNode.value("heightFalloff", 0.004f));
        volumetricFogRenderer_->setSeaLevel(fogNode.value("seaLevel", 0.0f));
        volumetricFogRenderer_->setFogRadius(fogNode.value("radius", 1500.0f));
        if (fogNode.contains("color")) {
            const auto& c = fogNode["color"];
            volumetricFogRenderer_->setFogColor({c.value("r", 0.575f), c.value("g", 0.68f), c.value("b", 0.88f)});
        }

        std::vector<LocalFogVolume> localVolumes;
        if (fogNode.contains("volumes") && fogNode["volumes"].is_array()) {
            for (const auto& volumeNode : fogNode["volumes"]) {
                LocalFogVolume volume;
                if (volumeNode.contains("center")) {
                    const auto& center = volumeNode["center"];
                    volume.center = {
                        center.value("x", 0.0f),
                        center.value("y", 0.0f),
                        center.value("z", 0.0f)};
                }
                if (volumeNode.contains("halfExtents")) {
                    const auto& extents = volumeNode["halfExtents"];
                    volume.halfExtents = {
                        extents.value("x", 100.0f),
                        extents.value("y", 20.0f),
                        extents.value("z", 100.0f)};
                }
                volume.density = volumeNode.value("density", 0.004f);
                volume.edgeFade = volumeNode.value("edgeFade", 0.25f);
                localVolumes.push_back(volume);
            }
        }
        volumetricFogRenderer_->setLocalVolumes(std::move(localVolumes));
        volumetricFogRenderer_->setEnabled(true);
        renderer->addFeature(volumetricFogRenderer_);
    }

    // 5. Kamera (Camera)
    if (root.contains("camera")) {
        auto cnode = root["camera"];
        Entity* camEnt = scene->createEntity("game_camera");
        auto* camComp = camEnt->addComponent<CameraComponent>();
        if (cnode.contains("position")) {
            auto pos = cnode["position"];
            camEnt->transform().position = {pos.value("x", -110.0f), pos.value("y", 90.0f), pos.value("z", -110.0f)};
        }
        if (cnode.contains("target")) {
            auto tgt = cnode["target"];
            camComp->target = {tgt.value("x", 0.0f), tgt.value("y", 28.0f), tgt.value("z", 0.0f)};
        }
        camComp->fieldOfViewDegrees = cnode.value("fov", 55.0f);
        camComp->nearPlane = cnode.value("near", 0.5f);
        camComp->farPlane = cnode.value("far", 12000.0f);
        scene->setActiveCamera(camComp);
    }

    // 6. Modele 3DS
    if (root.contains("static_models_3ds")) {
        for (const auto& item : root["static_models_3ds"]) {
            std::string mpath = item.value("model_path", "");
            Model3ds model;
            if (!model.load(mpath)) {
                LOG_WARN("Could not load 3DS: {}", mpath);
                continue;
            }

            std::string fallbackTexture = item.value("texture", "");
            bool disableAlpha = item.value("disableAlpha", false);
            std::vector<MaterialHandle> sourceMaterials;
            sourceMaterials.reserve(model.materialList.size());
            for (size_t i = 0; i < model.materialList.size(); ++i) {
                const auto& src = *model.materialList[i];
                Material material = Convert3dsMaterialToEngineMaterial(src);
                material.name = "map_3ds_mat_" + std::to_string(i) + "_" + mpath;
                if (disableAlpha) {
                    material.blendMode = BlendMode::Disabled;
                    material.depthWrite = true;
                    material.cullMode = CullMode::Back;
                }
                std::string texPath = src.texturePath;
                if (texPath.empty()) {
                    texPath = fallbackTexture;
                }
                if (!texPath.empty()) {
                    TextureHandle texture = renderer->loadTexture(material.name + "_tex", texPath, false, disableAlpha);
                    material.textureHandle = texture;
                }
                sourceMaterials.push_back(materialManager.add(material));
            }

            if (sourceMaterials.empty() && !fallbackTexture.empty()) {
                Material material;
                material.name = "map_3ds_mat_default_" + mpath;
                TextureHandle texture = renderer->loadTexture(material.name + "_tex", fallbackTexture, false, disableAlpha);
                material.textureHandle = texture;
                material.cullMode = CullMode::Back;
                material.blendMode = BlendMode::Disabled;
                material.depthWrite = true;
                sourceMaterials.push_back(materialManager.add(material));
            }

            struct SubMeshRef {
                std::string meshName;
                MaterialHandle material;
            };
            std::vector<SubMeshRef> subMeshes;
            subMeshes.reserve(model.objectList.size());
            for (size_t i = 0; i < model.objectList.size(); ++i) {
                const auto& object = *model.objectList[i];
                std::string meshName = "map_3ds_mesh_" + std::to_string(i) + "_" + mpath;
                meshManager->loadMesh(meshName, object.mesh);

                MaterialHandle matHandle{};
                if (!sourceMaterials.empty()) {
                    int matIdx = object.materialIndex;
                    if (matIdx < 0 || matIdx >= static_cast<int>(sourceMaterials.size())) {
                        matIdx = 0;
                    }
                    matHandle = sourceMaterials[matIdx];
                }
                subMeshes.push_back({meshName, matHandle});
            }

            Vec3 scale{1.0f, 1.0f, 1.0f};
            if (item.contains("scale")) {
                auto sc = item["scale"];
                scale = {sc.value("x", 1.0f), sc.value("y", 1.0f), sc.value("z", 1.0f)};
            }
            bool snap = item.value("snap_to_terrain", false);
            float offset = item.value("terrain_offset", 0.0f);

            std::vector<Vec3> positions;
            std::shared_ptr<Terrain> terrain = nullptr;
            for (const auto& ent : scene->getEntities()) {
                if (auto* tc = ent->getComponent<TerrainComponent>()) {
                    terrain = tc->terrain;
                    break;
                }
            }
            if (item.contains("random_instances") && terrain) {
                auto randNode = item["random_instances"];
                int count = randNode.value("count", 0);
                float minHeight = randNode.value("min_height", -9999.0f);
                float maxBound = terrain->worldSizeXZ() / 2.0f;
                float minBound = -maxBound;

                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_real_distribution<float> dis(minBound, maxBound);

                int attempts = 0;
                while (positions.size() < static_cast<size_t>(count) && attempts < 100000) {
                    float rx = dis(gen);
                    float rz = dis(gen);
                    float h = terrain->getHeight(rx, rz);
                    if (h >= minHeight) {
                        positions.push_back({rx, 0.0f, rz});
                    }
                    attempts++;
                }
            } else if (item.contains("instances")) {
                for (const auto& instNode : item["instances"]) {
                    if (instNode.contains("position")) {
                        auto pos = instNode["position"];
                        positions.push_back({pos.value("x", 0.0f), pos.value("y", 0.0f), pos.value("z", 0.0f)});
                    } else {
                        positions.push_back({0.0f, 0.0f, 0.0f});
                    }
                }
            }

            int instIdx = 0;
            for (const auto& pos : positions) {
                Entity* rootEnt = scene->createEntity("map_3ds_root_" + std::to_string(instIdx) + "_" + mpath);
                rootEnt->transform().position = pos;
                rootEnt->transform().scale = scale;
                if (snap) {
                    rootEnt->addComponent<TerrainPlacementComponent>(offset);
                }

                for (size_t i = 0; i < subMeshes.size(); ++i) {
                    Entity* part = scene->createEntity("map_3ds_part_" + std::to_string(instIdx) + "_" + std::to_string(i) + "_" + mpath);
                    scene->setParent(part, rootEnt);
                    auto* meshComp = part->addComponent<MeshComponent>(subMeshes[i].meshName);
                    meshComp->materialHandle = subMeshes[i].material;
                }
                instIdx++;
            }
        }
    }

    // 7. Modele MD2
    if (root.contains("characters_md2")) {
        for (const auto& item : root["characters_md2"]) {
            std::string name = item.value("name", "goblin");
            std::string mpath = item.value("model_path", "");
            std::string tpath = item.value("texture_path", "");
            std::string npath = item.value("normalmap_path", "");
            currentMd2Model_ = std::make_shared<Md2ModelData>(loadMd2Model(mpath));
            if (!currentMd2Model_ || currentMd2Model_->frames.empty()) {
                LOG_WARN("Could not load MD2: {}", mpath);
                currentMd2Model_.reset();
                continue;
            }

            currentMd2Anim_ = std::make_shared<Md2AnimationState>();
            currentMd2Anim_->startFrame = 0;
            currentMd2Anim_->endFrame = static_cast<int>(currentMd2Model_->frames.size()) - 1;
            currentMd2Anim_->speed = item.value("anim_speed", 10.0f);

            meshManager->loadMesh(name + "_mesh", currentMd2Model_->triangles);
            
            Material material;
            material.name = name + "_mat";
            if (!tpath.empty()) {
                TextureHandle texture = renderer->loadTexture(name + "_tex", tpath);
                material.textureHandle = texture;
            }
            if (!npath.empty()) {
                TextureHandle texture = renderer->loadTexture(name + "_norm", npath);
                material.normalMapHandle = texture;
                material.bumpIntensity = 1.0f;
            }
            material.cullMode = CullMode::Back;
            material.blendMode = BlendMode::Disabled;
            material.depthWrite = true;
            MaterialHandle matHandle = materialManager.add(material);

            Entity* ent = scene->createEntity(name + "_entity");
            auto* meshComp = ent->addComponent<MeshComponent>(name + "_mesh");
            meshComp->materialHandle = matHandle;

            if (item.contains("position")) {
                auto pos = item["position"];
                ent->transform().position = {pos.value("x", 0.0f), pos.value("y", 0.0f), pos.value("z", 0.0f)};
            }
            if (item.contains("rotation")) {
                auto rot = item["rotation"];
                ent->transform().rotation = {rot.value("x", 0.0f), rot.value("y", 0.0f), rot.value("z", 0.0f)};
            }
            if (item.contains("scale")) {
                auto sc = item["scale"];
                ent->transform().scale = {sc.value("x", 1.0f), sc.value("y", 1.0f), sc.value("z", 1.0f)};
            }

            if (item.value("snap_to_terrain", false)) {
                ent->addComponent<TerrainPlacementComponent>(item.value("terrain_offset", 0.0f));
            }

            md2Instances_.push_back({name + "_mesh", currentMd2Model_, currentMd2Anim_});
        }
    }

    // 8. Modele GLTF
    if (root.contains("gltf_models")) {
        for (const auto& item : root["gltf_models"]) {
            std::string mpath = item.value("model_path", "");
            GltfModel model = loadGltfModel(mpath);
            if (model.primitives.empty()) {
                LOG_WARN("Could not load GLTF: {}", mpath);
                continue;
            }

            struct SubMeshRef {
                std::string meshName;
                MaterialHandle material;
            };
            std::vector<SubMeshRef> subMeshes;
            subMeshes.reserve(model.primitives.size());

            int primIdx = 0;
            for (const auto& primitive : model.primitives) {
                std::string meshName = "map_gltf_mesh_" + std::to_string(primIdx) + "_" + mpath;
                std::string materialName = "map_gltf_mat_" + std::to_string(primIdx) + "_" + mpath;
                meshManager->loadMesh(meshName, primitive.mesh);

                Material material;
                material.name = materialName;
                material.cullMode = CullMode::Disabled;
                if (!primitive.texturePath.empty()) {
                    TextureHandle texture = renderer->loadTexture(materialName + "_tex", primitive.texturePath);
                    material.textureHandle = texture;
                }
                if (!primitive.normalMapPath.empty()) {
                    TextureHandle texture = renderer->loadTexture(materialName + "_n", primitive.normalMapPath);
                    material.normalMapHandle = texture;
                    material.bumpIntensity = 1.0f;
                }
                if (!primitive.ormPath.empty()) {
                    TextureHandle texture = renderer->loadTexture(materialName + "_orm", primitive.ormPath);
                    material.metallicRoughnessTextureHandle = texture;
                }
                if (!primitive.occlusionPath.empty()) {
                    TextureHandle texture = renderer->loadTexture(materialName + "_occ", primitive.occlusionPath);
                    material.occlusionTextureHandle = texture;
                }
                if (!primitive.emissivePath.empty()) {
                    TextureHandle texture = renderer->loadTexture(materialName + "_em", primitive.emissivePath);
                    material.emissiveTextureHandle = texture;
                }
                material.roughnessFactor = 0.5f;
                MaterialHandle handle = materialManager.add(material);

                subMeshes.push_back({meshName, handle});
                primIdx++;
            }

            Vec3 scale{1.0f, 1.0f, 1.0f};
            if (item.contains("scale")) {
                auto sc = item["scale"];
                scale = {sc.value("x", 1.0f), sc.value("y", 1.0f), sc.value("z", 1.0f)};
            }
            bool snap = item.value("snap_to_terrain", false);
            float offset = item.value("terrain_offset", 0.0f);

            int instIdx = 0;
            for (const auto& instNode : item["instances"]) {
                Entity* rootEnt = scene->createEntity("map_gltf_root_" + std::to_string(instIdx) + "_" + mpath);
                if (instNode.contains("position")) {
                    auto pos = instNode["position"];
                    rootEnt->transform().position = {pos.value("x", 0.0f), pos.value("y", 0.0f), pos.value("z", 0.0f)};
                }
                if (instNode.contains("rotation")) {
                    auto rot = instNode["rotation"];
                    rootEnt->transform().rotation = {rot.value("x", 0.0f), rot.value("y", 0.0f), rot.value("z", 0.0f)};
                }
                rootEnt->transform().scale = scale;
                if (snap) {
                    rootEnt->addComponent<TerrainPlacementComponent>(offset);
                }

                for (size_t i = 0; i < subMeshes.size(); ++i) {
                    Entity* part = scene->createEntity("map_gltf_part_" + std::to_string(instIdx) + "_" + std::to_string(i) + "_" + mpath);
                    scene->setParent(part, rootEnt);
                    auto* meshComp = part->addComponent<MeshComponent>(subMeshes[i].meshName);
                    meshComp->materialHandle = subMeshes[i].material;
                }
                instIdx++;
            }
        }
    }

    // 9. Animowane szkieletowo GLTF
    if (root.contains("gltf_skinned_models")) {
        currentBrainModel_ = std::make_unique<GltfSkinnedModel>();
        for (const auto& item : root["gltf_skinned_models"]) {
            std::string mpath = item.value("model_path", "");
            
            if (!currentBrainModel_->load(renderer.get(), meshManager.get(), &materialManager, assetRegistry.get(), mpath)) {
                LOG_WARN("Could not load GLTF skinned: {}", mpath);
                continue;
            }

            Vec3 scale{1.0f, 1.0f, 1.0f};
            if (item.contains("scale")) {
                auto sc = item["scale"];
                scale = {sc.value("x", 1.0f), sc.value("y", 1.0f), sc.value("z", 1.0f)};
            }
            bool snap = item.value("snap_to_terrain", false);
            float offset = item.value("terrain_offset", 0.0f);

            int instIdx = 0;
            for (const auto& instNode : item["instances"]) {
                auto inst = std::make_unique<GltfSkinnedInstance>();
                if (inst->initialize(renderer.get(), *currentBrainModel_)) {
                    Entity* rootEnt = inst->instantiate(scene, "map_skinned_" + std::to_string(instIdx) + "_" + mpath);
                    if (instNode.contains("position")) {
                        auto pos = instNode["position"];
                        rootEnt->transform().position = {pos.value("x", 0.0f), pos.value("y", 0.0f), pos.value("z", 0.0f)};
                    }
                    if (instNode.contains("rotation")) {
                        auto rot = instNode["rotation"];
                        rootEnt->transform().rotation = {rot.value("x", 0.0f), rot.value("y", 0.0f), rot.value("z", 0.0f)};
                    }
                    rootEnt->transform().scale = scale;
                    if (snap) {
                        rootEnt->addComponent<TerrainPlacementComponent>(offset);
                    }
                    inst->setAnimationTime(static_cast<float>(instIdx) * 0.7f);
                    currentBrainInstances_.push_back(std::move(inst));
                }
                instIdx++;
            }
        }
    }

    LOG_INFO("Successfully loaded map from {}", path);
    return true;
}

} // namespace engine3d
