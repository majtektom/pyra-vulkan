#include "engine.h"
#include "renderer.h"
#include "mesh_component.h"
#include "transform_component.h"
#include "camera_component.h"
#include "model_3ds.h"
#include "material_system.h"

#include <chrono>
#include <cstdio>
#include <fstream>
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

    const std::string skyboxDir = "assets/skybox/noc";
    renderer->loadTexture("skybox_fb", skyboxDir + "/box_12.dds");
    renderer->loadTexture("skybox_lr", skyboxDir + "/box_34.dds");
    renderer->loadTexture("skybox_top", skyboxDir + "/box_5.dds");
    renderer->setSkyboxEnabled(true, skyboxDir);
    // renderer->setGridEnabled(true);

    renderer->loadFont("times", "assets/fonts/times.ttf", 48);

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

    auto* camera = engine.createEntity("main_camera")->addComponent<CameraComponent>();
    camera->position = {-60.0f, 15.0f, -62.0f};
    camera->fieldOfViewDegrees = 50.0f;
    camera->nearPlane = 1.5f;
    camera->farPlane = 16000.0f;
    engine.setActiveCamera(camera);

    engine.onUpdate = [&](float, Engine& e) {
        std::ostringstream oss;
        oss << "FPS: " << static_cast<int>(e.getCurrentFPS());
        e.getRenderer()->setTextLabel(oss.str(), "times",
                                       e.getRenderer()->width() - 190.0f, 90.0f, 1.0f,
                                       {1.0f, 1.0f, 0.2f, 1.0f});
    };

    engine.run();
    return 0;
}
