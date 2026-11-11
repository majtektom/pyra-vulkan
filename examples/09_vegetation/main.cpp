/**
 * 09_vegetation - teren z warstwami tekstur i roślinnością rozsianą automatycznie (VegetationSystem).
 *
 * Roślinność opisują "gatunki" (VegetationSpecies): siatka, materiał, gęstość, zakres skali, wysokości
 * i maksymalne nachylenie terenu. System rozsiewa je po terenie i porusza na wietrze (shader vegetation).
 * Sterowanie: WASD + mysz, spacja / lewy Ctrl - góra / dół, G - przy ziemi, F1 - kursor, F2 - wiatr, Esc - wyjście.
 */
#include "core/logger.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <iomanip>
#include "core/engine.h"
#include "core/entity.h"
#include "render/renderer.h"
#include "core/types.h"
#include "core/mesh_component.h"
#include "camera/camera_component.h"
#include "camera/fly_camera.h"
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

constexpr const char* kActionSnapToGround = "terrain.snap_to_ground";
constexpr const char* kActionTogglePointer = "ui.toggle_pointer";

void configureBindings(Engine& engine) {
    auto& map = engine.inputActions();
    map.clearAll();
    FlyCamera::bindDefaultKeys(map);
    map.bindKey(kActionSnapToGround, SDL_SCANCODE_G);
    map.bindKey(kActionTogglePointer, SDL_SCANCODE_F1);
}

// --- Niebo i woda ----------------------------------------------------------------------------------------

std::shared_ptr<OceanRenderer> addOcean(Renderer* renderer) {
    auto ocean = std::make_shared<OceanRenderer>();
    OceanConfig cfg;
    cfg.enabled = true;
    cfg.waterLevel = 30.0f;
    cfg.waveAmplitude = 1.4f;
    cfg.waveSpeed = 1.0f;
    cfg.waveFrequency = 10.0f;
    cfg.foamThreshold = 2.95f;
    ocean->configure(cfg, "bin/assets/shaders/ocean", "ocean");
    ocean->setEnabled(true);
    renderer->addFeature(ocean);
    return ocean;
}

std::shared_ptr<SkydomeRenderer> addSkydome(Renderer* renderer) {
    auto skydome = std::make_shared<SkydomeRenderer>();
    SkydomeConfig cfg;
    cfg.enabled = true;
    cfg.domeScale = 1.5f;
    cfg.timeOfDay = 14.0f;
    cfg.cloudCoverage = 0.3f;
    cfg.sunElevation = 55.0f;
    skydome->configure(cfg, "bin/assets/shaders/skydome", "skydome");
    skydome->setEnabled(true);
    renderer->addFeature(skydome);
    return skydome;
}

// --- Teren -----------------------------------------------------------------------------------------------

// Teren z pliku JSON (warstwy tekstur + maski przejść); gdy pliku brak - losowy teren "plazmą".
std::shared_ptr<Terrain> createTerrain(Engine& engine, Scene* scene) {
    Renderer* renderer = engine.getRenderer();
    auto terrain = std::make_shared<Terrain>();
    const bool fromJson = terrain->loadFromJson("assets/teren/demo/terrain.json");
    if (!fromJson) {
        terrain->generatePlasma(256, 1.2f);
        terrain->smoothEdges();
    }
    terrain->buildMesh();
    terrain->uploadTo(*engine.getMeshManager());
    if (!fromJson) terrain->setScale(5.0f, 2.0f, 20.0f, 6);
    LOG_INFO("Teren ({}): {}x{}, warstw {}, siatka {}", fromJson ? "JSON" : "plazma", terrain->sizeX, terrain->sizeZ,
             terrain->layers.size(), terrain->meshName());

    Material material;
    material.name = "terrain_scene_mat";
    material.shaderProgram = "terrain";
    material.cullMode = CullMode::Back;
    material.depthWrite = true;
    if (!terrain->layers.empty() && !terrain->layers[0].texturePath.empty()) {
        material.textureHandle = renderer->loadTexture("terrain_scene_mat_tex", terrain->layers[0].texturePath);
    }

    Entity* entity = scene->createEntity("terrain_mesh_entity");
    entity->transform().position = terrain->worldOffset();
    auto* tc = entity->addComponent<TerrainComponent>(terrain);
    tc->materialHandle = engine.getMaterialManager()->add(material);
    tc->castShadows = true;
    tc->layerBlendEnabled = true;
    tc->layerCount = std::min<uint32_t>(static_cast<uint32_t>(terrain->layers.size()), kMaxTerrainLayers);
    tc->layerWorldSize = terrain->worldSizeXZ();
    // Warstwa 0 pokrywa cały teren; kolejne nakładają się według swoich masek.
    for (uint32_t i = 0; i < tc->layerCount; ++i) {
        tc->layerTextureHandles[i] = renderer->loadTexture("terrain_layer_tex_" + std::to_string(i), terrain->layers[i].texturePath);
        if (i > 0 && !terrain->layers[i].maskPath.empty()) {
            tc->layerMaskHandles[i] = renderer->loadTexture("terrain_layer_mask_" + std::to_string(i), terrain->layers[i].maskPath);
        }
    }
    // Mapa wysokości jako tekstura - teselacja na GPU podnosi wierzchołki według niej.
    tc->heightmapTextureHandle = renderer->loadTextureFromMemory("terrain_heightmap", terrain->getHeightmapData(),
                                                                 terrain->getHeightmapSize(), terrain->getHeightmapSize());
    tc->heightmapEnabled = true;
    return terrain;
}

// --- Roślinność ------------------------------------------------------------------------------------------

// Materiał roślinności: shader "vegetation" (kołysanie na wietrze), przezroczysty, widoczny z obu stron.
MaterialHandle addPlantMaterial(MaterialManager* materials, const std::string& name, TextureHandle texture,
                                std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f}) {
    Material m;
    m.name = name;
    m.shaderProgram = "vegetation";
    m.textureHandle = texture;
    m.blendMode = BlendMode::Alpha;
    m.cullMode = CullMode::Disabled;
    m.depthWrite = false;
    m.color = color;
    return materials->add(m);
}

// Gatunek roślin: gdzie rośnie (wysokość, maks. nachylenie w stopniach), jak gęsto i w jakich rozmiarach.
VegetationSpecies makeSpecies(const std::string& name, VegetationType type, const std::string& mesh, MaterialHandle material,
                              float density, float minScale, float maxScale, float minHeight, float maxHeight,
                              float slopeMax) {
    VegetationSpecies s;
    s.name = name;
    s.type = type;
    s.meshName = mesh;
    s.materialHandle = material;
    s.density = density;
    s.minScale = minScale;
    s.maxScale = maxScale;
    s.minHeight = minHeight;
    s.maxHeight = maxHeight;
    s.slopeMax = slopeMax;
    return s;
}

void addVegetation(Engine& engine, Scene* scene, const std::shared_ptr<Terrain>& terrain) {
    Renderer* renderer = engine.getRenderer();
    MaterialManager* materials = engine.getMaterialManager();
    MeshManager* meshes = engine.getMeshManager();
    auto vegetation = std::make_unique<VegetationSystem>(*meshes, *materials, *engine.getMaterialSystem());

    const TextureHandle leafTex = renderer->loadTexture("veg_leaf", "assets/textures/tliscie.dds");
    const TextureHandle barkTex = renderer->loadTexture("veg_bark", "assets/textures/tkora.dds");
    const TextureHandle grassTex = renderer->loadTexture("veg_grass", "assets/textures/trawa.dds");

    const MaterialHandle grassMat = addPlantMaterial(materials, "veg_grass_mat", grassTex, {0.6f, 0.8f, 0.3f, 1.0f});
    const MaterialHandle bushMat = addPlantMaterial(materials, "veg_bush_mat", leafTex);
    const MaterialHandle leafMat = addPlantMaterial(materials, "veg_leaf_mat", leafTex);
    // Pień: zwykły nieprzezroczysty materiał (shader default).
    Material bark;
    bark.name = "veg_bark_mat";
    bark.shaderProgram = "default";
    bark.textureHandle = barkTex;
    bark.blendMode = BlendMode::Disabled;
    bark.cullMode = CullMode::Back;
    bark.depthWrite = true;
    const MaterialHandle barkMat = materials->add(bark);

    // Siatki proceduralne: źdźbła (skrzyżowane prostokąty), krzaki, pień i korona drzewa.
    meshes->loadMesh("grass_blade", VegetationSystem::createGrassBladeMesh(1.2f, 2.5f));
    meshes->loadMesh("grass_tall", VegetationSystem::createGrassBladeMesh(1.0f, 4.0f));
    meshes->loadMesh("bush_small", VegetationSystem::createBushMesh(2.0f, 3.0f));
    meshes->loadMesh("bush_large", VegetationSystem::createBushMesh(4.0f, 6.0f));
    meshes->loadMesh("tree_trunk", VegetationSystem::createGrassBladeMesh(1.5f, 10.0f));
    meshes->loadMesh("tree_crown", VegetationSystem::createGrassBladeMesh(8.0f, 8.0f));

    //                                   nazwa        typ                    siatka         materiał  gęst.  skala      wysokość     nachyl.
    vegetation->addSpecies(makeSpecies("Grass",     VegetationType::Grass, "grass_blade", grassMat, 0.3f,  0.6f, 1.0f, -5.0f, 80.0f,  35.0f));
    vegetation->addSpecies(makeSpecies("TallGrass", VegetationType::Grass, "grass_tall",  grassMat, 0.05f, 0.8f, 1.3f, -5.0f, 60.0f,  30.0f));
    vegetation->addSpecies(makeSpecies("Bush",      VegetationType::Bush,  "bush_small",  bushMat,  0.02f, 1.0f, 2.5f,  5.0f, 80.0f,  40.0f));
    vegetation->addSpecies(makeSpecies("LargeBush", VegetationType::Bush,  "bush_large",  bushMat,  0.01f, 1.0f, 3.0f, 10.0f, 100.0f, 35.0f));

    // Drzewa: pień i korona to dwa gatunki generowane parami (generatePaired) w tym samym miejscu.
    const VegetationSpecies trunk = makeSpecies("Trunk", VegetationType::Tree, "tree_trunk", barkMat, 0.2f, 0.8f, 1.5f, 5.0f, 120.0f, 35.0f);
    VegetationSpecies crown = makeSpecies("Crown", VegetationType::Tree, "tree_crown", leafMat, 0.2f, 1.5f, 3.5f, 5.0f, 120.0f, 35.0f);
    crown.snapToTerrain = false;   // korona na szczycie pnia, nie na ziemi

    const float worldSize = terrain->worldSizeXZ();
    auto heightAt = [&](float x, float z) { return terrain->getHeight(x, z); };
    vegetation->generate(*scene, worldSize, 0.0f, 0.0f, heightAt);
    vegetation->generatePaired(*scene, worldSize, 0.0f, 0.0f, heightAt, trunk, crown);
    scene->addSystem(std::move(vegetation));
}

// Kamera startuje nad środkiem mapy, 40 jednostek nad terenem (stała wysokość mogła wypaść wewnątrz góry).
void setupCamera(Engine& engine, Scene* scene, const Terrain& terrain) {
    auto* camera = scene->createEntity("main_camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = {0.0f, std::max(100.0f, terrain.getHeight(0.0f, 0.0f) + 40.0f), 0.0f};
    // Patrzymy przed siebie i lekko w dół (na krajobraz, nie pionowo pod nogi).
    const Vec3 start = camera->getOwner()->transform().position;
    camera->target = {start.x + 100.0f, start.y - 25.0f, start.z + 100.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 18000.0f;
    scene->setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(camera->getOwner()->transform().position, camera->target);
}

// Pora dnia płynie 0.05 h na sekundę; słońce wschodzi o 6:00 i zachodzi o 18:00 (maks. 65 stopni nad horyzontem).
void advanceDayCycle(SkydomeRenderer& skydome, float& timeOfDay, float dt) {
    timeOfDay += dt * 0.05f;
    if (timeOfDay > 24.0f) timeOfDay -= 24.0f;
    const float dayProgress = std::clamp((timeOfDay - 6.0f) / 12.0f, 0.0f, 1.0f);
    auto cfg = skydome.getConfig();
    cfg.timeOfDay = timeOfDay;
    cfg.sunElevation = std::sin(dayProgress * PI) * 65.0f;
    cfg.sunAzimuth = 90.0f + dayProgress * 180.0f;
    skydome.setConfig(cfg);
}

void drawHud(Engine& e, const Terrain& terrain, bool windEnabled) {
    e.setTextLabel("FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())), "times", 10, 20, 0.7f, {1, 1, 0.3f, 1});
    e.setTextLabel(std::string("WASD - ruch, spacja/Ctrl - góra/dół, G - przy ziemi, F1 - kursor, F2 - wiatr: ") +
                       (windEnabled ? "WŁ" : "WYŁ"),
                   "times", 10, 50, 0.45f, {0.5f, 0.9f, 0.5f, 1.0f});

    CameraComponent* camera = e.getSceneManager()->activeScene()->activeCamera();
    if (!camera) return;
    const Vec3 p = camera->getOwner()->transform().position;
    std::ostringstream pos;
    pos << "pos: " << std::fixed << std::setprecision(0) << p.x << " " << p.y << " " << p.z
        << "  h: " << std::setprecision(1) << terrain.getHeight(p.x, p.z);
    e.setTextLabel(pos.str(), "times", 10, 95, 0.5f, {0.7f, 0.7f, 1.0f, 1.0f});
}

} // namespace

int main() {
    Engine engine;
    if (!engine.initialize("engine3d: vegetation", 1280, 800)) return 1;
    Scene* scene = engine.getSceneManager()->activeScene();
    if (!scene) return 1;

    configureBindings(engine);
    engine.setWireframeEnabled(false);
    engine.setTessellationEnabled(true);
    engine.setTessellationLevel(2.0f);
    engine.loadFont("times", "assets/fonts/times.ttf", 28);
    engine.getRenderer()->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Systemy terenu: siatki węzłów LOD i przyklejanie obiektów do powierzchni.
    scene->addSystem(std::make_unique<TerrainSystem>(*engine.getMeshManager(), *engine.getMaterialManager(),
                                                     *engine.getMaterialSystem()));
    scene->addSystem(std::make_unique<TerrainPlacementSystem>());

    auto ocean = addOcean(engine.getRenderer());
    auto skydome = addSkydome(engine.getRenderer());
    const std::shared_ptr<Terrain> terrain = createTerrain(engine, scene);
    addVegetation(engine, scene, terrain);

    auto valleyFog = std::make_shared<VolumetricFogRenderer>();
    valleyFog->configure("bin/assets/shaders/fog");
    valleyFog->setFogRadius(2200.0f);
    valleyFog->setEnabled(true);
    engine.getRenderer()->addFeature(valleyFog);

    setupCamera(engine, scene, *terrain);
    engine.enableShaderHotReload(true);

    bool cursorGrabbed = true;
    bool windEnabled = true;
    float animTime = 0.0f;
    float timeOfDay = 14.0f;

    engine.onUpdate = [&](float dt, Engine& e) {
        if (e.keyPressed(SDL_SCANCODE_ESCAPE)) e.requestQuit();

        // Animacja wody i chmur oraz pora dnia.
        animTime += dt;
        skydome->setTime(animTime);
        ocean->setTime(animTime);
        advanceDayCycle(*skydome, timeOfDay, dt);

        // Wiatr: czas wiatru przesuwa fazę kołysania roślin w shaderze.
        if (e.keyPressed(SDL_SCANCODE_F2)) {
            windEnabled = !windEnabled;
            if (!windEnabled) e.setWindTime(0.0f);
        }
        if (windEnabled) e.setWindTime(e.getWindTime() + dt * 0.5f);

        FlyCamera::update(e, dt, 150.0f);
        CameraComponent* camera = scene->activeCamera();
        if (camera && e.actionDown(kActionSnapToGround)) {
            Vec3& p = camera->getOwner()->transform().position;
            p.y = terrain->getHeight(p.x, p.z) + 3.0f;
        }

        if (e.actionPressed(kActionTogglePointer)) {
            cursorGrabbed = !cursorGrabbed;
            e.getPlatform()->grabPointer(cursorGrabbed);
        }
        drawHud(e, *terrain, windEnabled);
    };

    engine.run();
    return 0;
}
