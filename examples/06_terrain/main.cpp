#include "core/logger.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "core/engine.h"
#include "core/entity.h"
#include "render/renderer.h"
#include "core/types.h"
#include "core/mesh_component.h"
#include "camera/camera_component.h"
#include "object/terrain/terrain.h"
#include "object/terrain/terrain_component.h"
#include "object/terrain/terrain_system.h"
#include "object/terrain/terrain_placement_system.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"

using namespace engine3d;

namespace {

// Definicje akcji sterowania terenem
constexpr const char* kActionMoveForward = "move.forward";
constexpr const char* kActionMoveBackward = "move.backward";
constexpr const char* kActionMoveLeft = "move.left";
constexpr const char* kActionMoveRight = "move.right";
constexpr const char* kActionMoveUp = "move.up";
constexpr const char* kActionMoveDown = "move.down";
constexpr const char* kActionSnapToGround = "terrain.snap_to_ground";
constexpr const char* kActionTogglePointer = "ui.toggle_pointer";
constexpr const char* kActionTerrainModePlasma = "terrain.mode_plasma";
constexpr const char* kActionTerrainModeFault = "terrain.mode_fault";
constexpr const char* kActionTerrainModeDeposition = "terrain.mode_deposition";



/**
 * @brief Mapuje fizyczne klawisze na akcje systemu terenu.
 */
void configureTerrainBindings(Engine& engine) {
    auto& map = engine.inputActions();
    map.clearAll();
    map.bindKey(kActionMoveForward, SDL_SCANCODE_W);
    map.bindKey(kActionMoveBackward, SDL_SCANCODE_S);
    map.bindKey(kActionMoveLeft, SDL_SCANCODE_A);
    map.bindKey(kActionMoveRight, SDL_SCANCODE_D);
    map.bindKey(kActionMoveUp, SDL_SCANCODE_SPACE);
    map.bindKey(kActionMoveDown, SDL_SCANCODE_LCTRL);
    map.bindKey(kActionSnapToGround, SDL_SCANCODE_G);
    map.bindKey(kActionTogglePointer, SDL_SCANCODE_F1);
    map.bindKey(kActionTerrainModePlasma, SDL_SCANCODE_1);
    map.bindKey(kActionTerrainModeFault, SDL_SCANCODE_2);
    map.bindKey(kActionTerrainModeDeposition, SDL_SCANCODE_3);
}

} // namespace

int main() {
    Engine engine;
    if (!engine.initialize("engine3d: terrain LOD", 1280, 800)) return 1;

    configureTerrainBindings(engine);
    engine.setWireframeEnabled(false);
    engine.setTessellationEnabled(true);
    engine.setTessellationLevel(2.0f);

    auto* scene = engine.getSceneManager()->activeScene();
    if (!scene) return 1;

    scene->addSystem(std::make_unique<TerrainSystem>(*engine.getMeshManager(), *engine.getMaterialManager(), *engine.getMaterialSystem()));
    scene->addSystem(std::make_unique<TerrainPlacementSystem>());

    auto* renderer = engine.getRenderer();
    renderer->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    engine.loadFont("times", "assets/fonts/times.ttf", 28);
    auto* mats = engine.getMaterialManager();
    auto* assets = engine.getAssetRegistry();

    // KROK 1: DODANIE OCEANU
    auto ocean = std::make_shared<OceanRenderer>();
    OceanConfig oceanCfg;
    oceanCfg.enabled = true;
    oceanCfg.waterLevel = 60.0f;
    oceanCfg.waveAmplitude = 1.4f;
    oceanCfg.waveSpeed = 1.0f;
    oceanCfg.waveFrequency = 10.0f;
    oceanCfg.foamThreshold = 2.95f;
    ocean->configure(oceanCfg, "bin/assets/shaders/ocean", "ocean");
    ocean->setEnabled(true);
    renderer->addFeature(ocean);

    // KROK 2: DODANIE KOPUŁY NIEBA (Skydome)
    auto skydome = std::make_shared<SkydomeRenderer>();
    SkydomeConfig skydomeCfg;
    skydomeCfg.enabled = true;
    skydomeCfg.domeScale = 1.5f;
    skydomeCfg.timeOfDay = 10.0f;
    skydomeCfg.cloudCoverage = 0.4f;
    skydomeCfg.cloudSpeed0 = 0.3f;
    skydomeCfg.cloudSpeed1 = 0.15f;
    skydomeCfg.cloudScale0 = 1200.0f;
    skydomeCfg.cloudScale1 = 800.0f;
    skydomeCfg.sunElevation = 50.0f;
    skydome->configure(skydomeCfg, "bin/assets/shaders/skydome", "skydome");
    skydome->setEnabled(true);
    renderer->addFeature(skydome);

    // KROK 3: ŁADOWANIE LUB GENEROWANIE DANYCH TERENU
    // Klasa Terrain reprezentuje siatkę wysokościową (Heightfield).
    // Może zostać wczytana z konfiguracji JSON (wraz z warstwami tekstur) lub
    // wygenerowana proceduralnie bezpośrednio w pamięci RAM.
    auto terrain = std::make_shared<Terrain>();
    bool useJson = terrain->loadFromJson("assets/teren/demo/terrain.json");
    
    if (useJson) {
        // Parametry: rozmiar komórki siatki (10.0), maksymalna wysokość (2.0), podział LOD (20.0)
        // terrain->setScale(15.0f, 2.0f, 20.0f, 6); // Skala w płaszczyźnie XZ - ile jednostek świata na 1 piksel heightmapy
        terrain->buildMesh();
        terrain->uploadTo(*engine.getMeshManager());
        LOG_INFO("Terrain (JSON): {}x{} layers={} mesh={}", terrain->sizeX, terrain->sizeZ, terrain->layers.size(), terrain->meshName());
    } else {
        // Proceduralny algorytm fraktalny Plasma (Midpoint Displacement).
        // Parametry: rozmiar siatki (256x256), współczynnik chropowatości (1.2).
        terrain->generatePlasma(256, 1.2f);
        terrain->smoothEdges(); // Wygładzenie krawędzi do poziomu 0, aby teren płynnie przechodził w wodę
        terrain->buildMesh();
        terrain->uploadTo(*engine.getMeshManager());
        terrain->setScale(5.0f, 2.0f, 20.0f, 6); // Skala w płaszczyźnie XZ - ile jednostek świata na 1 piksel heightmapy
        LOG_INFO("Terrain (plasma): {}x{} layers={} mesh={}", terrain->sizeX, terrain->sizeZ, terrain->layers.size(), terrain->meshName());
    }

    // KROK 4: REJESTROWANIE SPECJALNEGO MATERIAŁU TERENU
    // Potok terenu (Shader "terrain") różni się od domyślnego. Wspiera tzw. Texture Splatting.
    // Zamiast jednej tekstury, shader miesza ze sobą wiele tekstur (np. trawa, skała, piasek)
    // na podstawie masek wagowych (Splat Maps) przesłanych jako kolejne warstwy.
    Material terrainMaterial;
    terrainMaterial.name = "terrain_scene_mat";
    terrainMaterial.shaderProgram = "terrain";
    terrainMaterial.cullMode = CullMode::Back;
    terrainMaterial.depthWrite = true;
    terrainMaterial.depthTest = true;
    if (!terrain->layers.empty() && !terrain->layers[0].texturePath.empty()) {
        TextureHandle texture = renderer->loadTexture("terrain_scene_mat_tex", terrain->layers[0].texturePath);
        terrainMaterial.textureHandle = texture;
    }
    const MaterialHandle terrainMaterialHandle = mats->add(terrainMaterial);

    Entity* terrainEntity = scene->createEntity("terrain_mesh_entity");
    terrainEntity->transform().position = terrain->worldOffset();
    auto* terrainComp = terrainEntity->addComponent<TerrainComponent>(terrain);
    terrainComp->materialHandle = terrainMaterialHandle;
    terrainComp->castShadows = true;
    
    // Konfiguracja mieszania warstw (Texture Splatting) na komponencie siatki
    terrainComp->layerBlendEnabled = true;
    terrainComp->layerCount = std::min<uint32_t>(
        static_cast<uint32_t>(terrain->layers.size()), kMaxTerrainLayers);
    terrainComp->layerWorldSize = terrain->worldSizeXZ();
    
    // Ładowanie tekstur warstwowych i opcjonalnych masek przejść
    for (uint32_t i = 0; i < terrainComp->layerCount; ++i) {
        const std::string texName = "terrain_layer_06_tex_" + std::to_string(i);
        terrainComp->layerTextureHandles[i] = renderer->loadTexture(texName, terrain->layers[i].texturePath);
        if (i > 0 && !terrain->layers[i].maskPath.empty()) {
            const std::string maskName = "terrain_layer_06_mask_" + std::to_string(i);
            terrainComp->layerMaskHandles[i] = renderer->loadTexture(maskName, terrain->layers[i].maskPath);
        }
    }
    
    // Ładowanie mapy wysokości bezpośrednio z pamięci na GPU dla potrzeb teselacji
    terrainComp->heightmapTextureHandle = renderer->loadTextureFromMemory("terrain_heightmap", terrain->getHeightmapData(), terrain->getHeightmapSize(), terrain->getHeightmapSize());
    terrainComp->heightmapEnabled = true;

    // Kamera startująca nad terenem
    auto* camera = scene->createEntity("main_camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = {0.0f, 120.0f, 0.0f};
    camera->target = {1.0f, 0.0f, 0.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 8000.0f;
    scene->setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(camera->getOwner()->transform().position, camera->target);
    engine.enableShaderHotReload(true);

    bool cursorGrabbed = true;
    int generatorMode = 0; // 0 = Plasma, 1 = Fault Formation, 2 = Particle Deposition
    static const char* modeNames[] = {"Plasma", "Fault", "Deposition"};

    // PĘTLA UPDATE
    engine.onUpdate = [&](float dt, Engine& e) {
        static float animTime = 0.0f;
        animTime += dt;
        skydome->setTime(animTime);
        ocean->setTime(animTime);
        
        auto* cc = e.getCameraController();
        auto* activeScene = e.getSceneManager()->activeScene();
        auto* cam = activeScene ? activeScene->activeCamera() : nullptr;
        const auto& snap = e.input();
        
        static bool escWasDown = false;
        const bool escDown = snap.keys[SDL_SCANCODE_ESCAPE];
        if (escDown && !escWasDown) {
            SDL_Event event{};
            event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&event);
        }
        escWasDown = escDown;

        if (cc && cam) {
            cc->moveSpeed = 150.0f;
            float ts = cc->mouseSensitivity;
            cc->rotateYaw(-static_cast<float>(snap.mouseDeltaX) * ts);
            cc->rotatePitch(-static_cast<float>(snap.mouseDeltaY) * ts);
            float vel = cc->moveSpeed * dt;
            if (e.actionDown(kActionMoveForward)) cc->moveForward(vel);
            if (e.actionDown(kActionMoveBackward)) cc->moveForward(-vel);
            if (e.actionDown(kActionMoveLeft)) cc->moveRight(-vel);
            if (e.actionDown(kActionMoveRight)) cc->moveRight(vel);
            if (e.actionDown(kActionMoveUp)) cc->moveUp(vel);
            if (e.actionDown(kActionMoveDown)) cc->moveUp(-vel);
            cc->update(dt, *cam);
            
            // Przyklejenie kamery do powierzchni terenu po kliknięciu klawisza 'G'.
            // Funkcja terrain->getHeight(x, z) interpoluje biliniowo wysokość siatki
            // pod współrzędnymi świata kamery i dodaje offset oczu gracza (+3.0f).
            if (e.actionDown(kActionSnapToGround))
                cam->getOwner()->transform().position.y = terrain->getHeight(cam->getOwner()->transform().position.x, cam->getOwner()->transform().position.z) + 3.0f;
        }

        // KROK 5: Przełączanie tekstu debugującego za pomocą klawisza 'B'
        static bool bWasDown = false;
        static bool showDebugText = true;
        const bool bDown = snap.keys[SDL_SCANCODE_B];
        if (bDown && !bWasDown) {
            showDebugText = !showDebugText;
            LOG_INFO("Toggled terrain debug text: {}", showDebugText);
        }
        bWasDown = bDown;

        // KROK 5b: Przełączanie teselacji za pomocą klawisza 'T'
        static bool tWasDown = false;
        const bool tDown = snap.keys[SDL_SCANCODE_T];
        if (tDown && !tWasDown) {
            bool current = engine.isTessellationEnabled();
            engine.setTessellationEnabled(!current);
            LOG_INFO("Toggled terrain tessellation: {}", !current ? "ENABLED" : "DISABLED");
        }
        tWasDown = tDown;

        // KROK 5c: Zmiana poziomu teselacji += / -= 
        static bool eqWasDown = false;
        const bool eqDown = snap.keys[SDL_SCANCODE_EQUALS];
        if (eqDown && !eqWasDown) {
            float level = engine.getTessellationLevel();
            level = std::min(level + 2.0f, 32.0f);
            engine.setTessellationLevel(level);
            LOG_INFO("Tessellation level: {}", level);
        }
        eqWasDown = eqDown;

        static bool minusWasDown = false;
        const bool minusDown = snap.keys[SDL_SCANCODE_MINUS];
        if (minusDown && !minusWasDown) {
            float level = engine.getTessellationLevel();
            level = std::max(level - 2.0f, 0.0f);
            engine.setTessellationLevel(level);
            LOG_INFO("Tessellation level: {}", level);
        }
        minusWasDown = minusDown;

        // KROK 6: PROCEDURALNE GENEROWANIE NOWEGO TERENU (Klawisze 1, 2, 3)
        bool regen = false;
        if (e.actionPressed(kActionTerrainModePlasma)) { generatorMode = 0; regen = true; }
        if (e.actionPressed(kActionTerrainModeFault)) { generatorMode = 1; regen = true; }
        if (e.actionPressed(kActionTerrainModeDeposition)) { generatorMode = 2; regen = true; }
        
        if (regen) {
            if (generatorMode == 0) {
                // Algorytm 1: Osocze (Plasma/Midpoint displacement) – fraktalny szum tworzący naturalne góry.
                terrain->generatePlasma(256, 1.2f);
                terrain->setScale(15.0f, 7.0f, 20.0f, 6); // Skala w płaszczyźnie XZ - ile jednostek świata na 1 piksel heightmapy
            }
            else if (generatorMode == 1) {
                // Algorytm 2: Uskoki tektoniczne (Fault Formation) – przecina teren losowymi liniami 
                // i podnosi jedną ze stron uskoku, tworząc strome zbocza i doliny.
                terrain->generateFault(256, 160, 100, 1, 5, 0.5f);
                terrain->setScale(15.0f, 2.0f, 20.0f, 6); // Skala w płaszczyźnie XZ - ile jednostek świata na 1 piksel heightmapy
            }
            else {
                // Algorytm 3: Erozja/Depozycja (Particle Deposition) – symuluje spływanie cząsteczek
                // piasku/ziemi ze wzniesień, co naturalnie wygładza strome szczyty.
                terrain->generateDeposition(256, 100, 30, 10, 20, 0.8f);
                terrain->setScale(15.0f, 7.0f, 20.0f, 6); // Skala w płaszczyźnie XZ - ile jednostek świata na 1 piksel heightmapy
            }
            
            terrain->smoothEdges();
            terrain->buildMesh();
            terrain->uploadTo(*engine.getMeshManager());
            terrainComp->heightmapTextureHandle = e.getRenderer()->loadTextureFromMemory("terrain_heightmap", terrain->getHeightmapData(), terrain->getHeightmapSize(), terrain->getHeightmapSize());
            terrainComp->layerWorldSize = terrain->worldSizeXZ();
            terrainEntity->transform().position = terrain->worldOffset();
            if (cam) {
                cam->getOwner()->transform().position.y = terrain->getHeight(cam->getOwner()->transform().position.x, cam->getOwner()->transform().position.z) + 3.0f;
            }
            LOG_INFO("Regenerated: {}", modeNames[generatorMode]);
        }
 
        if (e.actionPressed(kActionTogglePointer)) {
            cursorGrabbed = !cursorGrabbed;
            e.getPlatform()->grabPointer(cursorGrabbed);
        }

        // HUD i wyświetlanie danych diagnostycznych terenu
        std::ostringstream oss;
        oss << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.setTextLabel(oss.str(), "times", 10, 20, 0.7f, {1, 1, 0.3f, 1});
        
        std::ostringstream controls;
        controls << "1=Plasma 2=Fault 3=Deposition | B=Debug | T=Tess | +/-=Level";
        e.setTextLabel(controls.str(), "times", 10, 50, 0.45f, {0.5f, 0.9f, 0.5f, 1.0f});
        
        if (showDebugText) {
            // Obliczamy liczbę węzłów na każdym poziomie LOD
            std::array<int, 6> lodCounts{};
            for (const auto* node : terrain->getVisibleNodes()) {
                int lod = std::max(0, std::min(5, node->lodLevel));
                lodCounts[lod]++;
            }
            
            std::ostringstream mode;
            mode << "[" << modeNames[generatorMode] << "]  Total Boxes: " << terrain->getTotalLeafCount()
                 << "  Visible: " << terrain->visibleCount
                 << " (LOD0:" << lodCounts[0] << " LOD1:" << lodCounts[1]
                 << " LOD2:" << lodCounts[2] << " LOD3:" << lodCounts[3]
                 << " LOD4:" << lodCounts[4] << " LOD5:" << lodCounts[5] << ")";
            mode << " | Tessellation: ";
            if (!engine.isTessellationSupported()) {
                mode << "UNSUPPORTED";
            } else {
                mode << (engine.isTessellationEnabled() ? "ACTIVE" : "DISABLED")
                     << " (level=" << std::fixed << std::setprecision(1) << engine.getTessellationLevel() << ")";
            }
            e.setTextLabel(mode.str(), "times", 10, 72, 0.45f, {0.7f, 0.7f, 1.0f, 1.0f});
        }
        
        if (cam) {
            std::ostringstream pos;
            pos << "pos: " << std::fixed << std::setprecision(0)
                << cam->getOwner()->transform().position.x << " " << cam->getOwner()->transform().position.y << " " << cam->getOwner()->transform().position.z
                << "  h: " << std::setprecision(1) << terrain->getHeight(cam->getOwner()->transform().position.x, cam->getOwner()->transform().position.z);
            e.setTextLabel(pos.str(), "times", 10, 95, 0.5f, {0.7f, 0.7f, 1.0f, 1.0f});
        }
    };

    engine.run();
    return 0;
}
