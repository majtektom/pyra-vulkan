/**
 * 07_shadows - trzy rodzaje świateł z cieniami: kierunkowe (słońce), punktowe i reflektor.
 *
 * Światła to encje z komponentem LightComponent (scene->createDirectionalLight / createPointLight /
 * createSpotLight). Każde ma marker - małą siatkę w miejscu światła, która sama nie rzuca cienia.
 * Światła poruszają się, więc cienie przeszkód przesuwają się po podłodze.
 * Sterowanie: WASD + mysz, L - oświetlenie, K - cienie, 1/2/3 - poszczególne światła, Esc - wyjście.
 */
#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <string>

#include "camera/camera_component.h"
#include "camera/fly_camera.h"
#include "core/engine.h"
#include "core/entity.h"
#include "core/light_component.h"
#include "core/mesh_component.h"
#include "render/renderer.h"
#include "scene/scene.h"

using namespace engine3d;

namespace {

/**
 * @brief Proceduralnie tworzy siatkę prostopadłościanu (Box Mesh).
 * 
 * Generuje wierzchołki, normalne (potrzebne do wyliczenia kąta padania światła),
 * współrzędne UV (dla teksturowania) oraz indeksy trójkątów (indiced rendering).
 */
MeshData makeBox(float w, float h, float d) {
    float x = w/2, y = h/2, z = d/2;
    MeshData m;
    m.vertices = {
        // Każda ściana kostki musi mieć osobne wierzchołki ze względu na unikalne wektory normalne (z-facing, x-facing itp.)
        // Ściana przód
        {{{-x,-y, z}}, {{0,0,1}}, {{1,1,1}}, {{0,0}}}, {{{ x,-y, z}}, {{0,0,1}}, {{1,1,1}}, {{1,0}}},
        {{{ x, y, z}}, {{0,0,1}}, {{1,1,1}}, {{1,1}}}, {{{-x, y, z}}, {{0,0,1}}, {{1,1,1}}, {{0,1}}},
        // Ściana tył
        {{{ x,-y,-z}}, {{0,0,-1}}, {{1,1,1}}, {{0,0}}}, {{{-x,-y,-z}}, {{0,0,-1}}, {{1,1,1}}, {{1,0}}},
        {{{-x, y,-z}}, {{0,0,-1}}, {{1,1,1}}, {{1,1}}}, {{{ x, y,-z}}, {{0,0,-1}}, {{1,1,1}}, {{0,1}}},
        // Ściana góra
        {{{-x, y, z}}, {{0,1,0}}, {{1,1,1}}, {{0,0}}}, {{{ x, y, z}}, {{0,1,0}}, {{1,1,1}}, {{1,0}}},
        {{{ x, y,-z}}, {{0,1,0}}, {{1,1,1}}, {{1,1}}}, {{{-x, y,-z}}, {{0,1,0}}, {{1,1,1}}, {{0,1}}},
        // Ściana dół
        {{{-x,-y,-z}}, {{0,-1,0}}, {{1,1,1}}, {{0,0}}}, {{{ x,-y,-z}}, {{0,-1,0}}, {{1,1,1}}, {{1,0}}},
        {{{ x,-y, z}}, {{0,-1,0}}, {{1,1,1}}, {{1,1}}}, {{{-x,-y, z}}, {{0,-1,0}}, {{1,1,1}}, {{0,1}}},
        // Ściana prawa
        {{{ x,-y, z}}, {{1,0,0}}, {{1,1,1}}, {{0,0}}}, {{{ x,-y,-z}}, {{1,0,0}}, {{1,1,1}}, {{1,0}}},
        {{{ x, y,-z}}, {{1,0,0}}, {{1,1,1}}, {{1,1}}}, {{{ x, y, z}}, {{1,0,0}}, {{1,1,1}}, {{0,1}}},
        // Ściana lewa
        {{{-x,-y,-z}}, {{-1,0,0}}, {{1,1,1}}, {{0,0}}}, {{{-x,-y, z}}, {{-1,0,0}}, {{1,1,1}}, {{1,0}}},
        {{{-x, y, z}}, {{-1,0,0}}, {{1,1,1}}, {{1,1}}}, {{{-x, y,-z}}, {{-1,0,0}}, {{1,1,1}}, {{0,1}}},
    };
    // Zbudowanie indeksów trójkątów dla 6 ścian (2 trójkąty / 6 indeksów na ścianę)
    for (int i = 0; i < 6; i++) {
        uint32_t base = i * 4;
        m.indices.insert(m.indices.end(), {base, base+1, base+2, base, base+2, base+3});
    }
    return m;
}

/**
 * @brief Tworzy prostopadłościan z nadanym kolorem wierzchołków.
 */
MeshData makeColoredBox(float w, float h, float d, float r, float g, float b) {
    MeshData m = makeBox(w, h, d);
    for (auto& v : m.vertices) v.color = {r, g, b};
    return m;
}

/**
 * @brief Proceduralnie generuje siatkę kuli (Sphere Mesh).
 * 
 * Wykorzystuje sferyczny układ współrzędnych do wyliczenia rozłożenia wierzchołków
 * na siatce geograficznej (południki i równoleżniki).
 */
MeshData makeSphere(int segments, float radius, float r, float g, float b) {
    MeshData m;
    int rings = segments / 2;
    for (int i = 0; i <= rings; i++) {
        float phi = PI * float(i) / float(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);
        
        for (int j = 0; j <= segments; j++) {
            float theta = 2.0f * PI * float(j) / float(segments);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);
            
            // Pozycja X, Y, Z na kuli
            float x = cosTheta * sinPhi * radius;
            float y = cosPhi * radius;
            float z = sinTheta * sinPhi * radius;
            
            // Wektor normalny dla kuli to po prostu znormalizowana pozycja (kierunek od środka kuli)
            m.vertices.push_back({{x, y, z}, {x/radius, y/radius, z/radius}, {r, g, b}, {0, 0}});
        }
    }
    // Generowanie indeksów łączących punkty w trójkąty
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segments; j++) {
            uint32_t a = i * (segments + 1) + j;
            uint32_t b = a + segments + 1;
            m.indices.insert(m.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return m;
}

// Siatki sceny: podłoga, przeszkody i markery świateł (kolory > 1 świecą jaśniej niż oświetlona powierzchnia).
void loadMeshes(MeshManager* meshes) {
    meshes->loadMesh("floor", makeColoredBox(16, 0.2f, 16, 0.3f, 0.3f, 0.35f));
    meshes->loadMesh("box1", makeColoredBox(2.0f, 3.0f, 2.0f, 0.6f, 0.5f, 0.3f));
    meshes->loadMesh("box2", makeColoredBox(1.5f, 4.0f, 1.5f, 0.5f, 0.4f, 0.6f));
    meshes->loadMesh("box3", makeColoredBox(1.0f, 1.5f, 1.0f, 0.4f, 0.6f, 0.5f));
    meshes->loadMesh("box4", makeColoredBox(2.5f, 2.0f, 1.0f, 0.7f, 0.4f, 0.3f));
    meshes->loadMesh("box5", makeColoredBox(1.0f, 2.5f, 2.5f, 0.3f, 0.6f, 0.4f));
    meshes->loadMesh("box6", makeColoredBox(0.8f, 5.0f, 0.8f, 0.55f, 0.55f, 0.6f));
    meshes->loadMesh("marker_dir", makeColoredBox(0.4f, 0.4f, 0.4f, 2.0f, 0.15f, 0.15f));
    meshes->loadMesh("marker_point", makeSphere(16, 0.25f, 0.15f, 2.0f, 1.8f));
    meshes->loadMesh("marker_spot", makeColoredBox(0.35f, 0.35f, 0.35f, 0.2f, 1.5f, 2.0f));
}

// Podłoga i przeszkody rzucające cienie (MeshComponent ma domyślnie castShadows = true).
void createObstacles(Scene* scene, MaterialHandle material) {
    Entity* floor = scene->createEntity("floor");
    floor->addComponent<MeshComponent>("floor")->materialHandle = material;
    floor->transform().position = {0, -0.1f, 0};

    struct Obstacle { const char* mesh; Vec3 position; };
    const Obstacle obstacles[] = {
        {"box1", {-4, 1.5f, 2}},  {"box2", {3, 2.0f, -3}},  {"box3", {0, 0.75f, 4}},  {"box4", {-5, 1.0f, -4}},
        {"box5", {5, 1.25f, -2}}, {"box3", {-3, 0.75f, -5}}, {"box4", {4, 1.0f, 5}},   {"box6", {0, 2.5f, 0}},
    };
    for (const Obstacle& o : obstacles) {
        Entity* e = scene->createEntity(o.mesh);
        e->addComponent<MeshComponent>(o.mesh)->materialHandle = material;
        e->transform().position = o.position;
    }
}

// Marker światła: siatka w miejscu światła, która nie rzuca cienia (inaczej zasłaniałaby własne światło).
void addMarker(Entity* light, const char* mesh, MaterialHandle material) {
    auto* marker = light->addComponent<MeshComponent>(mesh);
    marker->materialHandle = material;
    marker->castShadows = false;
}

struct SceneLights {
    Entity* sun = nullptr;
    Entity* point = nullptr;
    Entity* spot = nullptr;
    std::array<bool, 3> enabled{true, true, true};   // klawisze 1, 2, 3
};

SceneLights createLights(Scene* scene, MaterialHandle material) {
    SceneLights lights;
    // Kierunkowe (słońce): bez pozycji, wszystkie promienie równoległe; kierunek, kolor (czerwony), natężenie.
    lights.sun = scene->createDirectionalLight("marker_dir", Vec3{0.0f, -1.0f, 0.0f}, Vec3{0.95f, 0.3f, 0.25f}, 1.6f);
    addMarker(lights.sun, "marker_dir", material);

    // Punktowe (żarówka): świeci we wszystkie strony, słabnie z odległością; pozycja, kolor, natężenie, zasięg.
    lights.point = scene->createPointLight("marker_point", Vec3{1.0f, 2.0f, 0.0f}, Vec3{0.2f, 0.9f, 1.0f}, 2.5f, 6.0f);
    addMarker(lights.point, "marker_point", material);

    // Reflektor (latarka): stożek światła; w kącie wewnętrznym (20 st.) pełna jasność, do zewnętrznego (35 st.)
    // płynnie gaśnie. Pozycja, kierunek, kolor, natężenie, zasięg, kąty.
    lights.spot = scene->createSpotLight("marker_spot", Vec3{0.0f, 7.0f, 6.0f}, Vec3{0.0f, -1.0f, 0.0f},
                                         Vec3{0.8f, 0.85f, 1.0f}, 2.0f, 10.0f, 20.0f, 35.0f);
    addMarker(lights.spot, "marker_spot", material);
    return lights;
}

// Ruch świateł: słońce krąży nad sceną, światło punktowe podskakuje, reflektor krąży i celuje w środek.
void animateLights(SceneLights& lights, float time) {
    const Vec3 sun{std::cos(time * 0.6f) * 10.0f, 9.0f, std::sin(time * 0.6f) * 10.0f};
    lights.sun->transform().position = sun;
    lights.sun->setActive(lights.enabled[0]);
    if (lights.enabled[0]) lights.sun->getComponent<LightComponent>()->direction = normalize(sun);

    lights.point->transform().position = {1.0f, 2.0f + std::abs(std::sin(time * 0.8f)) * 4.0f, 0.0f};
    lights.point->setActive(lights.enabled[1]);

    const Vec3 spot{std::cos(-time * 0.45f) * 6.0f, 7.0f, std::sin(-time * 0.45f) * 6.0f};
    lights.spot->transform().position = spot;
    lights.spot->setActive(lights.enabled[2]);
    if (lights.enabled[2]) lights.spot->getComponent<LightComponent>()->direction = normalize(multiply(spot, -1.0f));
}

void handleToggleKeys(Engine& e, SceneLights& lights) {
    if (e.keyPressed(SDL_SCANCODE_L)) e.setLightingEnabled(!e.isLightingEnabled());
    if (e.keyPressed(SDL_SCANCODE_K)) e.setShadowsEnabled(!e.shadowsEnabled());
    const int keys[3] = {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3};
    for (int i = 0; i < 3; ++i) {
        if (e.keyPressed(keys[i])) lights.enabled[i] = !lights.enabled[i];
    }
}

void drawHud(Engine& e, const SceneLights& lights) {
    using Color = std::array<float, 4>;
    const Color green{0.2f, 1.0f, 0.3f, 1.0f}, red{1.0f, 0.3f, 0.3f, 1.0f}, grey{0.35f, 0.35f, 0.35f, 1.0f};
    e.setTextLabel("FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())), "times",
                   e.getRenderer()->width() - 190.0f, 35.0f, 1.0f, {1.0f, 1.0f, 0.2f, 1.0f});

    float y = 30.0f;
    auto line = [&](const std::string& text, const Color& color) {
        e.setTextLabel(text, "times", 10.0f, y, 0.6f, color);
        y += 23.0f;
    };
    const bool lit = e.isLightingEnabled();
    const bool shadows = e.shadowsEnabled();
    line(std::string("Oświetlenie [L]: ") + (lit ? "WŁĄCZONE" : "WYŁĄCZONE"), lit ? green : red);
    line(std::string("Cienie     [K]: ") + (shadows ? "WŁĄCZONE" : "WYŁĄCZONE"), shadows ? green : red);
    y += 4.0f;

    int active = 0;
    for (bool on : lights.enabled) active += on ? 1 : 0;
    line("ŚWIATŁA: " + std::to_string(active) + " aktywne (max 4)", {0.8f, 0.8f, 1.0f, 1.0f});

    const char* names[3] = {"Kierunkowe (czerwony)", "Punktowe   (turkusowy)", "Reflektor  (niebieski)"};
    const Color colors[3] = {{1.0f, 0.5f, 0.4f, 1.0f}, {0.4f, 1.0f, 0.5f, 1.0f}, {0.4f, 0.6f, 1.0f, 1.0f}};
    for (int i = 0; i < 3; ++i) {
        line("  [" + std::to_string(i + 1) + "] " + names[i] + (lights.enabled[i] ? "  ON" : " OFF"),
             lights.enabled[i] ? colors[i] : grey);
    }
}

} // namespace

int main() {
    Engine engine;
    if (!engine.initialize("engine3d: directional + point + spot lights", 1280, 800)) return 1;
    Scene* scene = engine.getSceneManager()->activeScene();
    if (!scene) return 1;

    engine.getRenderer()->setClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    engine.getRenderer()->setShadowsEnabled(true);
    engine.configureDirectionalShadow(30.0f);   // zasięg cieni słońca: 30 jednostek
    engine.setLightingEnabled(true);
    engine.setAmbientStrength(0.12f);           // słabe światło otoczenia - klimat ciemności
    engine.setAmbientColor(0.25f, 0.25f, 0.35f);
    engine.loadFont("times", "assets/fonts/times.ttf", 48);
    FlyCamera::bindDefaultKeys(engine.inputActions());

    loadMeshes(engine.getMeshManager());
    Material sceneMaterial;
    sceneMaterial.name = "scene_default";
    const MaterialHandle material = engine.getMaterialManager()->add(sceneMaterial);
    createObstacles(scene, material);
    SceneLights lights = createLights(scene, material);

    auto* camera = scene->createEntity("camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = {0, 8, -15};
    camera->fieldOfViewDegrees = 55;
    camera->farPlane = 1200;
    scene->setActiveCamera(camera);
    engine.getCameraController()->setPositionAndTarget(camera->getOwner()->transform().position, {0, 1, 0});

    float time = 0.0f;
    engine.onUpdate = [&](float dt, Engine& e) {
        if (e.keyPressed(SDL_SCANCODE_ESCAPE)) e.requestQuit();
        handleToggleKeys(e, lights);
        FlyCamera::update(e, dt, e.getCameraController()->moveSpeed);
        time += dt;
        animateLights(lights, time);
        drawHud(e, lights);
    };

    engine.run();
    return 0;
}
