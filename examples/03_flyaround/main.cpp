#include "engine.h"
#include "renderer.h"
#include "mesh_component.h"
#include "transform_component.h"
#include "camera_component.h"
#include "model_3ds.h"
#include "model_md2.h"
#include "model_gltf.h"
#include "material_system.h"
#include "vulkan_util.h"
#include "skybox_renderer.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace engine3d;

bool textureHasAlpha(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    std::string ext = path.substr(path.find_last_of('.') + 1);
    for (auto& c : ext) c = char(tolower(c));

    if (ext == "dds") {
        char magic[4] = {}, header[124] = {};
        f.read(magic, 4);
        f.read(header, 124);
        if (std::string(magic, 4) != "DDS ") return false;
        auto read32 = [](const char* h, int off) -> uint32_t {
            return uint32_t(uint8_t(h[off])) | (uint32_t(uint8_t(h[off+1])) << 8)
                 | (uint32_t(uint8_t(h[off+2])) << 16) | (uint32_t(uint8_t(h[off+3])) << 24);
        };
        uint32_t pfFlags = read32(header, 76);
        if (pfFlags & 1) return true;
        if (pfFlags & 4) {
            uint32_t fourCC = read32(header, 80);
            return fourCC == 0x33545844 || fourCC == 0x35545844;
        }
        return false;
    }
    if (ext == "tga") {
        char tga[18] = {};
        f.read(tga, 18);
        return (tga[1] == 0 && (tga[2] == 2 || tga[2] == 10)) && tga[16] == 32;
    }
    return false;
}

int main() {
    Engine engine;

    if (!engine.initialize("engine3d: flyaround", 1024, 768)) {
        return 1;
    }

    auto* renderer = engine.getRenderer();
    auto* mats = engine.getMaterialManager();

    renderer->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    engine.setLightingEnabled(true);
    engine.setShadowsEnabled(false);
    engine.setAmbientStrength(0.35f);
    engine.setAmbientColor(0.45f, 0.48f, 0.55f);
    engine.addDirectionalLight(0.3f, 0.8f, 0.5f, 0.9f, 0.9f, 0.7f, 3.2f);

    const std::string skyboxDir = "assets/skybox/noc";
    // const std::string skyboxDir = "assets/skybox/dzien";
    renderer->loadTexture("skybox_fb", skyboxDir + "/box_12.dds");
    renderer->loadTexture("skybox_lr", skyboxDir + "/box_34.dds");
    renderer->loadTexture("skybox_top", skyboxDir + "/box_5.dds");

    auto skybox = std::make_shared<SkyboxRenderer>();
    SkyboxConfig skyboxCfg;
    skyboxCfg.enabled = true;
    skyboxCfg.directory = skyboxDir;
    skyboxCfg.size = 3500;
    skyboxCfg.verticalOffset = 100;
    skybox->configure(skyboxCfg, "bin/assets/shaders/skybox");
    skybox->setEnabled(true);
    skybox->setExposure(0.5f);
    renderer->addFeature(skybox);

    engine.loadFont("times", "assets/fonts/times.ttf", 48);

    renderer->loadTexture("drewno", "assets/textures/drewno.dds");
    renderer->loadTexture("cegla", "assets/textures/blacha.dds");
    renderer->loadTexture("metal", "assets/textures/metal.bmp");
    renderer->loadTexture("trawa", "assets/textures/trawa.dds");
    renderer->loadTexture("podloga", "assets/textures/piasek.bmp");

    auto addMat = [&](const std::string& name, const std::string& tex) {
        Material m; m.name = name; m.textureName = tex; mats->add(m);
    };
    addMat("drewno", "drewno");
    addMat("cegla", "cegla");
    addMat("metal", "metal");
    addMat("trawa", "trawa");
    addMat("podloga", "podloga");

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
        face(-s,-s, s,  s,-s, s,  s, s, s, -s, s, s,  0,0,1);
        face( s,-s,-s, -s,-s,-s, -s, s,-s,  s, s,-s, 0,0,-1);
        face(-s,-s,-s, -s,-s, s, -s, s, s, -s, s,-s, -1,0,0);
        face( s,-s, s,  s,-s,-s,  s, s,-s,  s, s, s,  1,0,0);
        face(-s, s, s,  s, s, s,  s, s,-s, -s, s,-s,  0,1,0);
        face(-s,-s,-s,  s,-s,-s,  s,-s, s, -s,-s, s,  0,-1,0);
        engine.getMeshManager()->loadMesh("cube", cube);
    }

    auto addObj = [&](const std::string& name, const std::string& meshName,
                      const std::string& matName, const Vec3& pos) {
        auto* e = engine.createEntity(name);
        auto* mc = e->addComponent<MeshComponent>(meshName);
        mc->materialName = matName;
        auto* tc = e->addComponent<TransformComponent>();
        tc->translation = pos;
        tc->scale = {2.0f, 2.0f, 2.0f};
    };

    addObj("floor", "floor", "podloga", {0,0,0});
    addObj("cube_drewno", "cube", "drewno", {0,1,0});
    addObj("cube_cegla", "cube", "cegla", {-5,1,-5});
    addObj("cube_metal", "cube", "metal", {5,1,5});
    addObj("cube_trawa", "cube", "trawa", {-5,1,5});
    addObj("cube_drewno2", "cube", "drewno", {5,1,-5});

    auto addModel = [&](const std::string& path, const std::string& prefix,
                        const Vec3& position, bool disableAlpha = false,
                        const std::string& fallbackTexture = "") {
        Model3ds model;
        if (!model.load(path)) {
            std::cerr << prefix << " load skipped: " << path << "\n";
            return;
        }
        std::cout << "Loaded " << prefix << ": " << model.objectList.size()
                  << " meshes, " << model.materialList.size() << " materials\n";

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
            std::string texPathForAlpha = mat.texturePath;
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
                    texPathForAlpha = fbPath;
                }
            }

            if (texExists) {
                renderer->loadTexture(matName, texPathToLoad);
            }

            // Użyj funkcji konwersji, zachowując cullMode z loadera
            Material modelMat = Convert3dsMaterialToEngineMaterial(mat);
            modelMat.name = matName;
            modelMat.textureName = texExists ? matName : "";
            // BlendMode i depthWrite mogą być nadpisane dla przezroczystości
            bool useAlpha = !disableAlpha && (mat.transparency < 0.99f || (texExists && textureHasAlpha(texPathForAlpha)));
            modelMat.blendMode = useAlpha ? BlendMode::Alpha : BlendMode::Disabled;
            modelMat.depthWrite = !useAlpha;
            // Materiały z alfą (liście itp.) renderuj dwustronnie jeśli model nie ma flagi twoSided
            if (useAlpha) {
                modelMat.cullMode = CullMode::Disabled;
            }
            mats->add(modelMat);

            finalMatNames[mi] = matName;
        }

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

            auto* e = engine.createEntity(meshName);
            auto* mc = e->addComponent<MeshComponent>(meshName);
            mc->materialName = matName;
            auto* tc = e->addComponent<TransformComponent>();
            tc->translation = position;
            tc->scale = {0.3f, 0.3f, 0.3f};
        }
    };

    addModel("assets/models/3ds/bronie/M4.3DS", "m4", {-20.0f, 10.0f, 0.0f}, true, "assets/models/3ds/bronie/m4.dds");
    addModel("assets/models/3ds/drzewko5.3DS", "DRZEWKO", {20.0f, 0.0f, 10.0f});
    addModel("assets/models/3ds/trawa/grass.3ds", "grass", {15.0f, 0.0f, 0.0f});
    addModel("assets/models/3ds/D1.3DS", "D1", {35.0f, 0.0f, 0.0f});

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

        renderer->loadTexture("goblin_tex", "assets/models/md2/goblin/goblin.bmp");
        renderer->loadTexture("goblin_dot3", "assets/models/md2/goblin/goblinDOT3.bmp");
        renderer->linkNormalMap("goblin_tex", "goblin_dot3");

        Material matGoblinBump;
        matGoblinBump.name = "goblin_bump";
        matGoblinBump.textureName = "goblin_tex";
        matGoblinBump.bumpIntensity = 1.0f;
        matGoblinBump.cullMode = CullMode::Disabled;
        mats->add(matGoblinBump);

        Material matGoblinFlat;
        matGoblinFlat.name = "goblin_flat";
        matGoblinFlat.textureName = "goblin_tex";
        matGoblinFlat.bumpIntensity = 0.0f;
        matGoblinFlat.cullMode = CullMode::Disabled;
        mats->add(matGoblinFlat);

        {
            auto* e = engine.createEntity("goblin_bump");
            auto* mc = e->addComponent<MeshComponent>("goblin_md2");
            mc->materialName = "goblin_bump";
            auto* tc = e->addComponent<TransformComponent>();
            tc->translation = {-50.0f, 7.0f, -5.0f};
            tc->rotation = {-90.0f, 0.0f, 0.0f};
            tc->scale = {0.4f, 0.4f, 0.4f};
        }
        {
            auto* e = engine.createEntity("goblin_flat");
            auto* mc = e->addComponent<MeshComponent>("goblin_md2");
            mc->materialName = "goblin_flat";
            auto* tc = e->addComponent<TransformComponent>();
            tc->translation = {-50.0f, 7.0f, -20.0f};
            tc->rotation = {-90.0f, 0.0f, 0.0f};
            tc->scale = {0.4f, 0.4f, 0.4f};
        }
    }

    {
        auto toyCar = loadGltfModel("assets/models/gLTF/toycar/bin/ToyCar.glb");
        std::cout << "Loaded ToyCar GLB: " << toyCar.primitives.size() << " primitives\n";

        for (size_t i = 0; i < toyCar.primitives.size(); ++i) {
            auto& prim = toyCar.primitives[i];
            std::string name = "toycar_" + std::to_string(i);
            engine.getMeshManager()->loadMesh(name, prim.mesh);

            std::string texName = name + "_tex";
            if (!prim.texturePath.empty()) renderer->loadTexture(texName, prim.texturePath);
            else texName = "metal";

            std::string nmName, ormName, occName, emName;
            if (!prim.normalMapPath.empty()) { nmName = name + "_n"; renderer->loadTexture(nmName, prim.normalMapPath); }
            if (!prim.ormPath.empty())        { ormName = name + "_orm"; renderer->loadTexture(ormName, prim.ormPath); }
            if (!prim.occlusionPath.empty())   { occName = name + "_occ"; renderer->loadTexture(occName, prim.occlusionPath); }
            if (!prim.emissivePath.empty())    { emName = name + "_em"; renderer->loadTexture(emName, prim.emissivePath); }

            Material mat;
            mat.name = name;
            mat.textureName = texName;
            mat.cullMode = CullMode::Disabled;
            if (!nmName.empty()) { mat.normalMapName = nmName; mat.bumpIntensity = 1.0f; }
            if (!ormName.empty()) mat.metallicRoughnessTextureName = ormName;
            if (!occName.empty()) mat.occlusionTextureName = occName;
            if (!emName.empty()) mat.emissiveTextureName = emName;
            mat.roughnessFactor = 0.5f;
            mats->add(mat);

            auto* e = engine.createEntity(name);
            auto* mc = e->addComponent<MeshComponent>(name);
            mc->materialName = name;
            auto* tc = e->addComponent<TransformComponent>();
            tc->translation = {-190.0f, 30.0f, 20.0f};
            tc->rotation = {0.0f, 0.0f, 0.0f};
            tc->scale = {0.2f, 0.2f, 0.2f};
        }
    }

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

        constexpr int kMaxBones = 64;
        VkDeviceSize boneSize = kMaxBones * sizeof(Matrix4);
        vk::createBuffer(dev, renderer->physicalDevice(), boneSize,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         brainBoneBuffer, brainBoneMemory);

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
            if (i < 3) std::cout << "  prim[" << i << "] verts=" << prim.mesh.vertices.size()
                                 << " tris=" << prim.mesh.indices.size()/3
                                 << " joints=" << (prim.joints.empty() ? "no" : "yes")
                                 << " tex=" << (prim.texturePath.empty() ? "none" : prim.texturePath)
                                 << " col=" << prim.mesh.vertices[0].color[0] << "," 
                                 << prim.mesh.vertices[0].color[1] << ","
                                 << prim.mesh.vertices[0].color[2] << "\n";

            std::string texName = name + "_tex";
            if (!prim.texturePath.empty()) renderer->loadTexture(texName, prim.texturePath);
            else texName = "";

            Material mat;
            mat.name = name;
            mat.textureName = texName;
            mat.color = prim.baseColor;
            mat.cullMode = CullMode::Disabled;
            mats->add(mat);

            auto* e = engine.createEntity(name);
            auto* mc = e->addComponent<MeshComponent>(name);
            mc->materialName = name;
            mc->boneDescriptorSet = boneDescSet;

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
                    memcpy(skinData.data() + off, prim.joints[v].data(), sizeof(uint32_t)*4);
                    memcpy(skinData.data() + off + sizeof(uint32_t)*4, prim.weights[v].data(), sizeof(float)*4);
                }
                void* mapped;
                vkMapMemory(dev, sm, 0, skinSize, 0, &mapped);
                memcpy(mapped, skinData.data(), skinSize);
                vkUnmapMemory(dev, sm);
                mc->skinVertexBuffer = svb;
            }

            auto* tc = e->addComponent<TransformComponent>();
            tc->translation = {-40.0f, 10.0f, 20.0f};
            tc->rotation = {0.0f, 0.0f, 0.0f};
            tc->scale = {10.0f, 10.0f, 10.0f};
        }
    }

    auto* camera = engine.createEntity("main_camera")->addComponent<CameraComponent>();
    camera->position = {-60.0f, 15.0f, -62.0f};
    camera->target = {0.5f, 0.0f, 0.5f};
    camera->fieldOfViewDegrees = 50.0f;
    camera->nearPlane = 1.5f;
    camera->farPlane = 16000.0f;
    engine.setActiveCamera(camera);

    // Inicjalizacja kontrolera kamery (tylko raz)
    engine.getCameraController()->setPositionAndTarget(camera->position, camera->target);

    engine.onUpdate = [&, md2Model, md2State](float dt, Engine& e) {
        MeshData animMesh;
        animateMd2(*md2Model, *md2State, dt, animMesh);
        engine.getMeshManager()->updateMesh("goblin_md2", animMesh);

        auto* cc = e.getCameraController();
        auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
        if (cc && cam) {
            cc->moveSpeed = 15.0f;

            auto snap = e.getPlatform()->pollInput();
            float ts = cc->mouseSensitivity;
            cc->rotateYaw(-static_cast<float>(snap.mouseDeltaX) * ts);
            cc->rotatePitch(-static_cast<float>(snap.mouseDeltaY) * ts);

            float vel = cc->moveSpeed * dt;
            if (snap.keys[SDL_SCANCODE_W]) cc->moveForward(vel);
            if (snap.keys[SDL_SCANCODE_S]) cc->moveForward(-vel);
            if (snap.keys[SDL_SCANCODE_A]) cc->moveRight(-vel);
            if (snap.keys[SDL_SCANCODE_D]) cc->moveRight(vel);
            if (snap.keys[SDL_SCANCODE_SPACE]) cc->moveUp(vel);
            if (snap.keys[SDL_SCANCODE_LCTRL]) cc->moveUp(-vel);

            static bool lWasDown = false;
            bool lDown = snap.keys[SDL_SCANCODE_L];
            if (lDown && !lWasDown) {
                bool cur = e.isLightingEnabled();
                e.setLightingEnabled(!cur);
                e.setShadowsEnabled(!cur);
            }
            lWasDown = lDown;

            cc->update(dt, *cam);
        }

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
                << cam->position.x << " " << cam->position.y << " " << cam->position.z;
            e.setTextLabel(pos.str(), "times", 10.0f, 30.0f, 0.7f,
                                          {0.7f, 0.7f, 1.0f, 1.0f});

            Vec3 dir = normalize(subtract(cam->target, cam->position));
            std::ostringstream look;
            look << "dir: " << std::fixed << std::setprecision(2)
                 << dir.x << " " << dir.y << " " << dir.z;
            e.setTextLabel(look.str(), "times", 10.0f, 60.0f, 0.7f,
                                          {0.7f, 0.7f, 1.0f, 1.0f});
        }
    };

    engine.onUpdate = [origUpdate = std::move(engine.onUpdate), model = brainModel,
                      boneBuffer = brainBoneBuffer, boneMemory = brainBoneMemory,
                      dev = brainDev](float dt, Engine& e) mutable {
        static float animTime = 0.0f;
        animTime += dt;
        std::vector<Matrix4> jointMats;
        evaluateAnimation(*model, 0, animTime, jointMats);
        static int dbgCount = 0;
        if (dbgCount == 0) {
            std::cout << "Anim frame: joints=" << jointMats.size() << " time=" << animTime << "\n";
            if (!jointMats.empty()) {
                auto& m = jointMats[0].values;
                std::cout << "  joint[0]: " << m[0] << " " << m[1] << " " << m[2] << " " << m[3] << "\n"
                          << "            " << m[4] << " " << m[5] << " " << m[6] << " " << m[7] << "\n";
            }
        }
        if (dbgCount == 120) std::cout << "Anim frame 120: joints=" << jointMats.size() << " time=" << animTime << "\n";
        dbgCount++;
        if (!jointMats.empty()) {
            void* mapped;
            vkMapMemory(dev, boneMemory, 0, sizeof(Matrix4) * jointMats.size(), 0, &mapped);
            memcpy(mapped, jointMats.data(), sizeof(Matrix4) * jointMats.size());
            vkUnmapMemory(dev, boneMemory);
        }
        if (origUpdate) origUpdate(dt, e);
    };

    engine.run();
    return 0;
}
