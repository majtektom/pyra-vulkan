#include "engine.h"
#include "renderer.h"
#include "types.h"
#include "math.h"
#include "mesh_component.h"
#include "camera_component.h"
#include "particle_system.h"
#include "sprite.h"
#include "grid_renderer.h"
#include "ocean_renderer.h"
#include "skydome_renderer.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace engine3d;

int main() {
    std::srand(42);
    Engine engine;

    if (!engine.initialize("engine3d: particles", 1024, 768)) {
        return 1;
    }

    auto* renderer = engine.getRenderer();
    renderer->setClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    engine.loadFont("times", "assets/fonts/times.ttf", 48);

    MeshData floor;
    floor.vertices = {
        {{{-10,-10,-10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{0,0}}},
        {{{ 10,-10,-10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{10,0}}},
        {{{ 10,-10, 10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{10,10}}},
        {{{-10,-10, 10}}, {{0,1,0}}, {{0.3f,0.3f,0.3f}}, {{0,10}}},
    };
    floor.indices = {0,2,1,0,3,2};
    engine.getMeshManager()->loadMesh("floor", floor);
    Material mat; mat.name = "floor"; engine.getMaterialManager()->add(mat);
    auto* entity = engine.createEntity("floor");
    entity->addComponent<MeshComponent>("floor")->materialInstance =
        engine.getMaterialSystem()->getOrCreateMaterialInstance(mat);

    
    auto grid = std::make_shared<GridRenderer>();
    GridConfig gridCfg;
    gridCfg.enabled = true;
    gridCfg.size = 200;
    gridCfg.spacing = 5;
    grid->configure(gridCfg, "bin/assets/shaders/grid");
    grid->setEnabled(true);
    renderer->addFeature(grid);

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

    CParticleSystem particleSys;
    if (!particleSys.load("assets/textures/particles/przyklad.particle")) {
        std::cout << "No .particle file found, using hardcoded setup\n";
        auto* emitter = particleSys.addEmitter("fountain");
        emitter->setNumParticles(200);
        emitter->setEmitRate(30);
        emitter->setGravity({0.0f, -2.0f, 0.0f});
        emitter->setPosition({-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f});
        emitter->setVelocity({-2.0f, 4.0f, -2.0f}, {2.0f, 8.0f, 2.0f});
        emitter->setLifeTime(1.5f, 3.0f);
        emitter->setSize(0.2f, 0.5f);
        emitter->setMass(0.5f, 1.0f);
        emitter->setColor({1.0f, 0.4f, 0.1f, 1.0f}, {1.0f, 0.8f, 0.2f, 1.0f});
        emitter->createParticles();
    } else {
        std::cout << "Loaded .particle file with " << particleSys.emitterCount() << " emitters\n";
        for (int ei = 0; ei < particleSys.emitterCount(); ei++) {
            particleSys.getEmitter(ei)->createParticles();
        }
    }

    particleSys.initialize(engine);

    renderer->loadTexture("sprite_firen", "assets/sprite/firen.tga");

    float timeOfDay = 10.0f;
    float cloudCov = 0.4f;
    int cloudDir = 1;

    const float dayNightSpeed = 0.21f;
    const float weatherSpeed  = 0.02f;

    SpriteSystem spriteSys;
    SpriteSystem::Sprite firenSprite;
    firenSprite.position = {-23.0f, 7.0f, 9.0f};
    firenSprite.size = 2.0f;
    firenSprite.color = {1.0f, 1.0f, 1.0f, 1.0f};
    firenSprite.textureName = "sprite_firen";
    int firenId = spriteSys.addSprite(firenSprite);
    spriteSys.setAnim(firenId, 10, 7, 0.3f, 0, -1, true);

    SpriteSystem::Sprite wybuchSprite;
    wybuchSprite.position = {-16.0f, 7.0f, 13.0f};
    wybuchSprite.size = 3.0f;
    wybuchSprite.color = {1.0f, 1.0f, 1.0f, 1.0f};
    wybuchSprite.isSequence = true;
    wybuchSprite.textureName = "sprite_wybuch1";
    for (int f = 1; f <= 8; f++) {
        std::string texName = "sprite_wybuch" + std::to_string(f);
        std::string path = "assets/sprite/wybuch/wybuch" + std::to_string(f) + ".tga";
        renderer->loadTexture(texName, path);
        wybuchSprite.frameTextures.push_back(texName);
    }
    int wybuchId = spriteSys.addSprite(wybuchSprite);
    spriteSys.setAnim(wybuchId, 0, 0, 0.10f, 0, 7, true);

    spriteSys.initialize(engine);

    auto* camera = engine.createEntity("main_camera")->addComponent<CameraComponent>();
    camera->position = {0.0f, 5.0f, -15.0f};
    camera->target = {0.0f, 0.5f, 0.0f};
    camera->fieldOfViewDegrees = 60.0f;
    camera->nearPlane = 0.5f;
    camera->farPlane = 16000.0f;
    engine.setActiveCamera(camera);

    engine.getCameraController()->setPositionAndTarget(camera->position, camera->target);

    engine.onUpdate = [&particleSys, &spriteSys, firenId, wybuchId, &timeOfDay, &cloudCov, &cloudDir, &skydomeCfg, &oceanCfg, &engine, &skydome, &ocean, dayNightSpeed, weatherSpeed](float dt, Engine& e) {
        static float animTime = 0.0f;
        animTime += dt;
        skydome->setTime(animTime);
        ocean->setTime(animTime);
        auto* cc = e.getCameraController();
        auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
        if (cc && cam) {
            cc->moveSpeed = 10.0f;
            auto snap = e.getPlatform()->pollInput();
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

        particleSys.update(dt, e);

        if (cam) {
            particleSys.setCameraPosition(cam->position);
        }

        {
            static float timeAcc = 0.0f;
            timeAcc += dt;

            timeOfDay += dt * dayNightSpeed;
            if (timeOfDay > 24.0f) timeOfDay -= 24.0f;

            cloudCov += dt * weatherSpeed * cloudDir;
            if (cloudCov >= 1.0f) { cloudCov = 1.0f; cloudDir = -1; }
            else if (cloudCov <= 0.0f) { cloudCov = 0.0f; cloudDir = 1; }

            float dayProgress = (timeOfDay - 6.0f) / 12.0f;
            dayProgress = std::max(0.0f, std::min(1.0f, dayProgress));
            float sunEl = std::sin(dayProgress * 3.1415926535f) * 65.0f;
            float sunAz = 90.0f + dayProgress * 180.0f;

            skydomeCfg.timeOfDay = timeOfDay;
            skydomeCfg.cloudCoverage = cloudCov;
            skydomeCfg.sunElevation = sunEl;
            skydomeCfg.sunAzimuth = sunAz;
            skydome->configure(skydomeCfg, "bin/assets/shaders/skydome", "skydome");

            float sinVal = std::sin(timeAcc * 0.6f);
            float cosVal = std::cos(timeAcc * 0.6f);

            spriteSys.setPosition(firenId, {-2.5f + cosVal * 1.5f, 2.5f + sinVal * 0.8f, 0.0f});
            spriteSys.setPosition(wybuchId, {3.0f + cosVal * 1.5f, 2.5f + sinVal * 0.8f, 0.0f});

            spriteSys.update(dt, e);
        }

        std::ostringstream oss;
        oss << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.setTextLabel(oss.str(), "times",
                                       e.getRenderer()->width() - 200.0f, 30.0f, 1.0f,
                                       {1.0f, 1.0f, 0.2f, 1.0f});

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
                << cam->position.x << " " << cam->position.y << " " << cam->position.z;
            e.setTextLabel(pos.str(), "times", 10.0f, 30.0f, 0.7f,
                                          {0.7f, 0.7f, 1.0f, 1.0f});

            Vec3 dir = normalize(subtract(cam->target, cam->position));
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
