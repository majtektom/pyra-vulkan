#include "core/logger.h"
#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include "core/engine.h"
#include "core/entity.h"
#include "render/renderer.h"
#include "core/mesh_component.h"
#include "core/light_component.h"
#include "camera/camera_component.h"
#include "object/model_3ds.h"
#include "object/model_md2.h"
#include "object/gltf/model_gltf.h"
#include "object/skybox_renderer.h"
#include "scene/scene.h"
#include "render/material_system.h"
#include "render/vulkan_util.h"

using namespace engine3d;

namespace {

// Nazwy akcji wejściowych powiązanych z klawiszami (Input Action System)
constexpr const char* kActionMoveForward = "move.forward";
constexpr const char* kActionMoveBackward = "move.backward";
constexpr const char* kActionMoveLeft = "move.left";
constexpr const char* kActionMoveRight = "move.right";
constexpr const char* kActionMoveUp = "move.up";
constexpr const char* kActionMoveDown = "move.down";
constexpr const char* kActionToggleLighting = "render.toggle_lighting";

/**
 * @brief Mapuje klawisze fizyczne na abstrakcyjne akcje wejściowe.
 */
void configureFlyaroundBindings(Engine& engine) {
    auto& map = engine.inputActions();
    map.clearAll();
    map.bindKey(kActionMoveForward, SDL_SCANCODE_W);
    map.bindKey(kActionMoveBackward, SDL_SCANCODE_S);
    map.bindKey(kActionMoveLeft, SDL_SCANCODE_A);
    map.bindKey(kActionMoveRight, SDL_SCANCODE_D);
    map.bindKey(kActionMoveUp, SDL_SCANCODE_SPACE);
    map.bindKey(kActionMoveDown, SDL_SCANCODE_LCTRL);
    map.bindKey(kActionToggleLighting, SDL_SCANCODE_L);
}

} // namespace

int main() {
    Engine engine;

    // Inicjalizacja okna o wymiarach 1024x768 pikseli
    if (!engine.initialize("engine3d: flyaround", 1024, 768)) {
        return 1;
    }

    configureFlyaroundBindings(engine);

    // Pobranie najważniejszych menedżerów silnika
    auto* renderer = engine.getRenderer();
    auto* mats = engine.getMaterialManager();
    auto* assets = engine.getAssetRegistry();
    std::unordered_map<std::string, MaterialHandle> materialHandles;

    // Lambda pomocnicza do pobierania nazwy tekstury z rejestru zasobów
    auto requireTextureName = [&](TextureHandle textureHandle, const std::string& fallback = "default") -> std::string {
        if (!textureHandle.isValid() || !assets) {
            return fallback;
        }
        const auto name = assets->textureName(textureHandle);
        return name.has_value() ? *name : fallback;
    };

    // Lambda pomocnicza do bezpiecznego pobierania identyfikatora materiału
    auto requireMaterialHandle = [&](const std::string& materialName) -> MaterialHandle {
        const auto found = materialHandles.find(materialName);
        if (found != materialHandles.end()) {
            return found->second;
        }
        const auto handle = mats->handle(materialName);
        if (handle.has_value()) {
            return *handle;
        }
        LOG_ERROR("Brak MaterialHandle dla materialu: {}", materialName);
        return {};
    };

    // Konfiguracja sceny, oświetlenia i cieni
    renderer->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    engine.setLightingEnabled(true);
    engine.setShadowsEnabled(true);
    engine.setAmbientStrength(0.35f);
    engine.setAmbientColor(0.45f, 0.48f, 0.55f);

    auto* activeScene = engine.getSceneManager()->activeScene();
    if (!activeScene) {
        return 1;
    }

    // Tworzenie światła kierunkowego (np. Słońce)
    auto* sunLightEntity = activeScene->createDirectionalLight(
        "sun_light",
        Vec3{2.3f, 2.8f, 2.5f},  // Kierunek świecenia
        Vec3{0.9f, 0.9f, 0.7f},  // Kolor (ciepła biel)
        3.2f);                   // Intensywność
    auto* sunLight = sunLightEntity->getComponent<LightComponent>();

    // Konfiguracja rzutowania cieni (rozmiar kaskady/obszaru widoczności cieni)
    engine.configureDirectionalShadow(1200.0f);

    // KROK 1: ŁADOWANIE SKYBOXA (Nieba otaczającego scenę)
    // Ładujemy 3 tekstury w formacie DDS (box_12 to bok 1 i 2, box_34 to 3 i 4, box_5 to góra)
    const std::string skyboxDir = "assets/skybox/noc";
    const auto skyboxFbHandle = renderer->loadTexture("skybox_fb", skyboxDir + "/box_12.dds");
    const auto skyboxLrHandle = renderer->loadTexture("skybox_lr", skyboxDir + "/box_34.dds");
    const auto skyboxTopHandle = renderer->loadTexture("skybox_top", skyboxDir + "/box_5.dds");

    auto skybox = std::make_shared<SkyboxRenderer>();
    SkyboxConfig skyboxCfg;
    skyboxCfg.enabled = true;
    skyboxCfg.directory = skyboxDir;
    skyboxCfg.size = 3500;
    skyboxCfg.verticalOffset = 100;
    skybox->configure(skyboxCfg, "bin/assets/shaders/skybox");
    skybox->setTextureHandles(skyboxFbHandle, skyboxLrHandle, skyboxTopHandle);
    skybox->setEnabled(true);
    skybox->setExposure(0.5f);
    renderer->addFeature(skybox); // Dodanie skyboxa do potoku renderowania

    engine.loadFont("times", "assets/fonts/times.ttf", 48);

    // KROK 2: WCZYTYWANIE TEKSTUR ŚRODOWISKOWYCH
    const auto drewnoHandle = renderer->loadTexture("drewno", "assets/textures/drewno.dds");
    const auto ceglaHandle = renderer->loadTexture("cegla", "assets/textures/blacha.dds");
    const auto metalHandle = renderer->loadTexture("metal", "assets/textures/metal.bmp");
    const auto trawaHandle = renderer->loadTexture("trawa", "assets/textures/trawa.dds");
    const auto podlogaHandle = renderer->loadTexture("podloga", "assets/textures/piasek.bmp");

    // Rejestrowanie materiałów powiązanych z teksturami
    auto addMat = [&](const std::string& name, TextureHandle tex) {
        Material m;
        m.name = name;
        m.textureHandle = tex;
        materialHandles[name] = mats->add(m);
    };
    addMat("drewno", drewnoHandle);
    addMat("cegla", ceglaHandle);
    addMat("metal", metalHandle);
    addMat("trawa", trawaHandle);
    addMat("podloga", podlogaHandle);

    // KROK 3: UTWORZENIE PŁASZCZYZNY PODŁOGI (Floor Mesh)
    {
        MeshData floor;
        floor.vertices = {
            {{{-6200.0f, 0.0f, -6200.0f}}, {{0,0,1}}, {{1,1,1}}, {{0,0}}},
            {{{ 6200.0f, 0.0f, -6200.0f}}, {{0,0,1}}, {{1,1,1}}, {{1280,0}}},
            {{{ 6200.0f, 0.0f,  6200.0f}}, {{0,0,1}}, {{1,1,1}}, {{1280,1280}}},
            {{{-6200.0f, 0.0f,  6200.0f}}, {{0,0,1}}, {{1,1,1}}, {{0,1280}}},
        };
        floor.indices = {0, 3, 2, 2, 1, 0};
        engine.getMeshManager()->loadMesh("floor", floor);
    }

    // KROK 4: UTWORZENIE SIATKI KOSTKI (Cube Mesh)
    {
        MeshData cube;
        const float s = 0.75f;
        auto vert = [&](float x, float y, float z, float nx, float ny, float nz, float u, float v) {
            cube.vertices.push_back({{x, y, z}, {nx, ny, nz}, {1,1,1}, {u, v}});
        };
        auto face = [&](float x1, float y1, float z1, float x2, float y2, float z2,
                        float x3, float y3, float z3, float x4, float y4, float z4,
                        float nx, float ny, float nz) {
            auto b = static_cast<uint32_t>(cube.vertices.size());
            vert(x1,y1,z1, nx,ny,nz, 0,0); vert(x2,y2,z2, nx,ny,nz, 1,0);
            vert(x3,y3,z3, nx,ny,nz, 1,1); vert(x4,y4,z4, nx,ny,nz, 0,1);
            cube.indices.insert(cube.indices.end(), {b+0,b+1,b+2, b+2,b+3,b+0});
        };
        // Rysowanie 6 ścian kostki z normalnymi powierzchni
        face(-s,-s, s,  s,-s, s,  s, s, s, -s, s, s,  0,0,1);
        face( s,-s,-s, -s,-s,-s, -s, s,-s,  s, s,-s, 0,0,-1);
        face(-s,-s,-s, -s,-s, s, -s, s, s, -s, s,-s, -1,0,0);
        face( s,-s, s,  s,-s,-s,  s, s,-s,  s, s, s,  1,0,0);
        face(-s, s, s,  s, s, s,  s, s,-s, -s, s,-s,  0,1,0);
        face(-s,-s,-s,  s,-s,-s,  s,-s, s, -s,-s, s,  0,-1,0);
        engine.getMeshManager()->loadMesh("cube", cube);
    }

    // Tworzenie obiektów w świecie przy użyciu kostki i podłogi
    auto addObj = [&](const std::string& name, const std::string& meshName,
                      const std::string& matName, const Vec3& pos) {
        auto* e = activeScene->createEntity(name);
        auto* mc = e->addComponent<MeshComponent>(meshName);
        mc->materialHandle = requireMaterialHandle(matName);
        e->transform().position = pos;
        e->transform().scale = {2.0f, 2.0f, 2.0f};
    };

    addObj("floor", "floor", "podloga", {0,0,0});
    addObj("cube_drewno", "cube", "drewno", {0,1,0});
    addObj("cube_cegla", "cube", "cegla", {-5,1,-5});
    addObj("cube_metal", "cube", "metal", {5,1,5});
    addObj("cube_trawa", "cube", "trawa", {-5,1,5});
    addObj("cube_drewno2", "cube", "drewno", {5,1,-5});

    // KROK 5: WCZYTYWANIE TRADYCYJNYCH MODELI 3DS
    auto addModel = [&](const std::string& path, const std::string& prefix,
                        const Vec3& position, bool disableAlpha = false,
                        const std::string& fallbackTexture = "") {
        Model3ds model;
        if (!model.load(path)) {
            LOG_ERROR("{} load skipped: {}", prefix, path);
            return;
        }
        LOG_INFO("Loaded {}: {} meshes, {} materials", prefix, model.objectList.size(), model.materialList.size());

        std::vector<std::string> finalMatNames(model.materialList.size());
        for (size_t mi = 0; mi < model.materialList.size(); ++mi) {
            const auto& mat = *model.materialList[mi];
            std::string matName = prefix + "_mat" + std::to_string(mi)
                + (mat.name.empty() ? "" : "_" + mat.name);

            bool texExists = !mat.texturePath.empty();
            if (texExists) {
                FILE* test = fopen(mat.texturePath.c_str(), "rb");
                if (test) {
                    fclose(test);
                } else {
                    texExists = false;
                }
            }

            std::string texName = matName;
            std::string texPathToLoad = mat.texturePath;
            TextureHandle texHandle{};
            if (!texExists && !fallbackTexture.empty()) {
                std::string fbPath = fallbackTexture;
                FILE* test = fopen(fbPath.c_str(), "rb");
                if (!test && fbPath.find('/') == std::string::npos) {
                    fbPath = "assets/textures/" + fallbackTexture;
                    test = fopen(fbPath.c_str(), "rb");
                }
                if (test) {
                    fclose(test);
                    matName += "_fb";
                    texExists = true;
                    texPathToLoad = fbPath;
                }
            }

            if (texExists) {
                texHandle = renderer->loadTexture(matName, texPathToLoad, false, disableAlpha);
            }

            Material modelMat = Convert3dsMaterialToEngineMaterial(mat);
            modelMat.name = matName;
            modelMat.textureHandle = texExists ? texHandle : TextureHandle{};
            // if (disableAlpha) {
            //     modelMat.blendMode = BlendMode::Disabled;
            //     modelMat.depthWrite = true;
            // }
            materialHandles[matName] = mats->add(modelMat);

            finalMatNames[mi] = matName;
        }

        // Dodanie wszystkich pod-siatek (sub-meshes) obiektu 3DS jako niezależnych encji
        for (size_t i = 0; i < model.objectList.size(); ++i) {
            const auto& obj = *model.objectList[i];
            std::string meshName = prefix + "_" + (obj.strName.empty()
                ? std::to_string(i) : obj.strName);

            engine.getMeshManager()->loadMesh(meshName, obj.mesh);

            std::string matName;
            if (!finalMatNames.empty()) {
                int mi = obj.materialIndex;
                if (mi < 0 || mi >= static_cast<int>(finalMatNames.size())) {
                    mi = 0;
                }
                matName = finalMatNames[mi];
            }

            auto* e = activeScene->createEntity(meshName);
            auto* mc = e->addComponent<MeshComponent>(meshName);
            mc->materialHandle = requireMaterialHandle(matName);
            e->transform().position = position;
            e->transform().scale = {0.3f, 0.3f, 0.3f};
        }
    };

    addModel("assets/models/3ds/bronie/M4.3DS", "m4", {-20.0f, 10.0f, 0.0f}, true, "assets/models/3ds/bronie/m4.dds");
    addModel("assets/models/3ds/drzewko5.3DS", "DRZEWKO", {20.0f, 0.0f, 10.0f});
    addModel("assets/models/3ds/trawa/grass.3ds", "grass", {15.0f, 0.0f, 0.0f});
    addModel("assets/models/3ds/D1.3DS", "D1", {-5.0f, 0.0f, 10.0f});
    addModel("assets/models/3ds/drzewko.3DS", "Drzewko", {-10.0f, 0.0f, 10.0f});

    // KROK 6: WCZYTYWANIE MODELU MD2 (Klasyczna animacja klatkowa z Quake 2)
    // Model MD2 nie posiada kości – animacja polega na płynnej, liniowej interpolacji (Morph Target)
    // pozycji wierzchołków bezpośrednio między kolejnymi klatkami kluczowymi.
    auto md2Model = std::make_shared<Md2ModelData>();
    auto md2State = std::make_shared<Md2AnimationState>();
    {
        *md2Model = loadMd2Model("assets/models/md2/goblin/goblin.md2");
        engine.getMeshManager()->loadMesh("goblin_md2", md2Model->triangles);
        std::cout << "Loaded MD2: " << md2Model->frames[0].vertices.size() << " verts, "
                  << md2Model->tris.size() << " tris, " << md2Model->frames.size() << " frames" << std::endl;

        md2State->startFrame = 0;
        md2State->endFrame = (int)md2Model->frames.size() - 1;
        md2State->interpol = 0.0f;
        md2State->speed = 10.0f;

        // Wczytanie tekstury dyfuzyjnej i mapy normalnych w przestrzeni stycznej (Dot3 Normal Map)
        const auto goblinTexHandle = renderer->loadTexture("goblin_tex", "assets/models/md2/goblin/goblin.bmp");
        const auto goblinDot3Handle = renderer->loadTexture("goblin_dot3", "assets/models/md2/goblin/goblinDOT3.bmp");

        // Materiał z mapowaniem normalnych (Bump Mapping)
        Material matGoblinBump;
        matGoblinBump.name = "goblin_bump";
        matGoblinBump.textureHandle = goblinTexHandle;
        matGoblinBump.normalMapHandle = goblinDot3Handle;
        matGoblinBump.bumpIntensity = 1.0f;
        matGoblinBump.cullMode = CullMode::Disabled;
        materialHandles[matGoblinBump.name] = mats->add(matGoblinBump);

        // Materiał płaski bez mapowania normalnych
        Material matGoblinFlat;
        matGoblinFlat.name = "goblin_flat";
        matGoblinFlat.textureHandle = goblinTexHandle;
        matGoblinFlat.bumpIntensity = 0.0f;
        matGoblinFlat.cullMode = CullMode::Disabled;
        materialHandles[matGoblinFlat.name] = mats->add(matGoblinFlat);

        // Goblin 1 (z cieniowaniem wybojów / bump mappingiem)
        {
            auto* e = activeScene->createEntity("goblin_bump");
            auto* mc = e->addComponent<MeshComponent>("goblin_md2");
            mc->materialHandle = requireMaterialHandle("goblin_bump");
            e->transform().position = {-50.0f, 7.0f, -5.0f};
            e->transform().rotation = {-90.0f, 0.0f, 0.0f};
            e->transform().scale = {0.4f, 0.4f, 0.4f};
        }
        // Goblin 2 (standardowa płaska tekstura)
        {
            auto* e = activeScene->createEntity("goblin_flat");
            auto* mc = e->addComponent<MeshComponent>("goblin_md2");
            mc->materialHandle = requireMaterialHandle("goblin_flat");
            e->transform().position = {-50.0f, 7.0f, -20.0f};
            e->transform().rotation = {-90.0f, 0.0f, 0.0f};
            e->transform().scale = {0.4f, 0.4f, 0.4f};
        }
    }

    // KROK 7: WCZYTYWANIE MODELU GLTF/GLB (ToyCar.glb - Fizycznie Bazowany Rendering PBR)
    {
        auto toyCar = loadGltfModel("assets/models/gLTF/toycar/bin/ToyCar.glb");
        std::cout << "Loaded ToyCar GLB: " << toyCar.primitives.size() << " primitives\n";

        for (size_t i = 0; i < toyCar.primitives.size(); ++i) {
            auto& prim = toyCar.primitives[i];
            std::string name = "toycar_" + std::to_string(i);
            engine.getMeshManager()->loadMesh(name, prim.mesh);

            TextureHandle texHandle = metalHandle;
            if (!prim.texturePath.empty()) {
                texHandle = renderer->loadTexture(name + "_tex", prim.texturePath);
            }

            // PBR korzysta z dodatkowych tekstur PBR (Normal Map, ORM: Occlusion, Roughness, Metallic, oraz Emissive)
            TextureHandle nmHandle{}, ormHandle{}, occHandle{}, emHandle{};
            if (!prim.normalMapPath.empty()) { nmHandle = renderer->loadTexture(name + "_n", prim.normalMapPath); }
            if (!prim.ormPath.empty())        { ormHandle = renderer->loadTexture(name + "_orm", prim.ormPath); }
            if (!prim.occlusionPath.empty())  { occHandle = renderer->loadTexture(name + "_occ", prim.occlusionPath); }
            if (!prim.emissivePath.empty())   { emHandle = renderer->loadTexture(name + "_em", prim.emissivePath); }

            Material mat;
            mat.name = name;
            mat.textureHandle = texHandle;
            mat.cullMode = CullMode::Disabled;
            if (nmHandle.isValid()) { mat.normalMapHandle = nmHandle; mat.bumpIntensity = 1.0f; }
            if (ormHandle.isValid()) mat.metallicRoughnessTextureHandle = ormHandle;
            if (occHandle.isValid()) mat.occlusionTextureHandle = occHandle;
            if (emHandle.isValid()) mat.emissiveTextureHandle = emHandle;
            mat.roughnessFactor = 0.5f;
            materialHandles[mat.name] = mats->add(mat);

            auto* e = activeScene->createEntity(name);
            auto* mc = e->addComponent<MeshComponent>(name);
            mc->materialHandle = requireMaterialHandle(name);
            e->transform().position = {-190.0f, 30.0f, 20.0f};
            e->transform().rotation = {0.0f, 0.0f, 0.0f};
            e->transform().scale = {0.2f, 0.2f, 0.2f};
        }
    }

    // KROK 8: ZAAWANSOWANE ŁADOWANIE GLTF Z ANIMACJĄ SZKIELETOWĄ (Skeletal Animation & Skinning)
    // Zamiast interpolować położenia wierzchołków CPU (jak w MD2), do shadera przekazujemy
    // macierze kości (Joint Matrices). Wierzchołki posiadają indeksy (Joints) i wagi (Weights)
    // i są przekształcane bezpośrednio na GPU w shaderze wierzchołków.
    auto brainModel = std::make_shared<GltfModel>();
    VkBuffer brainBoneBuffer = VK_NULL_HANDLE;
    VkDeviceMemory brainBoneMemory = VK_NULL_HANDLE;
    VkDevice brainDev = VK_NULL_HANDLE;
    {
        *brainModel = loadGltfModel("assets/models/gLTF/BrainStem.glb");
        std::cout << "Loaded BrainStem: " << brainModel->primitives.size() << " prims, "
                  << brainModel->animations.size() << " anims, " << brainModel->skins.size() << " skins\n";

        VkDevice dev = renderer->device();
        brainDev = dev;

        // Tworzenie bufora uniform (UBO) na macierze transformacji kości (maksymalnie 64 kości)
        constexpr int kMaxBones = 64;
        VkDeviceSize boneSize = kMaxBones * sizeof(Matrix4);
        vk::createBuffer(dev, renderer->physicalDevice(), boneSize,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         brainBoneBuffer, brainBoneMemory);

        // Przydzielenie Descriptor Set dedykowanego pod bufor kości (Set 1, patrz MaterialCompiler)
        VkDescriptorPoolSize bonePoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        VkDescriptorPoolCreateInfo bonePoolInfo{};
        bonePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        bonePoolInfo.maxSets = 1;
        bonePoolInfo.poolSizeCount = 1;
        bonePoolInfo.pPoolSizes = &bonePoolSize;
        VkDescriptorPool bonePool;
        vkCreateDescriptorPool(dev, &bonePoolInfo, nullptr, &bonePool);

        VkDescriptorSetLayoutBinding boneBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo boneLayoutInfo{};
        boneLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        boneLayoutInfo.bindingCount = 1;
        boneLayoutInfo.pBindings = &boneBinding;
        VkDescriptorSetLayout boneLayout;
        vkCreateDescriptorSetLayout(dev, &boneLayoutInfo, nullptr, &boneLayout);

        VkDescriptorSetAllocateInfo boneAlloc{};
        boneAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        boneAlloc.descriptorPool = bonePool;
        boneAlloc.descriptorSetCount = 1;
        boneAlloc.pSetLayouts = &boneLayout;
        VkDescriptorSet boneDescSet;
        vkAllocateDescriptorSets(dev, &boneAlloc, &boneDescSet);

        VkDescriptorBufferInfo boneBufInfo{brainBoneBuffer, 0, boneSize};
        VkWriteDescriptorSet boneWrite{};
        boneWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        boneWrite.dstSet = boneDescSet;
        boneWrite.dstBinding = 0;
        boneWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        boneWrite.descriptorCount = 1;
        boneWrite.pBufferInfo = &boneBufInfo;
        vkUpdateDescriptorSets(dev, 1, &boneWrite, 0, nullptr);

        for (size_t i = 0; i < brainModel->primitives.size(); ++i) {
            auto& prim = brainModel->primitives[i];
            std::string name = "brain_" + std::to_string(i);
            engine.getMeshManager()->loadMesh(name, prim.mesh);

            TextureHandle texHandle{};
            if (!prim.texturePath.empty()) {
                texHandle = renderer->loadTexture(name + "_tex", prim.texturePath);
            }

            Material mat;
            mat.name = name;
            mat.textureHandle = texHandle;
            mat.color = prim.baseColor;
            mat.cullMode = CullMode::Disabled;
            materialHandles[mat.name] = mats->add(mat);

            auto* e = activeScene->createEntity(name);
            auto* mc = e->addComponent<MeshComponent>(name);
            mc->materialHandle = requireMaterialHandle(name);
            mc->boneDescriptorSet = boneDescSet; // Przypisanie deskryptora kości do komponentu

            // Tworzenie bufora wierzchołków przechowującego wagi i indeksy kości (Skinning Vertex Buffer)
            if (!prim.joints.empty()) {
                size_t skinSize = prim.joints.size() * (sizeof(uint32_t)*4 + sizeof(float)*4);
                VkBuffer svb; VkDeviceMemory sm;
                vk::createBuffer(dev, renderer->physicalDevice(), skinSize,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 svb, sm);
                std::vector<uint8_t> skinData(skinSize);
                for (size_t v = 0; v < prim.joints.size(); ++v) {
                    size_t off = v * (sizeof(uint32_t)*4 + sizeof(float)*4);
                    uint32_t jointIndices[4] = {
                        static_cast<uint32_t>(prim.joints[v][0]),
                        static_cast<uint32_t>(prim.joints[v][1]),
                        static_cast<uint32_t>(prim.joints[v][2]),
                        static_cast<uint32_t>(prim.joints[v][3])
                    };
                    memcpy(skinData.data() + off, jointIndices, sizeof(uint32_t)*4);
                    memcpy(skinData.data() + off + sizeof(uint32_t)*4, prim.weights[v].data(), sizeof(float)*4);
                }
                void* mapped;
                vkMapMemory(dev, sm, 0, skinSize, 0, &mapped);
                memcpy(mapped, skinData.data(), skinSize);
                vkUnmapMemory(dev, sm);
                mc->skinVertexBuffer = svb; // Drugi bufor wierzchołków bindowany podczas renderowania
            }

            e->transform().position = {-40.0f, 0.0f, 20.0f};
            e->transform().rotation = {0.0f, 0.0f, 0.0f};
            e->transform().scale = {10.0f, 10.0f, 10.0f};
        }
    }

    // Ustawienie głównej kamery i jej parametrów
    auto* camera = activeScene->createEntity("main_camera")->addComponent<CameraComponent>();
    camera->getOwner()->transform().position = {-60.0f, 15.0f, -62.0f};
    camera->target = {0.5f, 0.0f, 0.5f};
    camera->fieldOfViewDegrees = 50.0f;
    camera->nearPlane = 1.5f;
    camera->farPlane = 16000.0f;
    activeScene->setActiveCamera(camera);

    engine.getCameraController()->setPositionAndTarget(camera->getOwner()->transform().position, camera->target);

    // KROK 9: PĘTLA LOGIKI I STEROWANIA (Update loop callback)
    engine.onUpdate = [&, md2Model, md2State](float dt, Engine& e) {
        const auto& snap = e.input();
        static bool escWasDown = false;
        const bool escDown = snap.keys[SDL_SCANCODE_ESCAPE];
        if (escDown && !escWasDown) {
            SDL_Event event{};
            event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&event);
        }
        escWasDown = escDown;

        // Aktualizacja klatki animacji morphingowej MD2 na CPU i przesłanie nowej siatki do bufora GPU
        MeshData animMesh;
        animateMd2(*md2Model, *md2State, dt, animMesh);
        engine.getMeshManager()->updateMesh("goblin_md2", animMesh);

        // Pobranie danych wejściowych i aktualizacja kamery FPP (Flyaround/Free camera)
        auto* cc = e.getCameraController();
        auto* scene = e.getSceneManager()->activeScene();
        auto* cam = scene ? scene->activeCamera() : nullptr;
        if (cc && cam) {
            cc->moveSpeed = 15.0f;

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

            // Włączanie/wyłączanie światła i cieni klawiszem 'L'
            if (e.actionPressed(kActionToggleLighting)) {
                bool cur = e.isLightingEnabled();
                e.setLightingEnabled(!cur);
                e.setShadowsEnabled(!cur);
            }

            cc->update(dt, *cam);
        }

        // Rysowanie napisów ekranowych HUD (statystyki klatki, pozycja, oświetlenie)
        std::ostringstream oss;
        oss << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.setTextLabel(oss.str(), "times",
                                       e.getRenderer()->width() - 190.0f, 35.0f, 1.0f,
                                       {1.0f, 1.0f, 0.2f, 1.0f});

        {
            std::ostringstream lightTxt;
            lightTxt << "Oswietlenie [L]: " << (e.isLightingEnabled() ? "WLACZONE" : "WYLACZONE");
            e.setTextLabel(lightTxt.str(), "times", 10.0f, 90.0f, 0.7f,
                           e.isLightingEnabled() ? std::array<float,4>{0.2f, 1.0f, 0.3f, 1.0f}
                                                 : std::array<float,4>{1.0f, 0.3f, 0.3f, 1.0f});
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
            e.setTextLabel(look.str(), "times", 10.0f, 60.0f, 0.7f,
                                          {0.7f, 0.7f, 1.0f, 1.0f});
        }
    };

    // KROK 10: AKTUALIZACJA ANIMACJI SZKIELETOWEJ GLTF (GPU Skinning updates)
    // Łączymy logikę z poprzeniego lambda. Dla modelu BrainStem wyliczamy
    // położenia kości na CPU, a następnie przesyłamy zaktualizowaną tablicę macierzy
    // do bufora uniform na GPU (Vulkan dynamic uniform buffer mapping).
    engine.onUpdate = [origUpdate = std::move(engine.onUpdate), model = brainModel,
                      boneBuffer = brainBoneBuffer, boneMemory = brainBoneMemory,
                      dev = brainDev](float dt, Engine& e) mutable {
        static float animTime = 0.0f;
        animTime += dt;
        
        // Pobranie czasu trwania pierwszej animacji GLTF
        float dur = getAnimationDuration(*model, 0);
        if (dur > 0.001f) animTime = fmodf(animTime, dur);
        
        std::vector<Matrix4> jointMats;
        // Ewaluacja drzewa hierarchii kości dla danego czasu animacji
        evaluateAnimation(*model, 0, animTime, jointMats);
        
        static int dbgCount = 0;
        if (dbgCount == 0) {
            std::cout << "Anim frame: joints=" << jointMats.size() << " time=" << animTime << "\n";
            if (!jointMats.empty()) {
                auto& m = jointMats[0].values;
                std::cout << "  joint[0]: " << m[0] << " " << m[1] << " " << m[2] << " " << m[3] << "\n";
            }
        }
        dbgCount++;
        
        // Zaktualizowanie danych w pamięci GPU
        if (!jointMats.empty()) {
            void* mapped;
            vkMapMemory(dev, boneMemory, 0, sizeof(Matrix4) * jointMats.size(), 0, &mapped);
            memcpy(mapped, jointMats.data(), sizeof(Matrix4) * jointMats.size());
            vkUnmapMemory(dev, boneMemory);
        }
        
        if (origUpdate) origUpdate(dt, e);
    };

    // Uruchomienie głównej pętli gry
    engine.run();
    return 0;
}
