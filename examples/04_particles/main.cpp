#include "core/logger.h"
#include <SDL3/SDL.h>

#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include "core/engine.h"
#include "core/entity.h"
#include "render/renderer.h"
#include "core/types.h"
#include "core/math.h"
#include "core/mesh_component.h"
#include "camera/camera_component.h"
#include "feature/particle/system.h"
#include "feature/sprite/sprite.h"
#include "object/grid_renderer.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"

using namespace engine3d;

int main() {
    std::srand(42); // Ustawienie stałego ziarna losowości dla powtarzalnych efektów cząsteczkowych
    Engine engine;

    if (!engine.initialize("engine3d: particles", 1024, 768)) {
        return 1;
    }

    auto* scene = engine.getSceneManager()->activeScene();
    if (!scene) {
        return 1;
    }

    auto* renderer = engine.getRenderer();
    renderer->setClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    engine.loadFont("times", "assets/fonts/times.ttf", 48);

    // KROK 1: UTWORZENIE PŁASZCZYZNY DLA SCENY
    MeshData floor;
    floor.vertices = {
        {{{-10,-10,-10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{0,0}}},
        {{{ 10,-10,-10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{10,0}}},
        {{{ 10,-10, 10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{10,10}}},
        {{{-10,-10, 10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{0,10}}},
    };
    floor.indices = {0,2,1,0,3,2};
    engine.getMeshManager()->loadMesh("floor", floor);
    Material mat; mat.name = "floor";
    const auto floorMaterialHandle = engine.getMaterialManager()->add(mat);
    auto* entity = scene->createEntity("floor");
    auto* floorMesh = entity->addComponent<MeshComponent>("floor");
    floorMesh->materialHandle = floorMaterialHandle;

    // KROK 2: DODANIE SIATKI KOORDYNACYJNEJ (Grid Renderer)
    auto grid = std::make_shared<GridRenderer>();
    GridConfig gridCfg;
    gridCfg.enabled = true;
    gridCfg.size = 200;
    gridCfg.spacing = 5;
    grid->configure(gridCfg, "bin/assets/shaders/grid");
    grid->setEnabled(true);
    renderer->addFeature(grid);

    // KROK 3: UTWORZENIE SZUMIĄCEGO OCEANU (Ocean Renderer)
    // Renderuje nieskończoną taflę wody animowaną funkcjami Gerstnera lub szumem na GPU.
    auto ocean = std::make_shared<OceanRenderer>();
    OceanConfig oceanCfg;
    oceanCfg.enabled = true;
    oceanCfg.waterLevel = -8.0f;
    oceanCfg.waveAmplitude = 1.0f;
    oceanCfg.waveSpeed = 1.0f;
    oceanCfg.waveFrequency = 1.0f;
    oceanCfg.foamThreshold = 0.5f;
    ocean->configure(oceanCfg, "bin/assets/shaders/ocean", "ocean");
    ocean->setEnabled(true);
    renderer->addFeature(ocean);

    // KROK 4: RENDERER KOPUŁY NIEBA (Skydome Renderer)
    // Generuje gradienty nieba zależnie od pory dnia, położenia słońca oraz dwie dynamiczne warstwy chmur.
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

    // KROK 5: INICJALIZACJA SYSTEMU CZĄSTECZEK (Particle System)
    // Cząsteczki rysowane są za pomocą Instanced Rendering w Vulkanie (wszystkie cząsteczki to jeden draw call).
    particles::ParticleSystem particleSys;
    if (!particleSys.load("assets/textures/particles/przyklad.particle")) {
        // Jeśli brak pliku konfiguracyjnego, konfigurujemy emiter z poziomu kodu (np. efekt fontanny ognia)
        LOG_INFO("No .particle file found, using hardcoded setup");
        auto* emitter = particleSys.addEmitter("fountain");
        emitter->setNumParticles(200);
        emitter->setEmitRate(30);
        emitter->setGravity({0.0f, -2.0f, 0.0f}); // Grawitacja spychająca cząsteczki w dół
        emitter->setPosition({-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f}); // Obszar spawnowania
        emitter->setVelocity({-2.0f, 4.0f, -2.0f}, {2.0f, 8.0f, 2.0f}); // Losowy rozrzut prędkości początkowej
        emitter->setLifeTime(1.5f, 3.0f); // Losowy czas życia cząsteczki (w sekundach)
        emitter->setSize(0.2f, 0.5f);     // Skalowanie wielkości
        emitter->setMass(0.5f, 1.0f);     // Masa wpływająca na siłę grawitacji
        emitter->setColor({1.0f, 0.4f, 0.1f, 1.0f}, {1.0f, 0.8f, 0.2f, 1.0f}); // Kolor od pomarańczu do żółtego
        emitter->createParticles();
    } else {
        LOG_INFO("Loaded .particle file with {} emitters", particleSys.emitterCount());
        for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
            particleSys.getEmitter(ei)->createParticles();
        }
    }

    particleSys.initialize(*scene,
                           *engine.getMeshManager(),
                           *engine.getMaterialManager(),
                           *engine.getMaterialSystem(),
                           *renderer,
                           *engine.getAssetRegistry(),
                           &engine.getJobSystem());   // ruch cząsteczek na puli wątków silnika

    // Wczytanie sprite'ów do animacji 2D
    const auto firenTextureHandle = renderer->loadTexture("sprite_firen", "assets/sprite/firen.tga");

    // Zmienne do symulacji cyklu dobowego i pogody
    float timeOfDay = 10.0f;
    float cloudCov = 0.4f;
    int cloudDir = 1;

    const float dayNightSpeed = 0.21f; // Prędkość upływu czasu (godziny na sekundę)
    const float weatherSpeed  = 0.02f;

    // KROK 6: KONFIGURACJA SPRITE'ÓW (Sprite System)
    // Sprite'y to płaskie 2D obrazy umieszczone w przestrzeni 3D, zorientowane przodem do kamery (billboarding).
    SpriteSystem spriteSys;
    
    // Konfiguracja Sprite 1: Animacja z pojedynczego atlasu tekstur (Sprite Sheet)
    SpriteSystem::Sprite firenSprite;
    firenSprite.position = {-23.0f, 7.0f, 9.0f};
    firenSprite.size = 2.0f;
    firenSprite.color = {1.0f, 1.0f, 1.0f, 1.0f};
    firenSprite.textureHandle = firenTextureHandle;
    int firenId = spriteSys.addSprite(firenSprite);
    // Ustawienie animacji: siatka 10x7 klatek, czas klatki 0.3s, start od klatki 0, pętla nieskończona
    spriteSys.setAnim(firenId, 10, 7, 0.3f, 0, -1, true);

    // Konfiguracja Sprite 2: Animacja klatkowa z osobnych plików tekstur (Frame-by-frame loading)
    SpriteSystem::Sprite wybuchSprite;
    wybuchSprite.position = {-16.0f, 7.0f, 13.0f};
    wybuchSprite.size = 3.0f;
    wybuchSprite.color = {1.0f, 1.0f, 1.0f, 1.0f};
    wybuchSprite.isSequence = true; // Flaga określająca sekwencję osobnych tekstur zamiast atlasu
    for (int f = 1; f <= 8; f++) {
        std::string texName = "sprite_wybuch" + std::to_string(f);
        std::string path = "assets/sprite/wybuch/wybuch" + std::to_string(f) + ".tga";
        const auto textureHandle = renderer->loadTexture(texName, path);
        wybuchSprite.frameTextureHandles.push_back(textureHandle);
    }
    int wybuchId = spriteSys.addSprite(wybuchSprite);
    spriteSys.setAnim(wybuchId, 0, 0, 0.10f, 0, 7, true);

    spriteSys.initialize(*scene,
                         *engine.getMeshManager(),
                         *engine.getMaterialManager(),
                         *engine.getMaterialSystem(),
                         *engine.getAssetRegistry());

    // Konfiguracja kamery FPP
    auto* camera = scene->createEntity("main_camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = {0.0f, 5.0f, -15.0f};
    camera->target = {0.0f, 0.5f, 0.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 16000.0f;
    scene->setActiveCamera(camera);

    engine.getCameraController()->setPositionAndTarget(camera->getOwner()->transform().position, camera->target);

    // PĘTLA UPDATE
    engine.onUpdate = [&particleSys, &spriteSys, firenId, wybuchId, &timeOfDay, &cloudCov, &cloudDir, &skydomeCfg, &oceanCfg, &engine, &skydome, &ocean, dayNightSpeed, weatherSpeed](float dt, Engine& e) {
        static float animTime = 0.0f;
        animTime += dt;
        
        // Zaktualizowanie czasu w shaderach kopuły nieba i oceanu (falowanie, ruch chmur)
        skydome->setTime(animTime);
        ocean->setTime(animTime);
        
        const auto& snap = e.input();
        static bool escWasDown = false;
        const bool escDown = snap.keys[SDL_SCANCODE_ESCAPE];
        if (escDown && !escWasDown) {
            SDL_Event event{};
            event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&event);
        }
        escWasDown = escDown;

        auto* cc = e.getCameraController();
        auto* activeScene = e.getSceneManager()->activeScene();
        auto* cam = activeScene ? activeScene->activeCamera() : nullptr;
        if (cc && cam) {
            cc->moveSpeed = 10.0f;
            float ts = cc->mouseSensitivity;
            cc->rotateYaw(-static_cast<float>(snap.mouseDeltaX) * ts);
            cc->rotatePitch(-static_cast<float>(snap.mouseDeltaY) * ts);
            float vel = cc->moveSpeed * dt;
            if (snap.keys[SDL_SCANCODE_W]) cc->moveForward(vel);
            if (snap.keys[SDL_SCANCODE_S]) cc->moveForward(-vel);
            if (snap.keys[SDL_SCANCODE_A]) cc->moveRight(-vel);
            if (snap.keys[SDL_SCANCODE_D]) cc->moveRight(vel);
            cc->update(dt, *cam);
        }

        // KROK 7: AKTUALIZACJA I BILLBOARDOWANIE CZĄSTECZEK
        // System cząsteczek przemieszcza je zgodnie z fizyką (prędkość, grawitacja).
        // Przekazujemy aktualną pozycję kamery, aby cząsteczki mogły zorientować się
        // przodem do ekranu (Billboarding wykonany w shaderze wierzchołków).
        particleSys.update(dt);
        if (cam) {
            particleSys.setCameraPosition(cam->getOwner()->transform().position);
        }

        // KROK 8: SYMULACJA DOBOSTRADY (Day/Night cycle)
        {
            static float timeAcc = 0.0f;
            timeAcc += dt;

            // Przesunięcie pory dnia
            timeOfDay += dt * dayNightSpeed;
            if (timeOfDay > 24.0f) timeOfDay -= 24.0f;

            // Zmiana zachmurzenia
            cloudCov += dt * weatherSpeed * cloudDir;
            if (cloudCov >= 1.0f) { cloudCov = 1.0f; cloudDir = -1; }
            else if (cloudCov <= 0.0f) { cloudCov = 0.0f; cloudDir = 1; }

            // Obliczenie wysokości (Elevation) i azymutu (Azimuth) słońca na niebie
            float dayProgress = (timeOfDay - 6.0f) / 12.0f; // Postęp dnia w godzinach 6:00 - 18:00
            dayProgress = std::max(0.0f, std::min(1.0f, dayProgress));
            float sunEl = std::sin(dayProgress * PI) * 65.0f; // Słońce wschodzi na max 65 stopni
            float sunAz = 90.0f + dayProgress * 180.0f;                  // Ruch ze wschodu (90) na zachód (270)

            // Re-konfiguracja parametrów kopuły nieba
            skydomeCfg.timeOfDay = timeOfDay;
            skydomeCfg.cloudCoverage = cloudCov;
            skydomeCfg.sunElevation = sunEl;
            skydomeCfg.sunAzimuth = sunAz;
            skydome->configure(skydomeCfg, "bin/assets/shaders/skydome", "skydome");

            // Ruch okrężny sprite'ów w przestrzeni 3D przy użyciu funkcji trygonometrycznych
            float sinVal = std::sin(timeAcc * 0.6f);
            float cosVal = std::cos(timeAcc * 0.6f);

            spriteSys.setPosition(firenId, {-2.5f + cosVal * 1.5f, 2.5f + sinVal * 0.8f, 0.0f});
            spriteSys.setPosition(wybuchId, {3.0f + cosVal * 1.5f, 2.5f + sinVal * 0.8f, 0.0f});

            // Aktualizacja animacji sprite'ów
            spriteSys.update(dt);
        }

        // HUD i teksty
        std::ostringstream oss;
        oss << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.setTextLabel(oss.str(), "times",
                                       e.getRenderer()->width() - 200.0f, 30.0f, 1.0f,
                                       {1.0f, 1.0f, 0.2f, 1.0f});

        // Wypisanie informacji o liczbie żywych cząsteczek na każdym emiterze
        for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
            std::string info = particleSys.infoParticles(ei);
            if (!info.empty()) {
                e.setTextLabel(info, "times", 10.0f, 95.0f + ei * 25.0f, 0.7f,
                               {0.2f, 1.0f, 0.2f, 1.0f});
            }
        }

        if (cam) {
            std::ostringstream pos;
            pos << "pos: " << std::fixed << std::setprecision(0)
                << cam->getOwner()->transform().position.x << " " << cam->getOwner()->transform().position.y << " " << cam->getOwner()->transform().position.z;
            e.setTextLabel(pos.str(), "times", 10.0f, 30.0f, 0.7f,
                                          {0.7f, 0.7f, 1.0f, 1.0f});

            Vec3 dir = normalize(subtract(cam->target, cam->getOwner()->transform().position));
            std::ostringstream look;
            look << "dir: " << std::fixed << std::setprecision(2)
                 << dir.x << " " << dir.y << " " << dir.z;
            e.setTextLabel(look.str(), "times", 10.0f, 55.0f, 0.7f,
                                          {0.7f, 0.7f, 1.0f, 1.0f});
        }
    };

    engine.run();
    return 0;
}
