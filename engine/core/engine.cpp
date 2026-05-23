#include "engine.h"
#include "renderer.h"
#include "font_manager.h"
#include "mesh_component.h"
#include "../render/backend/vulkan_context.h"
#include "../render/material_system.h"
#include "../object/mesh_manager.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <utility>

namespace engine3d {

// Konstruktor Engine — inicjalizuje wszystkie pola domyślnymi wartościami.
// Właściwe tworzenie zasobów (Vulkan, okno) odbywa się w initialize().
Engine::Engine() = default;
// Destruktor Engine — wywołuje cleanup(), który zwalnia zasoby w odwrotnej kolejności tworzenia
// (renderer → meshManager → materialSystem → vulkanContext → platform).
Engine::~Engine() { cleanup(); }

bool Engine::initialize(const std::string& appName, int width, int height, bool enableValidationLayers) {
    // ===================================================================
    // INICJALIZACJA SILNIKA — tworzy wszystkie podsystemy w odpowiedniej kolejności:
    //
    // 1. Platform        – okno X11 (musi być pierwsze, tworzy Display/Window)
    // 2. VulkanContext   – instancja Vulkan + GPU + powierzchnia
    // 3. MaterialSystem  – zarządzanie pipeline'ami (wstępnie z VK_NULL_HANDLE,
    //                       prawdziwy renderPass ustawiany po inicie rendera)
    // 4. MeshManager     – bufory wierzchołków/indeksów na GPU
    // 5. Renderer        – system renderowania (swapchain, tekstury, shadery)
    // 6. ShaderManager, TextureManager, FontManager – menedżery zasobów
    //
    // Po stworzeniu rendera, MaterialSystem dostaje prawdziwy VkRenderPass
    // ze swapchaina (bo pipeline'y muszą znać formaty attachmentów)
    // ===================================================================
    try {
        platform = std::make_unique<Platform>(appName, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        
        vulkanContext = std::make_shared<VulkanContext>();
        vulkanContext->initialize(appName);
        vulkanContext->createSurface(platform->nativeWindow());

        materialSystem = std::make_unique<MaterialSystem>(vulkanContext, VK_NULL_HANDLE);
        meshManager = std::make_unique<MeshManager>(vulkanContext);
        
        renderer = std::make_unique<Renderer>(platform.get(), &materialManager, vulkanContext);
        shaderManager = std::make_unique<ShaderManager>();
        textureManager = std::make_unique<TextureManager>();
        fontManager = std::make_unique<FontManager>();
        
        if (!renderer->initialize(appName, enableValidationLayers)) {
            return false;
        }

        materialSystem->setRenderPass(renderer->renderPass());

        // Init sub-renderers
        textOverlayRenderer_.init(renderer->device(), renderer->physicalDevice(), renderer->renderPass(),
                                  renderer->commandPool(), renderer->graphicsQueue(), renderer->extent());

        initialized = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Engine initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void Engine::run() {
    // ===================================================================
    // GŁÓWNA PĘTLA GRY — wykonuje się co klatkę, dopóki okno jest otwarte
    //
    // Przepływ:
    //   1. processEvents()    – odbierz zdarzenia X11 (klawisze, mysz)
    //   2. calculateDelta()   – oblicz czas między klatkami (dt)
    //   3. update(dt)         – zaktualizuj logikę (kamera, callback, encje)
    //   4. renderFrame()      – zbierz pakiety renderowania i wyślij do GPU
    //
    // Synchronizacja CPU-GPU przez double buffering i VkFence
    // ===================================================================
    if (!initialized) {
        std::cerr << "Engine not initialized!" << std::endl;
        return;
    }
    platform->processEvents();
    platform->grabPointer(true);
    running = true;
    lastFrameTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    while (running && !platform->shouldClose()) {
        platform->processEvents();
        if (platform->shouldClose()) break;

        TimeDelta dt = calculateDeltaTimeMs();
        update(dt);

        {
            std::shared_lock<std::shared_mutex> lock(entitiesMutex);
            renderFrame();
        }
    }
    renderer->waitIdle();
}

// cleanup — zwalnia wszystkie zasoby silnika w ściśle określonej kolejności.
// Renderer musi być zniszczony przed VulkanContext (bo używa jego urządzenia i kolejek),
// VulkanContext przed Platform (bo powierzchnia Vulkan jest powiązana z oknem X11).
void Engine::cleanup() {
    if (renderer) {
        renderer->cleanup();
        renderer.reset();
    }
    meshManager.reset();
    materialSystem.reset();
    vulkanContext.reset();
    platform.reset();
    initialized = false;
    running = false;
}

// createEntity — tworzy nowy byt, dodaje go do wektora entities i mapy entityMap
// pod unikalną nazwą. unique_lock zapewnia wyłączny dostęp podczas modyfikacji kolekcji.
// @param name — unikalna nazwa bytu (nie sprawdzamy duplikatów — nadpisze istniejący wpis w mapie)
// @return surowy wskaźnik do bytu (własność pozostaje w wektorze entities)
Entity* Engine::createEntity(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(entitiesMutex);
    auto entity = std::make_unique<Entity>(name);
    Entity* ptr = entity.get();
    entityMap[name] = ptr;
    entities.push_back(std::move(entity));
    return ptr;
}

// getEntity — wyszukuje byt w mapie entityMap po nazwie.
// Używa shared_lock — wiele wątków może jednocześnie czytać mapę (bezpieczeństwo współbieżne).
// @param name — klucz wyszukiwania
// @return wskaźnik do bytu lub nullptr
Entity* Engine::getEntity(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(entitiesMutex);
    auto it = entityMap.find(name);
    return it != entityMap.end() ? it->second : nullptr;
}

// removeEntity(Entity*) — usuwa byt po wskaźniku, delegując do removeEntity(name).
// @param entity — wskaźnik do bytu; nullptr → natychmiastowe false
// @return true jeśli byt został znaleziony i usunięty
bool Engine::removeEntity(Entity* entity) {
    if (!entity) return false;
    return removeEntity(entity->getName());
}

// removeEntity(const string&) — usuwa byt po nazwie z wektora i mapy.
// Jeśli usuwany byt posiada aktywną kamerę (activeCamera), resetuje ją do nullptr.
// Używa unique_lock — wyłączny dostęp podczas modyfikacji kolekcji.
// @param name — nazwa bytu do usunięcia
// @return true jeśli byt istniał i został usunięty
bool Engine::removeEntity(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(entitiesMutex);
    auto it = entityMap.find(name);
    if (it == entityMap.end()) return false;

    for (auto eit = entities.begin(); eit != entities.end(); ++eit) {
        if (eit->get() == it->second) {
            if (activeCamera && activeCamera->getOwner() == it->second) {
                activeCamera = nullptr;
            }
            entities.erase(eit);
            entityMap.erase(it);
            return true;
        }
    }
    return false;
}

// setActiveCamera — ustawia aktywną kamerę. Przekazanie nullptr wyłącza kamerę.
// @param cameraComponent — komponent kamery (lub nullptr)
void Engine::setActiveCamera(CameraComponent* cameraComponent) {
    activeCamera = cameraComponent;
}

// getActiveCamera — zwraca aktywną kamerę (const — tylko do odczytu).
// @return wskaźnik do CameraComponent lub nullptr
const CameraComponent* Engine::getActiveCamera() const {
    return activeCamera;
}

// setShaderManifest — iteruje przez wszystkie wpisy manifestu i dodaje każdy shader do ShaderManagera.
// @param manifest — lista par (nazwa, ShaderProgram) definiujących dostępne shadery
void Engine::setShaderManifest(const ShaderManifest& manifest) {
    if (shaderManager) {
        for (const auto& entry : manifest.entries) {
            shaderManager->add(entry.name, entry.program);
        }
    }
}

// addShader — dodaje pojedynczy shader do ShaderManagera pod podaną nazwą.
// @param name    — klucz, pod którym shader będzie dostępny (np. "default.vert")
// @param program — dane shadera: kod SPIR-V dla vertex/fragment shadera
void Engine::addShader(const std::string& name, const ShaderProgram& program) {
    if (shaderManager) {
        shaderManager->add(name, program);
    }
}

// update — główna funkcja aktualizacji logiki gry, wywoływana co klatkę.
// Kolejność: 1) sterowanie kamerą FPP, 2) callback użytkownika onUpdate,
// 3) aktualizacja menedżera scen, 4) update wszystkich aktywnych bytów.
// @param deltaTime — czas od poprzedniej klatki w milisekundach
void Engine::update(TimeDelta deltaTime) {

    if (onUpdate) {
        onUpdate(std::chrono::duration<float>(deltaTime).count(), *this);
    }

    sceneManager.update(std::chrono::duration<float>(deltaTime).count());

    {
        std::shared_lock<std::shared_mutex> lock(entitiesMutex);
        for (auto& entity : entities) {
            entity->update(deltaTime);
        }
    }
}

void Engine::renderFrame() {
    // ===================================================================
    // RENDEROWANIE KLATKI — zbiera dane z komponentów i wysyła do GPU
    //
    // Krok 1: Wyczyść scenę z poprzedniej klatki
    // Krok 2: Zbierz RenderPacket z każdego MeshComponent:
    //         - Rozwiąż materiał (jeśli jeszcze nie ma instancji)
    //         - Pobierz bufory GPU z MeshManager
    //         - Pobierz macierz modelu z TransformComponent
    // Krok 3: Oblicz odległość od kamery (potrzebne do sortowania
    //         przezroczystych obiektów od tyłu)
    // Krok 4: Dodaj pakiety do RenderScene (dzieli na opaque/transparent)
    // Krok 5: Wyślij do Renderer::render()
    // ===================================================================
    renderScene.clear();
    
    {
        std::shared_lock<std::shared_mutex> lock(entitiesMutex);
        std::vector<RenderPacket> packets;
        for (auto& entity : entities) {
            if (!entity->isActive()) continue;
            if (auto* renderable = entity->getComponent<MeshComponent>()) {
                // If materialInstance is not set but materialName is, try to resolve it
                if (!renderable->materialInstance && !renderable->materialName.empty()) {
                    auto* mat = materialManager.get(renderable->materialName);
                    if (mat) {
                        renderable->materialInstance = materialSystem->getOrCreateMaterialInstance(*mat);
                    }
                }
                renderable->collectRenderPackets(packets, *meshManager);
            }
        }
        
        for (auto& packet : packets) {
            if (activeCamera) {
                Vec3 objPos = { packet.transform.values[12], packet.transform.values[13], packet.transform.values[14] };
                Vec3 diff = subtract(objPos, activeCamera->position);
                packet.distanceToCamera = std::sqrt(dot(diff, diff));
            } else {
                packet.distanceToCamera = 0.0f;
            }
            renderScene.addPacket(packet);
        }
    }

    renderer->render(renderScene, activeCamera,
        [this](VkCommandBuffer cmd, uint32_t frame) {
            if (skyboxEnabled_ && skyboxRenderer_.isReady()) {
                std::array<VkDescriptorSet, 3> fs = {};
                fs[0] = renderer->getTextureDescriptorSet("skybox_fb", frame);
                fs[1] = renderer->getTextureDescriptorSet("skybox_lr", frame);
                fs[2] = renderer->getTextureDescriptorSet("skybox_top", frame);
                skyboxRenderer_.recordDrawCommands(cmd, skyboxRenderer_.getPipelineLayout(),
                                                    activeCamera ? activeCamera->position : Vec3{}, fs);
            }
            if (gridEnabled_ && gridRenderer_.isReady()) {
                gridRenderer_.recordDrawCommands(cmd, gridRenderer_.getPipelineLayout(),
                                                  renderer->getTextureDescriptorSet("", frame), frame);
            }
        },
        [this](VkCommandBuffer cmd, uint32_t) {
            if (textOverlayReady_ && textOverlayRenderer_.isReady()) {
                textOverlayRenderer_.buildTexts();
                textOverlayRenderer_.recordDrawCommands(cmd);
                textOverlayRenderer_.clearTexts();
            }
        });
}

Engine::TimeDelta Engine::calculateDeltaTimeMs() {
    // ===================================================================
    // OBLICZANIE DELTA TIME I FPS
    //
    // deltaTimeMs = aktualny czas - czas poprzedniej klatki
    // FPS jest uśredniany co 0.5 sekundy:
    //   currentFPS = (klatki od ostatniego pomiaru) / (czas od ostatniego pomiaru)
    //
    // Używamy std::chrono::steady_clock — monotoniczny, nie podatny na
    // zmiany czasu systemowego
    // ===================================================================
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

void Engine::setSkyboxEnabled(bool enabled, const std::string& directory) {
    skyboxEnabled_ = enabled;
    skyboxDirectory_ = directory;
    skyboxRenderer_.setEnabled(enabled);
    if (enabled && renderer) {
        SkyboxConfig cfg; cfg.enabled = true; cfg.directory = directory; cfg.size = 3500; cfg.verticalOffset = 100;
        skyboxRenderer_.init(renderer->device(), renderer->physicalDevice(), renderer->renderPass(),
                             renderer->descriptorSetLayout(), renderer->commandPool(), renderer->graphicsQueue(),
                             renderer->extent(), cfg, "bin/assets/shaders/skybox");
    }
}

void Engine::setSkyboxExposure(float e) { skyboxRenderer_.setExposure(e); }

void Engine::setGridEnabled(bool enabled) {
    gridEnabled_ = enabled;
    gridRenderer_.setEnabled(enabled);
    if (enabled && renderer) {
        GridConfig cfg; cfg.enabled = true; cfg.size = 200; cfg.spacing = 5;
        gridRenderer_.init(renderer->device(), renderer->physicalDevice(), renderer->renderPass(),
                           renderer->descriptorSetLayout(), renderer->commandPool(), renderer->graphicsQueue(),
                           renderer->extent(), cfg, "bin/assets/shaders/grid");
    }
}

void Engine::loadFont(const std::string& name, const std::string& path, uint32_t pixelHeight) {
    if (!textOverlayRenderer_.isPipelineReady()) return;
    FontAsset asset{name, path, pixelHeight};
    textOverlayRenderer_.loadFont(asset);
    textOverlayReady_ = true;
    if (fontManager) fontManager->add(asset);
}

void Engine::setTextLabel(const std::string& text, const std::string&, float x, float y, float scale,
                           const std::array<float, 4>& color) {
    if (!textOverlayRenderer_.isReady()) return;
    float sw = renderer ? (float)renderer->width() : 1024.0f;
    float sh = renderer ? (float)renderer->height() : 768.0f;
    textOverlayRenderer_.addText(text, x, y, scale, color, sw, sh);
}

} // namespace engine3d
