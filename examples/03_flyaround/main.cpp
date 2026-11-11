/**
 * 03_flyaround - swobodny lot kamerą po scenie z modelami w różnych formatach.
 *
 * Pokazuje kolejno: niebo (skybox), proste siatki z kodu (podłoga, kostki), modele 3DS, model MD2
 * z animacją klatkową na CPU, model glTF z materiałami PBR i model glTF z animacją szkieletową na GPU.
 * Sterowanie: WASD + mysz, spacja / lewy Ctrl - góra / dół, L - światło i cienie, Esc - wyjście.
 *
 * Każdy krok budowy sceny to osobna funkcja wołana z main() - można je czytać po kolei.
 */
#include <SDL3/SDL.h>

#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

#include "camera/camera_component.h"
#include "camera/fly_camera.h"
#include "core/engine.h"
#include "core/entity.h"
#include "core/logger.h"
#include "core/mesh_component.h"
#include "object/gltf/model_gltf.h"
#include "object/model_3ds.h"
#include "object/model_md2.h"
#include "object/skybox_renderer.h"
#include "render/material_system.h"
#include "render/renderer.h"
#include "scene/scene.h"

using namespace engine3d;

namespace {

// Akcja przełączania światła (ruch kamery: akcje FlyCamera - W/S/A/D, spacja, lewy Ctrl).
constexpr const char* kActionToggleLighting = "render.toggle_lighting";

// Wspólne dane kroków budowy sceny.
struct SceneContext {
    Engine& engine;
    Renderer* renderer;
    MaterialManager* materials;
    MeshManager* meshes;
    Scene* scene;
};

// Mapuje klawisze fizyczne na abstrakcyjne akcje wejściowe.
void configureBindings(Engine& engine) {
    auto& map = engine.inputActions();
    map.clearAll();
    FlyCamera::bindDefaultKeys(map);
    map.bindKey(kActionToggleLighting, SDL_SCANCODE_L);
}

// Encja z siatką i materiałem.
Entity* addMeshEntity(Scene* scene, const std::string& name, const std::string& meshName, MaterialHandle material,
                      const Vec3& position, const Vec3& scale, const Vec3& rotation = {0.0f, 0.0f, 0.0f}) {
    Entity* e = scene->createEntity(name);
    e->addComponent<MeshComponent>(meshName)->materialHandle = material;
    e->transform().position = position;
    e->transform().rotation = rotation;
    e->transform().scale = scale;
    return e;
}

// Materiał z jedną teksturą.
MaterialHandle addTexturedMaterial(SceneContext& ctx, const std::string& name, TextureHandle texture) {
    Material m;
    m.name = name;
    m.textureHandle = texture;
    return ctx.materials->add(m);
}

// --- KROK 1: światło, cienie i niebo ----------------------------------------------------------------------

void setupLightingAndSky(SceneContext& ctx) {
    ctx.renderer->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    ctx.engine.setLightingEnabled(true);
    ctx.engine.setShadowsEnabled(true);
    ctx.engine.setAmbientStrength(0.35f);
    ctx.engine.setAmbientColor(0.45f, 0.48f, 0.55f);

    // Słońce: kierunek świecenia, kolor (ciepła biel), intensywność.
    ctx.scene->createDirectionalLight("sun_light", Vec3{2.3f, 2.8f, 2.5f}, Vec3{0.9f, 0.9f, 0.7f}, 3.2f);
    // Zasięg cieni słońca (obszar pokryty kaskadami).
    ctx.engine.configureDirectionalShadow(1200.0f);

    // Skybox z 3 tekstur DDS: box_12 - boki 1 i 2, box_34 - boki 3 i 4, box_5 - góra.
    const std::string dir = "assets/skybox/noc";
    auto skybox = std::make_shared<SkyboxRenderer>();
    SkyboxConfig cfg;
    cfg.enabled = true;
    cfg.directory = dir;
    cfg.size = 3500;
    cfg.verticalOffset = 100;
    skybox->configure(cfg, "bin/assets/shaders/skybox");
    skybox->setTextureHandles(ctx.renderer->loadTexture("skybox_fb", dir + "/box_12.dds"),
                              ctx.renderer->loadTexture("skybox_lr", dir + "/box_34.dds"),
                              ctx.renderer->loadTexture("skybox_top", dir + "/box_5.dds"));
    skybox->setEnabled(true);
    skybox->setExposure(0.5f);
    ctx.renderer->addFeature(skybox);   // dodanie skyboxa do potoku renderowania
}

// --- KROK 2: podłoga i kostki zbudowane w kodzie --------------------------------------------------------

void createFloorMesh(MeshManager* meshes) {
    // Kwadrat 12400 x 12400 z teksturą powtórzoną 1280 razy (UV > 1 = powtarzanie).
    const float half = 6200.0f;
    const float repeat = 1280.0f;
    MeshData floor;
    floor.vertices = {
        {{{-half, 0.0f, -half}}, {{0, 0, 1}}, {{1, 1, 1}}, {{0, 0}}},
        {{{ half, 0.0f, -half}}, {{0, 0, 1}}, {{1, 1, 1}}, {{repeat, 0}}},
        {{{ half, 0.0f,  half}}, {{0, 0, 1}}, {{1, 1, 1}}, {{repeat, repeat}}},
        {{{-half, 0.0f,  half}}, {{0, 0, 1}}, {{1, 1, 1}}, {{0, repeat}}},
    };
    floor.indices = {0, 3, 2, 2, 1, 0};
    meshes->loadMesh("floor", floor);
}

void createCubeMesh(MeshManager* meshes) {
    MeshData cube;
    const float s = 0.75f;   // pół boku
    // Ściana: 4 wierzchołki (a, b, c, d - w kolejności obwodu) z jedną normalną i 2 trójkąty.
    auto face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n) {
        const auto base = static_cast<uint32_t>(cube.vertices.size());
        const Vec3 corners[4] = {a, b, c, d};
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int i = 0; i < 4; ++i) {
            cube.vertices.push_back({{corners[i].x, corners[i].y, corners[i].z}, {n.x, n.y, n.z}, {1, 1, 1},
                                     {uv[i][0], uv[i][1]}});
        }
        cube.indices.insert(cube.indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
    };
    face({-s, -s,  s}, { s, -s,  s}, { s,  s,  s}, {-s,  s,  s}, { 0,  0,  1});
    face({ s, -s, -s}, {-s, -s, -s}, {-s,  s, -s}, { s,  s, -s}, { 0,  0, -1});
    face({-s, -s, -s}, {-s, -s,  s}, {-s,  s,  s}, {-s,  s, -s}, {-1,  0,  0});
    face({ s, -s,  s}, { s, -s, -s}, { s,  s, -s}, { s,  s,  s}, { 1,  0,  0});
    face({-s,  s,  s}, { s,  s,  s}, { s,  s, -s}, {-s,  s, -s}, { 0,  1,  0});
    face({-s, -s, -s}, { s, -s, -s}, { s, -s,  s}, {-s, -s,  s}, { 0, -1,  0});
    meshes->loadMesh("cube", cube);
}

// Zwraca uchwyt tekstury metalu - przyda się jako zapasowa tekstura modelu glTF.
TextureHandle createFloorAndCubes(SceneContext& ctx) {
    createFloorMesh(ctx.meshes);
    createCubeMesh(ctx.meshes);

    const TextureHandle metalTexture = ctx.renderer->loadTexture("metal", "assets/textures/metal.bmp");
    const MaterialHandle wood = addTexturedMaterial(ctx, "drewno", ctx.renderer->loadTexture("drewno", "assets/textures/drewno.dds"));
    const MaterialHandle sheet = addTexturedMaterial(ctx, "cegla", ctx.renderer->loadTexture("cegla", "assets/textures/blacha.dds"));
    const MaterialHandle metal = addTexturedMaterial(ctx, "metal", metalTexture);
    const MaterialHandle grass = addTexturedMaterial(ctx, "trawa", ctx.renderer->loadTexture("trawa", "assets/textures/trawa.dds"));
    const MaterialHandle sand = addTexturedMaterial(ctx, "podloga", ctx.renderer->loadTexture("podloga", "assets/textures/piasek.bmp"));

    const Vec3 scale{2.0f, 2.0f, 2.0f};
    addMeshEntity(ctx.scene, "floor", "floor", sand, {0, 0, 0}, scale);
    addMeshEntity(ctx.scene, "cube_drewno", "cube", wood, {0, 1, 0}, scale);
    addMeshEntity(ctx.scene, "cube_cegla", "cube", sheet, {-5, 1, -5}, scale);
    addMeshEntity(ctx.scene, "cube_metal", "cube", metal, {5, 1, 5}, scale);
    addMeshEntity(ctx.scene, "cube_trawa", "cube", grass, {-5, 1, 5}, scale);
    addMeshEntity(ctx.scene, "cube_drewno2", "cube", wood, {5, 1, -5}, scale);
    return metalTexture;
}

// --- KROK 3: modele 3DS ----------------------------------------------------------------------------------

// Ścieżka tekstury zapasowej: podana wprost albo sama nazwa pliku z katalogu assets/textures/.
std::string resolveFallbackTexture(const std::string& fallback) {
    if (fallback.empty() || std::filesystem::exists(fallback)) return fallback;
    if (fallback.find('/') == std::string::npos && std::filesystem::exists("assets/textures/" + fallback)) {
        return "assets/textures/" + fallback;
    }
    return {};
}

// Model 3DS: każdy obiekt w pliku to osobna encja; materiały z pliku (tekstura z pliku albo zapasowa).
void add3dsModel(SceneContext& ctx, const std::string& path, const std::string& prefix, const Vec3& position,
                 bool disableAlpha = false, const std::string& fallbackTexture = "") {
    Model3ds model;
    if (!model.load(path)) {
        LOG_ERROR("{}: nie udało się wczytać {}", prefix, path);
        return;
    }
    LOG_INFO("Wczytano {}: {} siatek, {} materiałów", prefix, model.objectList.size(), model.materialList.size());

    std::vector<MaterialHandle> materials(model.materialList.size());
    for (size_t i = 0; i < model.materialList.size(); ++i) {
        const auto& src = *model.materialList[i];
        std::string name = prefix + "_mat" + std::to_string(i) + (src.name.empty() ? "" : "_" + src.name);

        std::string texturePath = std::filesystem::exists(src.texturePath) ? src.texturePath : std::string{};
        if (texturePath.empty()) {
            texturePath = resolveFallbackTexture(fallbackTexture);
            if (!texturePath.empty()) name += "_fb";
        }

        Material material = Convert3dsMaterialToEngineMaterial(src);
        material.name = name;
        material.textureHandle = texturePath.empty()
            ? TextureHandle{}
            : ctx.renderer->loadTexture(name, texturePath, false, disableAlpha);
        materials[i] = ctx.materials->add(material);
    }

    for (size_t i = 0; i < model.objectList.size(); ++i) {
        const auto& object = *model.objectList[i];
        const std::string meshName = prefix + "_" + (object.strName.empty() ? std::to_string(i) : object.strName);
        ctx.meshes->loadMesh(meshName, object.mesh);

        MaterialHandle material{};
        if (!materials.empty()) {
            const int index = object.materialIndex;
            material = materials[(index >= 0 && index < static_cast<int>(materials.size())) ? index : 0];
        }
        addMeshEntity(ctx.scene, meshName, meshName, material, position, {0.3f, 0.3f, 0.3f});
    }
}

void add3dsModels(SceneContext& ctx) {
    add3dsModel(ctx, "assets/models/3ds/bronie/M4.3DS", "m4", {-20.0f, 10.0f, 0.0f}, true, "assets/models/3ds/bronie/m4.dds");
    add3dsModel(ctx, "assets/models/3ds/drzewko5.3DS", "DRZEWKO", {20.0f, 0.0f, 10.0f});
    add3dsModel(ctx, "assets/models/3ds/trawa/grass.3ds", "grass", {15.0f, 0.0f, 0.0f});
    add3dsModel(ctx, "assets/models/3ds/D1.3DS", "D1", {-5.0f, 0.0f, 10.0f});
    add3dsModel(ctx, "assets/models/3ds/drzewko.3DS", "Drzewko", {-10.0f, 0.0f, 10.0f});
}

// --- KROK 4: model MD2 (animacja klatkowa) ---------------------------------------------------------------

// MD2 (format z Quake 2) nie ma kości - animacja to płynne przejście (interpolacja liniowa) pozycji
// wierzchołków między kolejnymi klatkami kluczowymi, liczone na CPU co klatkę (updateGoblin).
struct Md2Character {
    Md2ModelData model;
    Md2AnimationState animation;
};

void addGoblins(SceneContext& ctx, Md2Character& goblin) {
    goblin.model = loadMd2Model("assets/models/md2/goblin/goblin.md2");
    if (goblin.model.frames.empty()) {
        LOG_ERROR("nie udało się wczytać modelu goblina (MD2)");
        return;
    }
    ctx.meshes->loadMesh("goblin_md2", goblin.model.triangles);
    LOG_INFO("Wczytano MD2: {} wierzchołków, {} trójkątów, {} klatek", goblin.model.frames[0].vertices.size(),
             goblin.model.tris.size(), goblin.model.frames.size());

    goblin.animation.startFrame = 0;
    goblin.animation.endFrame = static_cast<int>(goblin.model.frames.size()) - 1;
    goblin.animation.interpol = 0.0f;
    goblin.animation.speed = 10.0f;

    // Tekstura koloru i mapa normalnych (Dot3) - dwa gobliny: z wypukłościami i bez, dla porównania.
    const TextureHandle color = ctx.renderer->loadTexture("goblin_tex", "assets/models/md2/goblin/goblin.bmp");
    const TextureHandle normals = ctx.renderer->loadTexture("goblin_dot3", "assets/models/md2/goblin/goblinDOT3.bmp");

    Material bump;
    bump.name = "goblin_bump";
    bump.textureHandle = color;
    bump.normalMapHandle = normals;
    bump.bumpIntensity = 1.0f;
    bump.cullMode = CullMode::Disabled;

    Material flat = bump;
    flat.name = "goblin_flat";
    flat.normalMapHandle = {};
    flat.bumpIntensity = 0.0f;

    // Model MD2 jest "położony" (oś Z w górę) - obrót o -90 stopni wokół X stawia go pionowo.
    const Vec3 scale{0.4f, 0.4f, 0.4f};
    const Vec3 upright{-90.0f, 0.0f, 0.0f};
    addMeshEntity(ctx.scene, "goblin_bump", "goblin_md2", ctx.materials->add(bump), {-50.0f, 7.0f, -5.0f}, scale, upright);
    addMeshEntity(ctx.scene, "goblin_flat", "goblin_md2", ctx.materials->add(flat), {-50.0f, 7.0f, -20.0f}, scale, upright);
}

// Nowa klatka animacji MD2 liczona na CPU i wysłana do bufora siatki.
void updateGoblin(MeshManager* meshes, Md2Character& goblin, float dt) {
    if (goblin.model.frames.empty()) return;
    MeshData frame;
    animateMd2(goblin.model, goblin.animation, dt, frame);
    meshes->updateMesh("goblin_md2", frame);
}

// --- KROK 5: model glTF z materiałami PBR ----------------------------------------------------------------

void addToyCar(SceneContext& ctx, TextureHandle fallbackTexture) {
    GltfModel toyCar = loadGltfModel("assets/models/gLTF/toycar/bin/ToyCar.glb");
    LOG_INFO("Wczytano ToyCar.glb: {} siatek", toyCar.primitives.size());

    for (size_t i = 0; i < toyCar.primitives.size(); ++i) {
        const auto& prim = toyCar.primitives[i];
        const std::string name = "toycar_" + std::to_string(i);
        ctx.meshes->loadMesh(name, prim.mesh);

        // PBR: oprócz koloru mapa normalnych, ORM (occlusion, roughness, metallic) i świecenie (emissive).
        auto load = [&](const std::string& path, const char* suffix) {
            return path.empty() ? TextureHandle{} : ctx.renderer->loadTexture(name + suffix, path);
        };
        Material mat;
        mat.name = name;
        mat.textureHandle = prim.texturePath.empty() ? fallbackTexture : load(prim.texturePath, "_tex");
        mat.normalMapHandle = load(prim.normalMapPath, "_n");
        if (mat.normalMapHandle.isValid()) mat.bumpIntensity = 1.0f;
        mat.metallicRoughnessTextureHandle = load(prim.ormPath, "_orm");
        mat.occlusionTextureHandle = load(prim.occlusionPath, "_occ");
        mat.emissiveTextureHandle = load(prim.emissivePath, "_em");
        mat.cullMode = CullMode::Disabled;
        mat.roughnessFactor = 0.5f;

        addMeshEntity(ctx.scene, name, name, ctx.materials->add(mat), {-190.0f, 30.0f, 20.0f}, {0.2f, 0.2f, 0.2f});
    }
}

// --- KROK 6: glTF z animacją szkieletową -----------------------------------------------------------------

// Zamiast przesuwać wierzchołki na CPU (jak w MD2), do shadera trafiają macierze kości. Wierzchołki mają
// indeksy kości (joints) i wagi (weights) i są przekształcane na GPU w vertex shaderze.
// GltfSkinnedModel ładuje siatki, materiały i bufory wag/indeksów kości (dane wspólne), a GltfSkinnedInstance
// trzyma macierze kości jednej instancji i tworzy jej encje.
bool addSkinnedRobot(SceneContext& ctx, GltfSkinnedModel& model, GltfSkinnedInstance& instance) {
    if (!model.load(ctx.renderer, ctx.meshes, ctx.materials, ctx.engine.getAssetRegistry(), "assets/models/gLTF/BrainStem.glb") ||
        !instance.initialize(ctx.renderer, model)) {
        return false;
    }
    const auto& data = model.getModel();
    LOG_INFO("Wczytano BrainStem: {} siatek, {} animacji, {} szkieletów", data.primitives.size(),
             data.animations.size(), data.skins.size());

    Entity* root = instance.instantiate(ctx.scene, "brain");
    root->transform().position = {-40.0f, 0.0f, 20.0f};
    root->transform().scale = {10.0f, 10.0f, 10.0f};
    return true;
}

// --- KROK 7: kamera i sterowanie -------------------------------------------------------------------------

void setupCamera(SceneContext& ctx) {
    auto* camera = ctx.scene->createEntity("main_camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = {-60.0f, 15.0f, -62.0f};
    camera->target = {0.5f, 0.0f, 0.5f};
    camera->fieldOfViewDegrees = 50.0f;
    camera->nearPlane = 1.5f;
    camera->farPlane = 16000.0f;
    ctx.scene->setActiveCamera(camera);
    ctx.engine.getCameraController()->setPositionAndTarget(camera->getOwner()->transform().position, camera->target);
}

void toggleLighting(Engine& e) {
    if (!e.actionPressed(kActionToggleLighting)) return;
    const bool enabled = !e.isLightingEnabled();
    e.setLightingEnabled(enabled);
    e.setShadowsEnabled(enabled);
}

// Napisy HUD: FPS, stan oświetlenia, pozycja i kierunek kamery.
void drawHud(Engine& e) {
    const std::array<float, 4> yellow{1.0f, 1.0f, 0.2f, 1.0f};
    const std::array<float, 4> green{0.2f, 1.0f, 0.3f, 1.0f};
    const std::array<float, 4> red{1.0f, 0.3f, 0.3f, 1.0f};
    const std::array<float, 4> lightBlue{0.7f, 0.7f, 1.0f, 1.0f};

    e.setTextLabel("FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())), "times",
                   e.getRenderer()->width() - 190.0f, 35.0f, 1.0f, yellow);
    const bool lit = e.isLightingEnabled();
    e.setTextLabel(std::string("Oświetlenie [L]: ") + (lit ? "WŁĄCZONE" : "WYŁĄCZONE"), "times", 10.0f, 90.0f, 0.7f,
                   lit ? green : red);

    Scene* scene = e.getSceneManager()->activeScene();
    CameraComponent* camera = scene ? scene->activeCamera() : nullptr;
    if (!camera) return;
    const Vec3 p = camera->getOwner()->transform().position;
    const Vec3 dir = normalize(subtract(camera->target, p));
    std::ostringstream pos, look;
    pos << "pos: " << std::fixed << std::setprecision(0) << p.x << " " << p.y << " " << p.z;
    look << "dir: " << std::fixed << std::setprecision(2) << dir.x << " " << dir.y << " " << dir.z;
    e.setTextLabel(pos.str(), "times", 10.0f, 30.0f, 0.7f, lightBlue);
    e.setTextLabel(look.str(), "times", 10.0f, 60.0f, 0.7f, lightBlue);
}

} // namespace

int main() {
    Engine engine;
    if (!engine.initialize("engine3d: flyaround", 1024, 768)) return 1;
    Scene* scene = engine.getSceneManager()->activeScene();
    if (!scene) return 1;

    configureBindings(engine);
    engine.loadFont("times", "assets/fonts/times.ttf", 48);

    SceneContext ctx{engine, engine.getRenderer(), engine.getMaterialManager(), engine.getMeshManager(), scene};
    setupLightingAndSky(ctx);
    const TextureHandle metal = createFloorAndCubes(ctx);
    add3dsModels(ctx);

    Md2Character goblin;
    addGoblins(ctx, goblin);
    addToyCar(ctx, metal);

    // Model i instancja animowanego robota są zadeklarowane po silniku, więc zostaną zniszczone przed nim
    // (po engine.run(), który kończy się czekaniem na GPU).
    GltfSkinnedModel robotModel;
    GltfSkinnedInstance robot;
    const bool robotLoaded = addSkinnedRobot(ctx, robotModel, robot);

    setupCamera(ctx);

    // Logika co klatkę.
    engine.onUpdate = [&](float dt, Engine& e) {
        if (e.keyPressed(SDL_SCANCODE_ESCAPE)) e.requestQuit();

        // Robot: macierze kości dla bieżącego czasu animacji liczone na CPU, zapisane do bufora na GPU.
        if (robotLoaded) robot.update(dt);
        updateGoblin(e.getMeshManager(), goblin, dt);
        toggleLighting(e);
        FlyCamera::update(e, dt, 15.0f);
        drawHud(e);
    };

    engine.run();
    return 0;
}
