#include "engine.h"
#include "renderer.h"
#include "font_manager.h"
#include "mesh_component.h"
#include "../render/backend/vulkan_context.h"
#include "../render/material_system.h"
#include "../object/mesh_manager.h"
#include "../render/shader_hot_reload.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <utility>

namespace engine3d {

Engine::Engine() = default;
Engine::~Engine() { cleanup(); }

bool Engine::initialize(const std::string& appName, int width, int height, bool enableValidationLayers) {
    try {
        platform = std::make_unique<Platform>(appName, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

        vulkanContext = std::make_shared<VulkanContext>();
        vulkanContext->initialize(appName);
        vulkanContext->createSurface(platform->nativeWindow());

        materialSystem = std::make_unique<MaterialSystem>(vulkanContext, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED);
        meshManager = std::make_unique<MeshManager>(vulkanContext);

        renderer = std::make_unique<Renderer>(platform.get(), &materialManager, vulkanContext);
        shaderManager = std::make_unique<ShaderManager>();
        textureManager = std::make_unique<TextureManager>();
        fontManager = std::make_unique<FontManager>();

        if (!renderer->initialize(appName, enableValidationLayers)) {
            return false;
        }

        materialSystem->setFormats(renderer->colorFormat(), renderer->depthFormat());

        textOverlayRenderer_ = std::make_shared<TextOverlayRenderer>();
        renderer->addFeature(textOverlayRenderer_);

        initialized = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Engine initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void Engine::run() {
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

void Engine::cleanup() {
    if (renderer) {
        renderer->cleanup();
        renderer.reset();
    }
    textOverlayRenderer_.reset();
    meshManager.reset();
    materialSystem.reset();
    vulkanContext.reset();
    platform.reset();
    initialized = false;
    running = false;
}

Entity* Engine::createEntity(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(entitiesMutex);
    auto entity = std::make_unique<Entity>(name);
    Entity* ptr = entity.get();
    entityMap[name] = ptr;
    entities.push_back(std::move(entity));
    return ptr;
}

Entity* Engine::getEntity(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(entitiesMutex);
    auto it = entityMap.find(name);
    return it != entityMap.end() ? it->second : nullptr;
}

bool Engine::removeEntity(Entity* entity) {
    if (!entity) return false;
    return removeEntity(entity->getName());
}

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

void Engine::setActiveCamera(CameraComponent* cameraComponent) {
    activeCamera = cameraComponent;
}

const CameraComponent* Engine::getActiveCamera() const {
    return activeCamera;
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

void Engine::update(TimeDelta deltaTime) {
    float dtSeconds = std::chrono::duration<float>(deltaTime).count();

    if (onUpdate) {
        onUpdate(dtSeconds, *this);
    }

    if (shaderHotReload_) shaderHotReload_->check();

    sceneManager.update(dtSeconds);

    {
        std::shared_lock<std::shared_mutex> lock(entitiesMutex);
        for (auto& entity : entities) {
            entity->update(deltaTime);
        }
    }
}

void Engine::renderFrame() {
    renderScene.clear();

    {
        std::shared_lock<std::shared_mutex> lock(entitiesMutex);
        std::vector<RenderPacket> packets;
        for (auto& entity : entities) {
            if (!entity->isActive()) continue;
            if (auto* renderable = entity->getComponent<MeshComponent>()) {
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
            renderer->prepareRenderPacket(packet);
            renderScene.addPacket(packet);
        }
    }

    RenderView renderView{};
    if (activeCamera) {
        renderView.view = activeCamera->getViewMatrix();
        renderView.projection = activeCamera->getProjectionMatrix();
        renderView.cameraPosition = activeCamera->position;
        Vec3 forward = normalize(subtract(activeCamera->target, activeCamera->position));
        renderView.cameraRight = normalize(cross(forward, activeCamera->up));
        renderView.cameraUp = normalize(cross(renderView.cameraRight, forward));
    }

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
                           const std::array<float, 4>& color) {
    if (!textOverlayRenderer_ || !textOverlayRenderer_->isReady()) return;
    float sw = renderer ? (float)renderer->width() : 1024.0f;
    float sh = renderer ? (float)renderer->height() : 768.0f;
    textOverlayRenderer_->addText(text, x, y, scale, color, sw, sh);
}

void Engine::reloadShader(const std::string&) {
    if (!materialSystem) return;
    std::shared_lock<std::shared_mutex> lock(entitiesMutex);
    for (auto& entity : entities) {
        auto* mc = entity->getComponent<MeshComponent>();
        if (mc && mc->materialInstance && !mc->materialName.empty()) {
            auto* mat = materialManager.get(mc->materialName);
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
PresentMode Engine::presentMode() const {
    return renderer ? renderer->presentMode() : PresentMode::Fifo;
}

void Engine::setFullscreen(bool fullscreen) {
    if (platform) platform->setFullscreen(fullscreen);
}
bool Engine::isFullscreen() const {
    return platform ? platform->isFullscreen() : false;
}

} // namespace engine3d
