/**
 * 04_particles - cząsteczki i animowane sprite'y nad oceanem, pod kopułą nieba z szybkim cyklem dnia.
 *
 * System cząsteczek (feature/particle) czyta emitery z pliku .particle (albo konfiguruje fontannę w kodzie)
 * i rysuje wszystkie cząsteczki jednym poleceniem (instancing). Sprite'y to płaskie obrazki zwrócone zawsze
 * do kamery (billboarding): jeden z atlasem klatek, drugi z osobnymi plikami klatek.
 * Sterowanie: WASD + mysz, spacja / lewy Ctrl - góra / dół, Esc - wyjście.
 */
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "camera/camera_component.h"
#include "camera/fly_camera.h"
#include "core/engine.h"
#include "core/entity.h"
#include "core/logger.h"
#include "core/mesh_component.h"
#include "feature/particle/system.h"
#include "feature/sprite/sprite.h"
#include "object/grid_renderer.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"
#include "render/renderer.h"

using namespace engine3d;

namespace {

void createFloor(Engine& engine, Scene* scene) {
    MeshData floor;
    floor.vertices = {
        {{{-10, -10, -10}}, {{0, 1, 0}}, {{0.3f, 0.3f, 0.3f}}, {{0, 0}}},
        {{{ 10, -10, -10}}, {{0, 1, 0}}, {{0.3f, 0.3f, 0.3f}}, {{10, 0}}},
        {{{ 10, -10,  10}}, {{0, 1, 0}}, {{0.3f, 0.3f, 0.3f}}, {{10, 10}}},
        {{{-10, -10,  10}}, {{0, 1, 0}}, {{0.3f, 0.3f, 0.3f}}, {{0, 10}}},
    };
    floor.indices = {0, 2, 1, 0, 3, 2};
    engine.getMeshManager()->loadMesh("floor", floor);
    Material material;
    material.name = "floor";
    scene->createEntity("floor")->addComponent<MeshComponent>("floor")->materialHandle =
        engine.getMaterialManager()->add(material);
}

// Siatka pomocnicza (linie co 5 jednostek) - ułatwia ocenę odległości.
void addGrid(Renderer* renderer) {
    auto grid = std::make_shared<GridRenderer>();
    GridConfig cfg;
    cfg.enabled = true;
    cfg.size = 200;
    cfg.spacing = 5;
    grid->configure(cfg, "bin/assets/shaders/grid");
    grid->setEnabled(true);
    renderer->addFeature(grid);
}

// Ocean: tafla wody z falami liczonymi na GPU.
std::shared_ptr<OceanRenderer> addOcean(Renderer* renderer) {
    auto ocean = std::make_shared<OceanRenderer>();
    OceanConfig cfg;
    cfg.enabled = true;
    cfg.waterLevel = -8.0f;
    cfg.waveAmplitude = 1.0f;
    cfg.waveSpeed = 1.0f;
    cfg.waveFrequency = 1.0f;
    cfg.foamThreshold = 0.5f;
    ocean->configure(cfg, "bin/assets/shaders/ocean", "ocean");
    ocean->setEnabled(true);
    renderer->addFeature(ocean);
    return ocean;
}

// Kopuła nieba: gradient zależny od pory dnia i położenia słońca, dwie warstwy chmur.
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

// Emitery z pliku .particle; bez pliku - fontanna ognia skonfigurowana w kodzie.
void setupParticles(Engine& engine, Scene* scene, particles::ParticleSystem& particles) {
    if (particles.load("assets/textures/particles/przyklad.particle")) {
        LOG_INFO("Wczytano plik .particle: {} emiterów", particles.emitterCount());
        for (int i = 0; i < particles.emitterCount(); i++) particles.getEmitter(i)->createParticles();
    } else {
        LOG_INFO("Brak pliku .particle - fontanna skonfigurowana w kodzie");
        auto* emitter = particles.addEmitter("fountain");
        emitter->setNumParticles(200);
        emitter->setEmitRate(30);                                                // nowych cząsteczek na sekundę
        emitter->setGravity({0.0f, -2.0f, 0.0f});
        emitter->setPosition({-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f});          // obszar narodzin
        emitter->setVelocity({-2.0f, 4.0f, -2.0f}, {2.0f, 8.0f, 2.0f});          // losowa prędkość początkowa
        emitter->setLifeTime(1.5f, 3.0f);                                        // czas życia w sekundach
        emitter->setSize(0.2f, 0.5f);
        emitter->setMass(0.5f, 1.0f);                                            // wpływ grawitacji
        emitter->setColor({1.0f, 0.4f, 0.1f, 1.0f}, {1.0f, 0.8f, 0.2f, 1.0f});   // od pomarańczu do żółci
        emitter->createParticles();
    }
    particles.initialize(*scene, *engine.getMeshManager(), *engine.getMaterialManager(), *engine.getMaterialSystem(),
                         *engine.getRenderer(), *engine.getAssetRegistry(),
                         &engine.getJobSystem());   // ruch cząsteczek na puli wątków silnika
}

struct SpriteIds {
    int firen = -1;     // animacja z atlasu
    int explosion = -1; // animacja z osobnych plików
};

SpriteIds setupSprites(Engine& engine, Scene* scene, SpriteSystem& sprites) {
    Renderer* renderer = engine.getRenderer();
    SpriteIds ids;

    // Sprite 1: atlas 10 x 7 klatek, 0.3 s na klatkę, pętla.
    SpriteSystem::Sprite firen;
    firen.position = {-23.0f, 7.0f, 9.0f};
    firen.size = 2.0f;
    firen.color = {1.0f, 1.0f, 1.0f, 1.0f};
    firen.textureHandle = renderer->loadTexture("sprite_firen", "assets/sprite/firen.tga");
    ids.firen = sprites.addSprite(firen);
    sprites.setAnim(ids.firen, 10, 7, 0.3f, 0, -1, true);

    // Sprite 2: 8 osobnych tekstur (isSequence), 0.1 s na klatkę.
    SpriteSystem::Sprite explosion;
    explosion.position = {-16.0f, 7.0f, 13.0f};
    explosion.size = 3.0f;
    explosion.color = {1.0f, 1.0f, 1.0f, 1.0f};
    explosion.isSequence = true;
    for (int f = 1; f <= 8; f++) {
        explosion.frameTextureHandles.push_back(renderer->loadTexture(
            "sprite_wybuch" + std::to_string(f), "assets/sprite/wybuch/wybuch" + std::to_string(f) + ".tga"));
    }
    ids.explosion = sprites.addSprite(explosion);
    sprites.setAnim(ids.explosion, 0, 0, 0.10f, 0, 7, true);

    sprites.initialize(*scene, *engine.getMeshManager(), *engine.getMaterialManager(), *engine.getMaterialSystem(),
                       *engine.getAssetRegistry());
    return ids;
}

// Szybki cykl dnia (0.21 h na sekundę) i zachmurzenie wahające się od 0 do 1.
struct Weather {
    float timeOfDay = 10.0f;
    float cloudCoverage = 0.4f;
    float cloudDirection = 1.0f;

    void update(SkydomeRenderer& skydome, float dt) {
        timeOfDay += dt * 0.21f;
        if (timeOfDay > 24.0f) timeOfDay -= 24.0f;
        cloudCoverage += dt * 0.02f * cloudDirection;
        if (cloudCoverage >= 1.0f) { cloudCoverage = 1.0f; cloudDirection = -1.0f; }
        else if (cloudCoverage <= 0.0f) { cloudCoverage = 0.0f; cloudDirection = 1.0f; }

        // Słońce od 6:00 do 18:00: wschodzi na wschodzie (azymut 90), zachodzi na zachodzie (270),
        // w południe maks. 65 stopni nad horyzontem.
        const float dayProgress = std::clamp((timeOfDay - 6.0f) / 12.0f, 0.0f, 1.0f);
        auto cfg = skydome.getConfig();
        cfg.timeOfDay = timeOfDay;
        cfg.cloudCoverage = cloudCoverage;
        cfg.sunElevation = std::sin(dayProgress * PI) * 65.0f;
        cfg.sunAzimuth = 90.0f + dayProgress * 180.0f;
        skydome.setConfig(cfg);
    }
};

void setupCamera(Engine& engine, Scene* scene) {
    auto* camera = scene->createEntity("main_camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = {0.0f, 5.0f, -15.0f};
    camera->target = {0.0f, 0.5f, 0.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 16000.0f;
    scene->setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(camera->getOwner()->transform().position, camera->target);
}

void drawHud(Engine& e, particles::ParticleSystem& particles) {
    const std::array<float, 4> lightBlue{0.7f, 0.7f, 1.0f, 1.0f};
    e.setTextLabel("FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())), "times",
                   e.getRenderer()->width() - 200.0f, 30.0f, 1.0f, {1.0f, 1.0f, 0.2f, 1.0f});
    // Liczba żywych cząsteczek każdego emitera.
    for (int i = 0; i < particles.emitterCount(); i++) {
        const std::string info = particles.infoParticles(i);
        if (!info.empty()) e.setTextLabel(info, "times", 10.0f, 95.0f + i * 25.0f, 0.7f, {0.2f, 1.0f, 0.2f, 1.0f});
    }

    CameraComponent* camera = e.getSceneManager()->activeScene()->activeCamera();
    if (!camera) return;
    const Vec3 p = camera->getOwner()->transform().position;
    const Vec3 dir = normalize(subtract(camera->target, p));
    std::ostringstream pos, look;
    pos << "pos: " << std::fixed << std::setprecision(0) << p.x << " " << p.y << " " << p.z;
    look << "dir: " << std::fixed << std::setprecision(2) << dir.x << " " << dir.y << " " << dir.z;
    e.setTextLabel(pos.str(), "times", 10.0f, 30.0f, 0.7f, lightBlue);
    e.setTextLabel(look.str(), "times", 10.0f, 55.0f, 0.7f, lightBlue);
}

} // namespace

int main() {
    std::srand(42);   // stałe ziarno - powtarzalne efekty cząsteczkowe
    Engine engine;
    if (!engine.initialize("engine3d: particles", 1024, 768)) return 1;
    Scene* scene = engine.getSceneManager()->activeScene();
    if (!scene) return 1;

    engine.getRenderer()->setClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    engine.loadFont("times", "assets/fonts/times.ttf", 48);
    FlyCamera::bindDefaultKeys(engine.inputActions());

    createFloor(engine, scene);
    addGrid(engine.getRenderer());
    auto ocean = addOcean(engine.getRenderer());
    auto skydome = addSkydome(engine.getRenderer());

    particles::ParticleSystem particles;
    setupParticles(engine, scene, particles);
    SpriteSystem sprites;
    const SpriteIds spriteIds = setupSprites(engine, scene, sprites);
    setupCamera(engine, scene);

    Weather weather;
    float animTime = 0.0f;

    engine.onUpdate = [&](float dt, Engine& e) {
        if (e.keyPressed(SDL_SCANCODE_ESCAPE)) e.requestQuit();

        // Czas animacji w shaderach nieba i oceanu (ruch chmur, fale).
        animTime += dt;
        skydome->setTime(animTime);
        ocean->setTime(animTime);
        weather.update(*skydome, dt);

        FlyCamera::update(e, dt, 10.0f);

        // Cząsteczki: ruch (prędkość, grawitacja); pozycja kamery do zwracania ich przodem do ekranu.
        particles.update(dt);
        if (CameraComponent* camera = scene->activeCamera()) {
            particles.setCameraPosition(camera->getOwner()->transform().position);
        }

        // Sprite'y krążą po elipsie.
        const float c = std::cos(animTime * 0.6f);
        const float s = std::sin(animTime * 0.6f);
        sprites.setPosition(spriteIds.firen, {-2.5f + c * 1.5f, 2.5f + s * 0.8f, 0.0f});
        sprites.setPosition(spriteIds.explosion, {3.0f + c * 1.5f, 2.5f + s * 0.8f, 0.0f});
        sprites.update(dt);

        drawHud(e, particles);
    };

    engine.run();
    return 0;
}
