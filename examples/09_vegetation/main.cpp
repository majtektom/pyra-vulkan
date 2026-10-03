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
#include "object/terrain/terrain_placement_component.h"
#include "object/terrain/terrain_placement_system.h"
#include "object/vegetation/vegetation_system.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"
#include "feature/fog/volumetric_fog_renderer.h"

using namespace engine3d;

namespace {

constexpr const char* kActionMoveForward = "move.forward";
constexpr const char* kActionMoveBackward = "move.backward";
constexpr const char* kActionMoveLeft = "move.left";
constexpr const char* kActionMoveRight = "move.right";
constexpr const char* kActionMoveUp = "move.up";
constexpr const char* kActionMoveDown = "move.down";
constexpr const char* kActionSnapToGround = "terrain.snap_to_ground";
constexpr const char* kActionTogglePointer = "ui.toggle_pointer";

void configureBindings(Engine& engine) {
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
}

} // namespace

int main() {
    Engine engine;
    if (!engine.initialize("engine3d: vegetation", 1280, 800)) return 1;

    configureBindings(engine);
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
    auto* meshMgr = engine.getMeshManager();

    // Ocean
    auto ocean = std::make_shared<OceanRenderer>();
    OceanConfig oceanCfg;
    oceanCfg.enabled = true;
    oceanCfg.waterLevel = 30.0f;
    oceanCfg.waveAmplitude = 1.4f;
    oceanCfg.waveSpeed = 1.0f;
    oceanCfg.waveFrequency = 10.0f;
    oceanCfg.foamThreshold = 2.95f;
    ocean->configure(oceanCfg, "bin/assets/shaders/ocean", "ocean");
    ocean->setEnabled(true);
    renderer->addFeature(ocean);

    // Skydome
    auto skydome = std::make_shared<SkydomeRenderer>();
    SkydomeConfig skydomeCfg;
    skydomeCfg.enabled = true;
    skydomeCfg.domeScale = 1.5f;
    skydomeCfg.timeOfDay = 14.0f;
    skydomeCfg.cloudCoverage = 0.3f;
    skydomeCfg.sunElevation = 55.0f;
    skydome->configure(skydomeCfg, "bin/assets/shaders/skydome", "skydome");
    skydome->setEnabled(true);
    renderer->addFeature(skydome);

    // Terrain
    auto terrain = std::make_shared<Terrain>();
    bool useJson = terrain->loadFromJson("assets/teren/demo/terrain.json");

    if (useJson) {
        terrain->buildMesh();
        terrain->uploadTo(*meshMgr);
        LOG_INFO("Terrain (JSON): {}x{} layers={} mesh={}", terrain->sizeX, terrain->sizeZ, terrain->layers.size(), terrain->meshName());
    } else {
        terrain->generatePlasma(256, 1.2f);
        terrain->smoothEdges();
        terrain->buildMesh();
        terrain->uploadTo(*meshMgr);
        terrain->setScale(5.0f, 2.0f, 20.0f, 6);
        LOG_INFO("Terrain (plasma): {}x{} layers={} mesh={}", terrain->sizeX, terrain->sizeZ, terrain->layers.size(), terrain->meshName());
    }

    Material terrainMaterial;
    terrainMaterial.name = "terrain_scene_mat";
    terrainMaterial.shaderProgram = "terrain";
    terrainMaterial.cullMode = CullMode::Back;
    terrainMaterial.depthWrite = true;
    if (!terrain->layers.empty() && !terrain->layers[0].texturePath.empty()) {
        TextureHandle tex = renderer->loadTexture("terrain_scene_mat_tex", terrain->layers[0].texturePath);
        terrainMaterial.textureHandle = tex;
    }
    const MaterialHandle terrainMaterialHandle = mats->add(terrainMaterial);

    Entity* terrainEntity = scene->createEntity("terrain_mesh_entity");
    terrainEntity->transform().position = terrain->worldOffset();
    auto* terrainComp = terrainEntity->addComponent<TerrainComponent>(terrain);
    terrainComp->materialHandle = terrainMaterialHandle;
    terrainComp->castShadows = true;
    terrainComp->layerBlendEnabled = true;
    terrainComp->layerCount = std::min<uint32_t>(static_cast<uint32_t>(terrain->layers.size()), kMaxTerrainLayers);
    terrainComp->layerWorldSize = terrain->worldSizeXZ();

    for (uint32_t i = 0; i < terrainComp->layerCount; ++i) {
        const std::string texName = "terrain_layer_tex_" + std::to_string(i);
        terrainComp->layerTextureHandles[i] = renderer->loadTexture(texName, terrain->layers[i].texturePath);
        if (i > 0 && !terrain->layers[i].maskPath.empty()) {
            const std::string maskName = "terrain_layer_mask_" + std::to_string(i);
            terrainComp->layerMaskHandles[i] = renderer->loadTexture(maskName, terrain->layers[i].maskPath);
        }
    }
    terrainComp->heightmapTextureHandle = renderer->loadTextureFromMemory("terrain_heightmap", terrain->getHeightmapData(), terrain->getHeightmapSize(), terrain->getHeightmapSize());
    terrainComp->heightmapEnabled = true;

    // === Vegetation ===
    auto vegSystem = std::make_unique<VegetationSystem>(*meshMgr, *mats, *engine.getMaterialSystem());

    TextureHandle leafTex = renderer->loadTexture("veg_leaf", "assets/textures/tliscie.dds");
    TextureHandle barkTex = renderer->loadTexture("veg_bark", "assets/textures/tkora.dds");
    TextureHandle grassTex = renderer->loadTexture("veg_grass", "assets/textures/trawa.dds");

    // Materiał trawy (alpha, dwustronny, shader vegetation)
    Material grassMat;
    grassMat.name = "veg_grass_mat";
    grassMat.shaderProgram = "vegetation";
    grassMat.textureHandle = grassTex;
    grassMat.blendMode = BlendMode::Alpha;
    grassMat.cullMode = CullMode::Disabled;
    grassMat.depthWrite = false;
    grassMat.color = {0.6f, 0.8f, 0.3f, 1.0f};
    MaterialHandle grassHandle = mats->add(grassMat);

    // Materiał krzaków (shader vegetation)
    Material bushMat;
    bushMat.name = "veg_bush_mat";
    bushMat.shaderProgram = "vegetation";
    bushMat.textureHandle = leafTex;
    bushMat.blendMode = BlendMode::Alpha;
    bushMat.cullMode = CullMode::Disabled;
    bushMat.depthWrite = false;
    MaterialHandle bushHandle = mats->add(bushMat);

    // Materiał pnia drzewa (default shader, nieprzezroczysty)
    Material barkMat;
    barkMat.name = "veg_bark_mat";
    barkMat.shaderProgram = "default";
    barkMat.textureHandle = barkTex;
    barkMat.blendMode = BlendMode::Disabled;
    barkMat.cullMode = CullMode::Back;
    barkMat.depthWrite = true;
    MaterialHandle barkHandle = mats->add(barkMat);

    // Materiał liści (shader vegetation, alpha)
    Material leafMat;
    leafMat.name = "veg_leaf_mat";
    leafMat.shaderProgram = "vegetation";
    leafMat.textureHandle = leafTex;
    leafMat.blendMode = BlendMode::Alpha;
    leafMat.cullMode = CullMode::Disabled;
    leafMat.depthWrite = false;
    MaterialHandle leafHandle = mats->add(leafMat);

    // Generuj i załaduj siatki proceduralne
    MeshData grassBlade = VegetationSystem::createGrassBladeMesh(1.2f, 2.5f);
    meshMgr->loadMesh("grass_blade", grassBlade);

    MeshData grassTall = VegetationSystem::createGrassBladeMesh(1.0f, 4.0f);
    meshMgr->loadMesh("grass_tall", grassTall);

    MeshData bushSmall = VegetationSystem::createBushMesh(2.0f, 3.0f);
    meshMgr->loadMesh("bush_small", bushSmall);

    MeshData bushLarge = VegetationSystem::createBushMesh(4.0f, 6.0f);
    meshMgr->loadMesh("bush_large", bushLarge);

    // Pień drzewa - prosty cross-quad
    MeshData trunkMesh = VegetationSystem::createGrassBladeMesh(1.5f, 10.0f);
    meshMgr->loadMesh("tree_trunk", trunkMesh);

    // Korona drzewa
    MeshData crownMesh = VegetationSystem::createGrassBladeMesh(8.0f, 8.0f);
    meshMgr->loadMesh("tree_crown", crownMesh);

    // Zdefiniuj gatunki wegetacji
    VegetationSpecies grass;
    grass.name = "Grass";
    grass.type = VegetationType::Grass;
    grass.meshName = "grass_blade";
    grass.materialHandle = grassHandle;
    grass.density = 0.3f;
    grass.minScale = 0.6f;
    grass.maxScale = 1.0f;
    grass.minHeight = -5.0f;
    grass.maxHeight = 80.0f;
    grass.slopeMax = 35.0f;
    vegSystem->addSpecies(grass);

    VegetationSpecies tallGrass;
    tallGrass.name = "TallGrass";
    tallGrass.type = VegetationType::Grass;
    tallGrass.meshName = "grass_tall";
    tallGrass.materialHandle = grassHandle;
    tallGrass.density = 0.05f;
    tallGrass.minScale = 0.8f;
    tallGrass.maxScale = 1.3f;
    tallGrass.minHeight = -5.0f;
    tallGrass.maxHeight = 60.0f;
    tallGrass.slopeMax = 30.0f;
    vegSystem->addSpecies(tallGrass);

    VegetationSpecies bush;
    bush.name = "Bush";
    bush.type = VegetationType::Bush;
    bush.meshName = "bush_small";
    bush.materialHandle = bushHandle;
    bush.density = 0.02f;
    bush.minScale = 1.0f;
    bush.maxScale = 2.5f;
    bush.minHeight = 5.0f;
    bush.maxHeight = 80.0f;
    bush.slopeMax = 40.0f;
    vegSystem->addSpecies(bush);

    VegetationSpecies largeBush;
    largeBush.name = "LargeBush";
    largeBush.type = VegetationType::Bush;
    largeBush.meshName = "bush_large";
    largeBush.materialHandle = bushHandle;
    largeBush.density = 0.01f;
    largeBush.minScale = 1.0f;
    largeBush.maxScale = 3.0f;
    largeBush.minHeight = 10.0f;
    largeBush.maxHeight = 100.0f;
    largeBush.slopeMax = 35.0f;
    vegSystem->addSpecies(largeBush);

    // Drzewa: pień i korona jako osobne gatunki (osobne encje w tym samym miejscu)
    VegetationSpecies trunk;
    trunk.name = "Trunk";
    trunk.type = VegetationType::Tree;
    trunk.meshName = "tree_trunk";
    trunk.materialHandle = barkHandle;
    trunk.density = 0.2f;
    trunk.minScale = 0.8f;
    trunk.maxScale = 1.5f;
    trunk.minHeight = 5.0f;
    trunk.maxHeight = 120.0f;
    trunk.slopeMax = 35.0f;
    // Pień i korona nie są dodawane do addSpecies -
    // będą generowane parami przez generatePaired poniżej.

    VegetationSpecies crown;
    crown.name = "Crown";
    crown.type = VegetationType::Tree;
    crown.meshName = "tree_crown";
    crown.materialHandle = leafHandle;
    crown.density = 0.2f;
    crown.minScale = 1.5f;
    crown.maxScale = 3.5f;
    crown.minHeight = 5.0f;
    crown.maxHeight = 120.0f;
    crown.slopeMax = 35.0f;
    crown.snapToTerrain = false; // korona na szczycie pnia, nie na ziemi

    // Generuj wegetację na terenie
    float worldSize = terrain->worldSizeXZ();
    vegSystem->generate(*scene, worldSize, 0.0f, 0.0f,
                        [&](float x, float z) { return terrain->getHeight(x, z); });

    // Generuj drzewa parami (pień + korona w tym samym miejscu)
    vegSystem->generatePaired(*scene, worldSize, 0.0f, 0.0f,
                              [&](float x, float z) { return terrain->getHeight(x, z); },
                              trunk, crown);

    scene->addSystem(std::move(vegSystem));

    // Fog
    auto valleyFog = std::make_shared<VolumetricFogRenderer>();
    valleyFog->configure("bin/assets/shaders/fog");
    valleyFog->setFogRadius(2200.0f);
    valleyFog->setEnabled(true);
    renderer->addFeature(valleyFog);

    // Camera
    auto* camera = scene->createEntity("main_camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = {0.0f, 100.0f, 0.0f};
    camera->target = {1.0f, 0.0f, 0.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 18000.0f;
    scene->setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(camera->getOwner()->transform().position, camera->target);
    engine.enableShaderHotReload(true);

    bool cursorGrabbed = true;
    bool windEnabled = true;

    engine.onUpdate = [&](float dt, Engine& e) {
        static float animTime = 0.0f;
        animTime += dt;
        skydome->setTime(animTime);
        ocean->setTime(animTime);

        static float timeOfDay = 14.0f;
        timeOfDay += dt * 0.05f;
        if (timeOfDay > 24.0f) timeOfDay -= 24.0f;

        float dayProgress = (timeOfDay - 6.0f) / 12.0f;
        dayProgress = std::max(0.0f, std::min(1.0f, dayProgress));
        float sunEl = std::sin(dayProgress * PI) * 65.0f;
        float sunAz = 90.0f + dayProgress * 180.0f;

        auto cfg = skydome->getConfig();
        cfg.timeOfDay = timeOfDay;
        cfg.sunElevation = sunEl;
        cfg.sunAzimuth = sunAz;
        skydome->setConfig(cfg);

        if (windEnabled) {
            float wt = e.getWindTime() + dt * 0.5f;
            e.setWindTime(wt);
        }

        auto* cc = e.getCameraController();
        auto* cam = scene->activeCamera();
        const auto& snap = e.input();

        static bool escWasDown = false;
        if (snap.keys[SDL_SCANCODE_ESCAPE] && !escWasDown) {
            SDL_Event event{};
            event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&event);
        }
        escWasDown = snap.keys[SDL_SCANCODE_ESCAPE];

        if (cc && cam) {
            cc->moveSpeed = 150.0f;
            cc->rotateYaw(-static_cast<float>(snap.mouseDeltaX) * cc->mouseSensitivity);
            cc->rotatePitch(-static_cast<float>(snap.mouseDeltaY) * cc->mouseSensitivity);
            float vel = cc->moveSpeed * dt;
            if (e.actionDown(kActionMoveForward)) cc->moveForward(vel);
            if (e.actionDown(kActionMoveBackward)) cc->moveForward(-vel);
            if (e.actionDown(kActionMoveLeft)) cc->moveRight(-vel);
            if (e.actionDown(kActionMoveRight)) cc->moveRight(vel);
            if (e.actionDown(kActionMoveUp)) cc->moveUp(vel);
            if (e.actionDown(kActionMoveDown)) cc->moveUp(-vel);
            cc->update(dt, *cam);

            if (e.actionDown(kActionSnapToGround))
                cam->getOwner()->transform().position.y = terrain->getHeight(cam->getOwner()->transform().position.x, cam->getOwner()->transform().position.z) + 3.0f;
        }

        // Włącz/wyłącz wiatr
        static bool wWasDown = false;
        if (snap.keys[SDL_SCANCODE_F2] && !wWasDown) {
            windEnabled = !windEnabled;
            if (!windEnabled) e.setWindTime(0.0f);
        }
        wWasDown = snap.keys[SDL_SCANCODE_F2];

        if (e.actionPressed(kActionTogglePointer)) {
            cursorGrabbed = !cursorGrabbed;
            e.getPlatform()->grabPointer(cursorGrabbed);
        }

        std::ostringstream fps;
        fps << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.setTextLabel(fps.str(), "times", 10, 20, 0.7f, {1, 1, 0.3f, 1});

        std::ostringstream hud;
        hud << "WASD=Move Sp=Up LCtrl=Down G=Snap F1=Pointer F2=Wind: " << (windEnabled ? "ON" : "OFF");
        e.setTextLabel(hud.str(), "times", 10, 50, 0.45f, {0.5f, 0.9f, 0.5f, 1.0f});

        if (cam) {
            std::ostringstream pos;
            pos << "pos: " << std::fixed << std::setprecision(0)
                << cam->getOwner()->transform().position.x << " "
                << cam->getOwner()->transform().position.y << " "
                << cam->getOwner()->transform().position.z
                << "  h: " << std::setprecision(1)
                << terrain->getHeight(cam->getOwner()->transform().position.x, cam->getOwner()->transform().position.z);
            e.setTextLabel(pos.str(), "times", 10, 95, 0.5f, {0.7f, 0.7f, 1.0f, 1.0f});
        }
    };

    engine.run();
    return 0;
}