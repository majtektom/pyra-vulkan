#include "engine.h"
#include "renderer.h"
#include "mesh_component.h"
#include "transform_component.h"
#include "camera_component.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <sstream>
#include <iomanip>

using namespace engine3d;

static MeshData makeBox(float w, float h, float d) {
    float x = w/2, y = h/2, z = d/2;
    MeshData m;
    m.vertices = {
        {{{-x,-y, z}}, {{0,0,1}}, {{1,1,1}}, {{0,0}}}, {{{ x,-y, z}}, {{0,0,1}}, {{1,1,1}}, {{1,0}}},
        {{{ x, y, z}}, {{0,0,1}}, {{1,1,1}}, {{1,1}}}, {{{-x, y, z}}, {{0,0,1}}, {{1,1,1}}, {{0,1}}},
        {{{ x,-y,-z}}, {{0,0,-1}}, {{1,1,1}}, {{0,0}}}, {{{-x,-y,-z}}, {{0,0,-1}}, {{1,1,1}}, {{1,0}}},
        {{{-x, y,-z}}, {{0,0,-1}}, {{1,1,1}}, {{1,1}}}, {{{ x, y,-z}}, {{0,0,-1}}, {{1,1,1}}, {{0,1}}},
        {{{-x, y, z}}, {{0,1,0}}, {{1,1,1}}, {{0,0}}}, {{{ x, y, z}}, {{0,1,0}}, {{1,1,1}}, {{1,0}}},
        {{{ x, y,-z}}, {{0,1,0}}, {{1,1,1}}, {{1,1}}}, {{{-x, y,-z}}, {{0,1,0}}, {{1,1,1}}, {{0,1}}},
        {{{-x,-y,-z}}, {{0,-1,0}}, {{1,1,1}}, {{0,0}}}, {{{ x,-y,-z}}, {{0,-1,0}}, {{1,1,1}}, {{1,0}}},
        {{{ x,-y, z}}, {{0,-1,0}}, {{1,1,1}}, {{1,1}}}, {{{-x,-y, z}}, {{0,-1,0}}, {{1,1,1}}, {{0,1}}},
        {{{ x,-y, z}}, {{1,0,0}}, {{1,1,1}}, {{0,0}}}, {{{ x,-y,-z}}, {{1,0,0}}, {{1,1,1}}, {{1,0}}},
        {{{ x, y,-z}}, {{1,0,0}}, {{1,1,1}}, {{1,1}}}, {{{ x, y, z}}, {{1,0,0}}, {{1,1,1}}, {{0,1}}},
        {{{-x,-y,-z}}, {{-1,0,0}}, {{1,1,1}}, {{0,0}}}, {{{-x,-y, z}}, {{-1,0,0}}, {{1,1,1}}, {{1,0}}},
        {{{-x, y, z}}, {{-1,0,0}}, {{1,1,1}}, {{1,1}}}, {{{-x, y,-z}}, {{-1,0,0}}, {{1,1,1}}, {{0,1}}},
    };
    for (int i = 0; i < 6; i++) {
        uint32_t base = i * 4;
        m.indices.insert(m.indices.end(), {base, base+1, base+2, base, base+2, base+3});
    }
    return m;
}

static MeshData makeColoredBox(float w, float h, float d, float r, float g, float b) {
    MeshData m = makeBox(w, h, d);
    for (auto& v : m.vertices) v.color = {r, g, b};
    return m;
}

static MeshData makeSphere(int segments, float radius, float r, float g, float b) {
    MeshData m;
    int rings = segments / 2;
    for (int i = 0; i <= rings; i++) {
        float phi = 3.14159265f * float(i) / float(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);
        for (int j = 0; j <= segments; j++) {
            float theta = 2.0f * 3.14159265f * float(j) / float(segments);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);
            float x = cosTheta * sinPhi * radius;
            float y = cosPhi * radius;
            float z = sinTheta * sinPhi * radius;
            m.vertices.push_back({{x, y, z}, {x/radius, y/radius, z/radius}, {r, g, b}, {0, 0}});
        }
    }
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segments; j++) {
            uint32_t a = i * (segments + 1) + j;
            uint32_t b = a + segments + 1;
            m.indices.insert(m.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return m;
}

int main() {
    Engine engine;
    if (!engine.initialize("engine3d: directional + point + spot lights", 1280, 800)) return 1;

    auto* renderer = engine.getRenderer();
    renderer->setClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    renderer->setShadowsEnabled(true);
    engine.setLightingEnabled(true);
    engine.setAmbientStrength(0.12f);
    engine.setAmbientColor(0.25f, 0.25f, 0.35f);

    engine.loadFont("times", "assets/fonts/times.ttf", 48);

    engine.getMeshManager()->loadMesh("floor", makeColoredBox(16, 0.2f, 16, 0.3f, 0.3f, 0.35f));
    engine.getMeshManager()->loadMesh("box1", makeColoredBox(2.0f, 3.0f, 2.0f, 0.6f, 0.5f, 0.3f));
    engine.getMeshManager()->loadMesh("box2", makeColoredBox(1.5f, 4.0f, 1.5f, 0.5f, 0.4f, 0.6f));
    engine.getMeshManager()->loadMesh("box3", makeColoredBox(1.0f, 1.5f, 1.0f, 0.4f, 0.6f, 0.5f));
    engine.getMeshManager()->loadMesh("box4", makeColoredBox(2.5f, 2.0f, 1.0f, 0.7f, 0.4f, 0.3f));
    engine.getMeshManager()->loadMesh("box5", makeColoredBox(1.0f, 2.5f, 2.5f, 0.3f, 0.6f, 0.4f));
    engine.getMeshManager()->loadMesh("box6", makeColoredBox(0.8f, 5.0f, 0.8f, 0.55f, 0.55f, 0.6f));

    engine.getMeshManager()->loadMesh("marker_dir", makeColoredBox(0.4f, 0.4f, 0.4f, 2.0f, 0.15f, 0.15f));
    engine.getMeshManager()->loadMesh("marker_point", makeSphere(16, 0.25f, 0.15f, 2.0f, 1.8f));
    engine.getMeshManager()->loadMesh("marker_spot", makeColoredBox(0.35f, 0.35f, 0.35f, 0.2f, 1.5f, 2.0f));

    Material matLit;
    matLit.name = "lit_mat";
    matLit.shaderProgram = "lit";
    engine.getMaterialManager()->add(matLit);
    auto litInst = engine.getMaterialSystem()->getOrCreateMaterialInstance(matLit);

    Material matUnlit;
    matUnlit.name = "unlit_marker";
    matUnlit.shaderProgram = "unlit";
    engine.getMaterialManager()->add(matUnlit);
    auto unlitInst = engine.getMaterialSystem()->getOrCreateMaterialInstance(matUnlit);

    auto* floorE = engine.createEntity("floor");
    floorE->addComponent<MeshComponent>("floor")->materialInstance = litInst;
    auto* ft = floorE->addComponent<TransformComponent>();
    ft->translation = {0, -0.1f, 0};

    struct { const char* mesh; float x, y, z; } obstacles[] = {
        {"box1", -4, 1.5f, 2}, {"box2",  3, 2.0f, -3},
        {"box3",  0, 0.75f, 4}, {"box4", -5, 1.0f, -4},
        {"box5",  5, 1.25f, -2}, {"box3", -3, 0.75f, -5},
        {"box4",  4, 1.0f, 5},   {"box6",  0, 2.5f, 0},
    };
    for (auto& obs : obstacles) {
        auto* e = engine.createEntity(obs.mesh);
        e->addComponent<MeshComponent>(obs.mesh)->materialInstance = litInst;
        auto* t = e->addComponent<TransformComponent>();
        t->translation = {obs.x, obs.y, obs.z};
    }

    auto* markerDir = engine.createEntity("marker_dir");
    markerDir->addComponent<MeshComponent>("marker_dir")->materialInstance = unlitInst;
    auto* mdT = markerDir->addComponent<TransformComponent>();

    auto* markerPoint = engine.createEntity("marker_point");
    markerPoint->addComponent<MeshComponent>("marker_point")->materialInstance = unlitInst;
    auto* mpT = markerPoint->addComponent<TransformComponent>();

    auto* markerSpot = engine.createEntity("marker_spot");
    markerSpot->addComponent<MeshComponent>("marker_spot")->materialInstance = unlitInst;
    auto* msT = markerSpot->addComponent<TransformComponent>();

    auto* camera = engine.createEntity("camera")->addComponent<CameraComponent>();
    camera->position = {0, 8, -15};
    camera->fieldOfViewDegrees = 55;
    camera->farPlane = 200;
    engine.setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(camera->position, {0, 1, 0});

    engine.onUpdate = [&](float dt, Engine& e) {
        auto snap = e.getPlatform()->pollInput();

        static bool lWasDown = false;
        static bool kWasDown = false;
        static bool n1WasDown = false;
        static bool n2WasDown = false;
        static bool n3WasDown = false;
        static bool light1On = true;
        static bool light2On = true;
        static bool light3On = true;

        bool lDown = snap.keys[SDL_SCANCODE_L];
        bool kDown = snap.keys[SDL_SCANCODE_K];
        bool n1Down = snap.keys[SDL_SCANCODE_1];
        bool n2Down = snap.keys[SDL_SCANCODE_2];
        bool n3Down = snap.keys[SDL_SCANCODE_3];

        if (lDown && !lWasDown) e.setLightingEnabled(!e.isLightingEnabled());
        if (kDown && !kWasDown) e.setShadowsEnabled(!e.shadowsEnabled());
        if (n1Down && !n1WasDown) light1On = !light1On;
        if (n2Down && !n2WasDown) light2On = !light2On;
        if (n3Down && !n3WasDown) light3On = !light3On;
        lWasDown = lDown;
        kWasDown = kDown;
        n1WasDown = n1Down;
        n2WasDown = n2Down;
        n3WasDown = n3Down;

        auto* cc = e.getCameraController();
        auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
        if (cc && cam) {
            float vel = cc->moveSpeed * dt;
            if (snap.keys[SDL_SCANCODE_W]) cc->moveForward(vel);
            if (snap.keys[SDL_SCANCODE_S]) cc->moveForward(-vel);
            if (snap.keys[SDL_SCANCODE_A]) cc->moveRight(-vel);
            if (snap.keys[SDL_SCANCODE_D]) cc->moveRight(vel);
            float ts = cc->mouseSensitivity;
            cc->rotateYaw(-static_cast<float>(snap.mouseDeltaX) * ts);
            cc->rotatePitch(-static_cast<float>(snap.mouseDeltaY) * ts);
            cc->update(dt, *cam);
        }

        static float time = 0;
        time += dt;

        // --- Setup lights each frame ---
        e.clearLights();

        int activeCount = 0;

        // 1. Directional light (red marker) — orbiting sun
        float sunAngle = time * 0.6f;
        float sunRad = 10.0f;
        float sx = std::cos(sunAngle) * sunRad;
        float sz = std::sin(sunAngle) * sunRad;
        float sy = 9.0f;

        mdT->translation = {sx, sy, sz};
        markerDir->setActive(light1On);

        if (light1On) {
            float sdist = std::sqrt(sx*sx + sy*sy + sz*sz);
            e.addDirectionalLight(sx/sdist, sy/sdist, sz/sdist,  0.95f, 0.3f, 0.25f, 1.6f);
            activeCount++;
        }

        // 2. Point light (green marker) — bouncing up and down
        float py = 2.0f + std::abs(std::sin(time * 0.8f)) * 4.0f;
        mpT->translation = {1.0f, py, 0.0f};
        markerPoint->setActive(light2On);

        if (light2On) {
            e.addPointLight(1.0f, py, 0.0f,  0.2f, 0.9f, 1.0f,  2.5f, 6.0f);
            activeCount++;
        }

        // 3. Spot light (blue marker) — circling and pointing at floor
        float spotAngle = -time * 0.45f;
        float spotH = 7.0f;
        float spotR = 6.0f;
        float spx = std::cos(spotAngle) * spotR;
        float spz = std::sin(spotAngle) * spotR;
        float spy = spotH;

        msT->translation = {spx, spy, spz};
        markerSpot->setActive(light3On);

        if (light3On) {
            float sdx = -spx;
            float sdz = -spz;
            float sdy = -spy;
            float sdlen = std::sqrt(sdx*sdx + sdy*sdy + sdz*sdz);
            e.addSpotLight(spx, spy, spz,  sdx/sdlen, sdy/sdlen, sdz/sdlen,
                           0.8f, 0.85f, 1.0f,  2.0f, 10.0f,  20.0f, 35.0f);
            activeCount++;
        }

        // --- HUD ---
        float rw = (float)e.getRenderer()->width();

        std::ostringstream fps;
        fps << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.setTextLabel(fps.str(), "times", rw - 190.0f, 35.0f, 1.0f,
                       {1.0f, 1.0f, 0.2f, 1.0f});

        float y = 30.0f;
        auto label = [&](const std::string& txt, const std::array<float,4>& c) {
            e.setTextLabel(txt, "times", 10.0f, y, 0.6f, c);
            y += 23.0f;
        };

        bool lit = e.isLightingEnabled();
        bool shd = e.shadowsEnabled();

        std::ostringstream ltxt;
        ltxt << "Oswietlenie [L]: " << (lit ? "WLACZONE" : "WYLACZONE");
        label(ltxt.str(), lit ? std::array<float,4>{0.2f,1.0f,0.3f,1.f}
                              : std::array<float,4>{1.0f,0.3f,0.3f,1.f});

        std::ostringstream stxt;
        stxt << "Cienie     [K]: " << (shd ? "WLACZONE" : "WYLACZONE");
        label(stxt.str(), shd ? std::array<float,4>{0.2f,1.0f,0.3f,1.f}
                              : std::array<float,4>{1.0f,0.3f,0.3f,1.f});

        y += 4.0f;
        {
            std::ostringstream cnt;
            cnt << "SWIATLA: " << activeCount << " aktywne (max 4)";
            label(cnt.str(), {0.8f, 0.8f, 1.0f, 1.0f});
        }

        auto lightLabel = [&](int n, const std::string& name, bool on,
                              const std::array<float,4>& c) {
            std::ostringstream s;
            s << "  [" << n << "] " << name << (on ? "  ON" : " OFF");
            label(s.str(), on ? c : std::array<float,4>{0.35f, 0.35f, 0.35f, 1.f});
        };

        lightLabel(1, "Kierunkowe (czerwony)", light1On, {1.0f, 0.5f, 0.4f, 1.0f});
        lightLabel(2, "Punktowe   (zielony) ", light2On, {0.4f, 1.0f, 0.5f, 1.0f});
        lightLabel(3, "Reflektora (niebieski)", light3On, {0.4f, 0.6f, 1.0f, 1.0f});
    };

    engine.run();
    return 0;
}
