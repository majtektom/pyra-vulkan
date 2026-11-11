/**
 * 06_terrain - teren z poziomami szczegółowości (LOD), teselacją na GPU i trzema generatorami wysokości.
 *
 * Teren to siatka wysokości podzielona na węzły; bliskie węzły mają gęstą siatkę, dalekie rzadką (LOD),
 * a teselacja na GPU dodaje szczegóły blisko kamery. Tekstury warstw (trawa, skała, piasek...) są mieszane
 * według masek (texture splatting). Do tego ocean, kopuła nieba z cyklem dnia i mgła w dolinach.
 * Sterowanie: WASD + mysz, spacja / lewy Ctrl - góra / dół, G - przy ziemi, 1/2/3 - nowy teren (plazma, uskoki,
 * osypywanie), B - napisy debug, T - teselacja, +/- - poziom teselacji, F1 - kursor, Esc - wyjście.
 */
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>

#include "camera/camera_component.h"
#include "camera/fly_camera.h"
#include "core/engine.h"
#include "core/entity.h"
#include "core/logger.h"
#include "feature/fog/volumetric_fog_renderer.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"
#include "object/terrain/terrain.h"
#include "object/terrain/terrain_component.h"
#include "object/terrain/terrain_placement_system.h"
#include "object/terrain/terrain_system.h"
#include "render/renderer.h"

using namespace engine3d;

namespace {

constexpr const char* kActionSnapToGround = "terrain.snap_to_ground";
constexpr const char* kActionTogglePointer = "ui.toggle_pointer";
constexpr const char* kActionTerrainModePlasma = "terrain.mode_plasma";
constexpr const char* kActionTerrainModeFault = "terrain.mode_fault";
constexpr const char* kActionTerrainModeDeposition = "terrain.mode_deposition";

constexpr float kWaterLevel = 60.0f;

enum class TerrainGenerator { Plasma, Fault, Deposition };
const char* generatorName(TerrainGenerator g) {
    switch (g) {
        case TerrainGenerator::Plasma: return "Plazma";
        case TerrainGenerator::Fault: return "Uskoki";
        case TerrainGenerator::Deposition: return "Osypywanie";
    }
    return "";
}

void configureBindings(Engine& engine) {
    auto& map = engine.inputActions();
    map.clearAll();
    FlyCamera::bindDefaultKeys(map);
    map.bindKey(kActionSnapToGround, SDL_SCANCODE_G);
    map.bindKey(kActionTogglePointer, SDL_SCANCODE_F1);
    map.bindKey(kActionTerrainModePlasma, SDL_SCANCODE_1);
    map.bindKey(kActionTerrainModeFault, SDL_SCANCODE_2);
    map.bindKey(kActionTerrainModeDeposition, SDL_SCANCODE_3);
}

// Lokalna mgła osiada w trzech najniższych (nad wodą), oddalonych od siebie dolinach.
std::vector<LocalFogVolume> findValleyFogVolumes(const Terrain& terrain, float waterLevel) {
    struct Candidate {
        Vec3 position;
    };

    std::vector<Candidate> candidates;
    const float worldSize = terrain.worldSizeXZ();
    constexpr int samplesPerAxis = 14;
    for (int z = 1; z < samplesPerAxis - 1; ++z) {
        for (int x = 1; x < samplesPerAxis - 1; ++x) {
            const float worldX = (static_cast<float>(x) / (samplesPerAxis - 1) - 0.5f) * worldSize;
            const float worldZ = (static_cast<float>(z) / (samplesPerAxis - 1) - 0.5f) * worldSize;
            const float height = terrain.getHeight(worldX, worldZ);
            if (height > waterLevel + 2.0f) {
                candidates.push_back({{worldX, height, worldZ}});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.position.y < b.position.y;
    });

    std::vector<LocalFogVolume> volumes;
    const float minimumSpacing = worldSize * 0.22f;
    const float horizontalExtent = std::clamp(worldSize * 0.09f, 140.0f, 450.0f);
    for (const Candidate& candidate : candidates) {
        const bool separated = std::all_of(volumes.begin(), volumes.end(), [&](const LocalFogVolume& volume) {
            const float dx = candidate.position.x - volume.center.x;
            const float dz = candidate.position.z - volume.center.z;
            return dx * dx + dz * dz >= minimumSpacing * minimumSpacing;
        });
        if (!separated) continue;

        LocalFogVolume volume;
        volume.center = {candidate.position.x, candidate.position.y + 18.0f, candidate.position.z};
        volume.halfExtents = {horizontalExtent, 42.0f, horizontalExtent};
        volume.density = 0.005f;
        volume.edgeFade = 0.4f;
        volumes.push_back(volume);
        if (volumes.size() == 3) break;
    }
    return volumes;
}

// --- Niebo i woda ----------------------------------------------------------------------------------------

std::shared_ptr<OceanRenderer> addOcean(Renderer* renderer) {
    auto ocean = std::make_shared<OceanRenderer>();
    OceanConfig cfg;
    cfg.enabled = true;
    cfg.waterLevel = kWaterLevel;
    cfg.waveAmplitude = 1.4f;
    cfg.waveSpeed = 1.0f;
    cfg.waveFrequency = 10.0f;
    cfg.foamThreshold = 2.95f;
    ocean->configure(cfg, "bin/assets/shaders/ocean", "ocean");
    ocean->setEnabled(true);
    renderer->addFeature(ocean);
    return ocean;
}

// Kopuła nieba z chmurami (zamiast niej można użyć SkyboxRenderer - zob. przykład 03).
std::shared_ptr<SkydomeRenderer> addSkydome(Renderer* renderer) {
    auto skydome = std::make_shared<SkydomeRenderer>();
    SkydomeConfig cfg;
    cfg.enabled = true;
    cfg.domeScale = 1.5f;
    cfg.timeOfDay = 10.0f;
    cfg.cloudCoverage = 0.4f;
    cfg.cloudSpeed0 = 0.3f;
    cfg.cloudSpeed1 = 0.15f;
    cfg.cloudScale0 = 1200.0f;
    cfg.cloudScale1 = 800.0f;
    cfg.sunElevation = 50.0f;
    skydome->configure(cfg, "bin/assets/shaders/skydome", "skydome");
    skydome->setEnabled(true);
    renderer->addFeature(skydome);
    return skydome;
}

// Pogoda: pora dnia (0.05 h/s, słońce od 6:00 do 18:00) i zachmurzenie wahające się między 0.15 a 0.7.
struct Weather {
    float timeOfDay = 10.0f;
    float cloudCoverage = 0.4f;
    float cloudDirection = 1.0f;   // +1 chmur przybywa, -1 ubywa

    void update(SkydomeRenderer& skydome, float dt) {
        timeOfDay += dt * 0.05f;
        if (timeOfDay > 24.0f) timeOfDay -= 24.0f;
        cloudCoverage += dt * 0.02f * cloudDirection;
        if (cloudCoverage >= 0.7f) { cloudCoverage = 0.7f; cloudDirection = -1.0f; }
        else if (cloudCoverage <= 0.15f) { cloudCoverage = 0.15f; cloudDirection = 1.0f; }

        const float dayProgress = std::clamp((timeOfDay - 6.0f) / 12.0f, 0.0f, 1.0f);
        auto cfg = skydome.getConfig();
        cfg.timeOfDay = timeOfDay;
        cfg.cloudCoverage = cloudCoverage;
        cfg.sunElevation = std::sin(dayProgress * PI) * 65.0f;
        cfg.sunAzimuth = 90.0f + dayProgress * 180.0f;
        skydome.setConfig(cfg);
    }
};

// --- Teren -----------------------------------------------------------------------------------------------

// Teren wczytany z JSON (z warstwami tekstur) albo, gdy pliku brak, wygenerowany fraktalnie (plazma).
std::shared_ptr<Terrain> loadOrGenerateTerrain(Engine& engine) {
    auto terrain = std::make_shared<Terrain>();
    const bool fromJson = terrain->loadFromJson("assets/teren/demo/terrain.json");
    if (!fromJson) {
        terrain->generatePlasma(256, 1.2f);   // rozmiar siatki 256x256, chropowatość 1.2
        terrain->smoothEdges();               // brzegi do zera - teren płynnie schodzi do wody
    }
    terrain->buildMesh();
    terrain->uploadTo(*engine.getMeshManager());
    // Skala: jednostki świata na piksel mapy wysokości (XZ), mnożnik wysokości, LOD.
    if (!fromJson) terrain->setScale(5.0f, 2.0f, 20.0f, 6);
    LOG_INFO("Teren ({}): {}x{}, warstw {}, siatka {}", fromJson ? "JSON" : "plazma", terrain->sizeX, terrain->sizeZ,
             terrain->layers.size(), terrain->meshName());
    return terrain;
}

// Encja terenu. Shader "terrain" miesza wiele tekstur (trawa, skała, piasek) według masek kolejnych warstw
// (texture splatting); mapa wysokości jako tekstura pozwala teselacji podnosić wierzchołki na GPU.
TerrainComponent* createTerrainEntity(Engine& engine, Scene* scene, const std::shared_ptr<Terrain>& terrain) {
    Renderer* renderer = engine.getRenderer();
    Material material;
    material.name = "terrain_scene_mat";
    material.shaderProgram = "terrain";
    material.cullMode = CullMode::Back;
    material.depthWrite = true;
    material.depthTest = true;
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
    for (uint32_t i = 0; i < tc->layerCount; ++i) {
        tc->layerTextureHandles[i] = renderer->loadTexture("terrain_layer_06_tex_" + std::to_string(i), terrain->layers[i].texturePath);
        if (i > 0 && !terrain->layers[i].maskPath.empty()) {
            tc->layerMaskHandles[i] = renderer->loadTexture("terrain_layer_06_mask_" + std::to_string(i), terrain->layers[i].maskPath);
        }
    }
    tc->heightmapTextureHandle = renderer->loadTextureFromMemory("terrain_heightmap", terrain->getHeightmapData(),
                                                                 terrain->getHeightmapSize(), terrain->getHeightmapSize());
    tc->heightmapEnabled = true;
    return tc;
}

// Nowy teren wybranym algorytmem: przebudowa siatki, mapy wysokości na GPU i mgły w dolinach.
void regenerateTerrain(Engine& engine, Terrain& terrain, TerrainComponent& tc, VolumetricFogRenderer& fog,
                       TerrainGenerator generator) {
    switch (generator) {
        case TerrainGenerator::Plasma:
            // Plazma (midpoint displacement) - fraktalny szum tworzący naturalne góry.
            terrain.generatePlasma(256, 1.2f);
            terrain.setScale(15.0f, 7.0f, 20.0f, 6);
            break;
        case TerrainGenerator::Fault:
            // Uskoki (fault formation) - losowe linie przecinają teren i podnoszą jedną stronę:
            // strome zbocza i doliny.
            terrain.generateFault(256, 160, 100, 1, 5, 0.5f);
            terrain.setScale(15.0f, 2.0f, 20.0f, 6);
            break;
        case TerrainGenerator::Deposition:
            // Osypywanie (particle deposition) - cząstki ziemi spływają ze wzniesień i wygładzają szczyty.
            terrain.generateDeposition(256, 100, 30, 10, 20, 0.8f);
            terrain.setScale(15.0f, 7.0f, 20.0f, 6);
            break;
    }
    terrain.smoothEdges();
    terrain.buildMesh();
    terrain.uploadTo(*engine.getMeshManager());
    tc.heightmapTextureHandle = engine.getRenderer()->loadTextureFromMemory(
        "terrain_heightmap", terrain.getHeightmapData(), terrain.getHeightmapSize(), terrain.getHeightmapSize());
    tc.layerWorldSize = terrain.worldSizeXZ();
    tc.getOwner()->transform().position = terrain.worldOffset();
    fog.setLocalVolumes(findValleyFogVolumes(terrain, kWaterLevel));
    LOG_INFO("Nowy teren: {}", generatorName(generator));
}

// Kamera nad powierzchnią terenu (wysokość interpolowana z siatki + 3 jednostki "oczu").
void snapCameraToGround(Engine& engine, const Terrain& terrain) {
    CameraComponent* camera = engine.getSceneManager()->activeScene()->activeCamera();
    if (!camera) return;
    Vec3& p = camera->getOwner()->transform().position;
    p.y = terrain.getHeight(p.x, p.z) + 3.0f;
}

// Kamera startuje nad środkiem mapy, 40 jednostek nad terenem (stała wysokość mogła wypaść wewnątrz góry).
void setupCamera(Engine& engine, Scene* scene, const Terrain& terrain) {
    auto* camera = scene->createEntity("main_camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = {0.0f, std::max(120.0f, terrain.getHeight(0.0f, 0.0f) + 40.0f), 0.0f};
    // Patrzymy przed siebie i lekko w dół (na krajobraz, nie pionowo pod nogi).
    const Vec3 start = camera->getOwner()->transform().position;
    camera->target = {start.x + 100.0f, start.y - 25.0f, start.z + 100.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 18000.0f;
    scene->setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(camera->getOwner()->transform().position, camera->target);
}

// Klawisze teselacji: T - włącz/wyłącz, +/- - poziom (0..32, krok 2).
void handleTessellationKeys(Engine& e) {
    if (e.keyPressed(SDL_SCANCODE_T)) {
        const bool enabled = !e.isTessellationEnabled();
        e.setTessellationEnabled(enabled);
        LOG_INFO("Teselacja terenu: {}", enabled ? "WŁĄCZONA" : "WYŁĄCZONA");
    }
    float level = e.getTessellationLevel();
    if (e.keyPressed(SDL_SCANCODE_EQUALS)) level = std::min(level + 2.0f, 32.0f);
    if (e.keyPressed(SDL_SCANCODE_MINUS)) level = std::max(level - 2.0f, 0.0f);
    if (level != e.getTessellationLevel()) {
        e.setTessellationLevel(level);
        LOG_INFO("Poziom teselacji: {}", level);
    }
}

void drawHud(Engine& e, const Terrain& terrain, TerrainGenerator generator, bool showDebug) {
    const std::array<float, 4> lightBlue{0.7f, 0.7f, 1.0f, 1.0f};
    e.setTextLabel("FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())), "times", 10, 20, 0.7f, {1, 1, 0.3f, 1});
    e.setTextLabel("1 - plazma, 2 - uskoki, 3 - osypywanie | B - debug | T - teselacja | +/- - poziom", "times", 10, 50,
                   0.45f, {0.5f, 0.9f, 0.5f, 1.0f});

    if (showDebug) {
        // Liczba widocznych węzłów na każdym poziomie LOD.
        std::array<int, 6> lodCounts{};
        for (const auto* node : terrain.getVisibleNodes()) lodCounts[std::clamp(node->lodLevel, 0, 5)]++;
        std::ostringstream info;
        info << "[" << generatorName(generator) << "]  węzłów: " << terrain.getTotalLeafCount()
             << "  widocznych: " << terrain.visibleCount << " (";
        for (int lod = 0; lod < 6; ++lod) info << (lod ? " " : "") << "LOD" << lod << ":" << lodCounts[lod];
        info << ") | teselacja: ";
        if (!e.isTessellationSupported()) {
            info << "BRAK";
        } else {
            info << (e.isTessellationEnabled() ? "WŁ" : "WYŁ") << " (poziom " << std::fixed << std::setprecision(1)
                 << e.getTessellationLevel() << ")";
        }
        e.setTextLabel(info.str(), "times", 10, 72, 0.45f, lightBlue);
    }

    CameraComponent* camera = e.getSceneManager()->activeScene()->activeCamera();
    if (!camera) return;
    const Vec3 p = camera->getOwner()->transform().position;
    std::ostringstream pos;
    pos << "pos: " << std::fixed << std::setprecision(0) << p.x << " " << p.y << " " << p.z << "  h: "
        << std::setprecision(1) << terrain.getHeight(p.x, p.z);
    e.setTextLabel(pos.str(), "times", 10, 95, 0.5f, lightBlue);
}

} // namespace

int main() {
    Engine engine;
    if (!engine.initialize("engine3d: terrain LOD", 1280, 800)) return 1;
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
    const std::shared_ptr<Terrain> terrain = loadOrGenerateTerrain(engine);
    TerrainComponent* terrainComponent = createTerrainEntity(engine, scene, terrain);

    auto valleyFog = std::make_shared<VolumetricFogRenderer>();
    valleyFog->configure("bin/assets/shaders/fog");
    valleyFog->setFogRadius(2200.0f);
    valleyFog->setLocalVolumes(findValleyFogVolumes(*terrain, kWaterLevel));
    valleyFog->setEnabled(true);
    engine.getRenderer()->addFeature(valleyFog);

    setupCamera(engine, scene, *terrain);
    engine.enableShaderHotReload(true);

    bool cursorGrabbed = true;
    bool showDebug = true;
    TerrainGenerator generator = TerrainGenerator::Plasma;
    Weather weather;
    float animTime = 0.0f;

    engine.onUpdate = [&](float dt, Engine& e) {
        if (e.keyPressed(SDL_SCANCODE_ESCAPE)) e.requestQuit();

        animTime += dt;
        skydome->setTime(animTime);
        ocean->setTime(animTime);
        weather.update(*skydome, dt);

        FlyCamera::update(e, dt, 150.0f);
        if (e.actionDown(kActionSnapToGround)) snapCameraToGround(e, *terrain);

        if (e.keyPressed(SDL_SCANCODE_B)) {
            showDebug = !showDebug;
            LOG_INFO("Napisy debug terenu: {}", showDebug);
        }
        handleTessellationKeys(e);

        // Klawisze 1/2/3: nowy teren wybranym algorytmem, kamera ląduje na jego powierzchni.
        const bool regenerate = e.actionPressed(kActionTerrainModePlasma) || e.actionPressed(kActionTerrainModeFault) ||
                                e.actionPressed(kActionTerrainModeDeposition);
        if (e.actionPressed(kActionTerrainModePlasma)) generator = TerrainGenerator::Plasma;
        if (e.actionPressed(kActionTerrainModeFault)) generator = TerrainGenerator::Fault;
        if (e.actionPressed(kActionTerrainModeDeposition)) generator = TerrainGenerator::Deposition;
        if (regenerate) {
            regenerateTerrain(e, *terrain, *terrainComponent, *valleyFog, generator);
            snapCameraToGround(e, *terrain);
        }

        if (e.actionPressed(kActionTogglePointer)) {
            cursorGrabbed = !cursorGrabbed;
            e.getPlatform()->grabPointer(cursorGrabbed);
        }
        drawHud(e, *terrain, generator, showDebug);
    };

    engine.run();
    return 0;
}
