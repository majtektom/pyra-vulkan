/**
 * engine_map.cpp - wczytywanie mapy z pliku JSON (Engine::loadMap).
 *
 * Plik mapy to obiekt JSON; każda sekcja jest opcjonalna i ma tu własną funkcję:
 *
 *   "ambient"              światło otoczenia (siła, kolor)            -> loadMapAtmosphere
 *   "fog"                  mgła odległościowa + mgła wolumetryczna    -> loadMapAtmosphere, loadMapVolumetricFog
 *   "skydome", "ocean"     niebo z chmurami i ocean                   -> loadMapSkyAndOcean
 *   "camera"               kamera startowa                            -> loadMapCamera
 *   "static_models_3ds"    statyczne modele 3DS (lista instancji      -> loadMap3dsModels
 *                          albo losowe rozmieszczenie na terenie)
 *   "characters_md2"       animowane postacie MD2                     -> loadMapMd2Characters
 *   "gltf_models"          statyczne modele glTF                      -> loadMapGltfModels
 *   "gltf_skinned_models"  modele glTF animowane szkieletem           -> loadMapSkinnedModels
 *
 * Wartości domyślne (drugi argument json.value(...)) obowiązują, gdy pola nie ma w pliku.
 */
#include "core/engine.h"

#include "core/entity.h"
#include "core/logger.h"
#include "core/mesh_component.h"
#include "object/gltf/model_gltf.h"
#include "object/model_3ds.h"
#include "object/model_md2.h"
#include "object/ocean_renderer.h"
#include "object/skydome_renderer.h"
#include "object/terrain/terrain.h"
#include "object/terrain/terrain_component.h"
#include "object/terrain/terrain_placement_component.h"
#include "render/renderer.h"
#include "scene/scene.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <random>

namespace engine3d {

namespace {

using json = nlohmann::json;

// Odczyt {"x":..,"y":..,"z":..} spod klucza; brak klucza albo pola = wartość domyślna.
Vec3 readVec3(const json& node, const char* key, const Vec3& fallback) {
    if (!node.contains(key)) return fallback;
    const json& v = node[key];
    return {v.value("x", fallback.x), v.value("y", fallback.y), v.value("z", fallback.z)};
}

// Odczyt {"r":..,"g":..,"b":..} spod klucza.
Vec3 readColor(const json& node, const char* key, const Vec3& fallback) {
    if (!node.contains(key)) return fallback;
    const json& c = node[key];
    return {c.value("r", fallback.x), c.value("g", fallback.y), c.value("b", fallback.z)};
}

// Część modelu złożonego z wielu siatek (każda z własnym materiałem).
struct SubMesh {
    std::string meshName;
    MaterialHandle material;
};

// Wspólne ustawienia instancji modelu z mapy: skala i opcjonalne "przyklejenie" do terenu.
struct InstanceSettings {
    Vec3 scale{1.0f, 1.0f, 1.0f};
    bool snapToTerrain = false;
    float terrainOffset = 0.0f;
};

InstanceSettings readInstanceSettings(const json& item) {
    InstanceSettings s;
    s.scale = readVec3(item, "scale", s.scale);
    s.snapToTerrain = item.value("snap_to_terrain", false);
    s.terrainOffset = item.value("terrain_offset", 0.0f);
    return s;
}

// Ustawia transformację korzenia instancji i dodaje przyklejenie do terenu.
void applyInstanceSettings(Entity* root, const InstanceSettings& settings, const Vec3& position, const Vec3& rotation) {
    root->transform().position = position;
    root->transform().rotation = rotation;
    root->transform().scale = settings.scale;
    if (settings.snapToTerrain) {
        root->addComponent<TerrainPlacementComponent>(settings.terrainOffset);
    }
}

// Tworzy instancję modelu: encja-korzeń z transformacją, a pod nią po jednej encji na każdą siatkę.
void spawnModelInstance(Scene* scene, const std::string& namePrefix, int instanceIndex, const std::string& modelPath,
                        const std::vector<SubMesh>& subMeshes, const InstanceSettings& settings,
                        const Vec3& position, const Vec3& rotation) {
    const std::string suffix = std::to_string(instanceIndex) + "_" + modelPath;
    Entity* root = scene->createEntity(namePrefix + "_root_" + suffix);
    applyInstanceSettings(root, settings, position, rotation);
    for (size_t i = 0; i < subMeshes.size(); ++i) {
        Entity* part = scene->createEntity(namePrefix + "_part_" + std::to_string(instanceIndex) + "_" +
                                           std::to_string(i) + "_" + modelPath);
        scene->setParent(part, root);
        part->addComponent<MeshComponent>(subMeshes[i].meshName)->materialHandle = subMeshes[i].material;
    }
}

std::shared_ptr<Terrain> findTerrain(Scene* scene) {
    for (const auto& entity : scene->getEntities()) {
        if (auto* tc = entity->getComponent<TerrainComponent>()) return tc->terrain;
    }
    return nullptr;
}

// Losowe pozycje na terenie (tylko tam, gdzie teren jest nie niżej niż minHeight).
std::vector<Vec3> randomTerrainPositions(const Terrain& terrain, int count, float minHeight) {
    const float maxBound = terrain.worldSizeXZ() / 2.0f;
    std::random_device seed;
    std::mt19937 gen(seed());
    std::uniform_real_distribution<float> coord(-maxBound, maxBound);

    std::vector<Vec3> positions;
    for (int attempts = 0; positions.size() < static_cast<size_t>(count) && attempts < 100000; ++attempts) {
        const float x = coord(gen);
        const float z = coord(gen);
        if (terrain.getHeight(x, z) >= minHeight) positions.push_back({x, 0.0f, z});
    }
    return positions;
}

} // namespace

bool Engine::loadMap(const std::string& path, Scene* scene) {
    if (!scene) {
        LOG_ERROR("loadMap: brak sceny");
        return false;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("nie można otworzyć pliku mapy: {}", path);
        return false;
    }
    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        LOG_ERROR("błąd w pliku mapy (JSON): {}", e.what());
        return false;
    }

    clearMapFeatures();
    loadMapAtmosphere(root);
    loadMapSkyAndOcean(root);
    loadMapVolumetricFog(root);
    loadMapCamera(root, scene);
    loadMap3dsModels(root, scene);
    loadMapMd2Characters(root, scene);
    loadMapGltfModels(root, scene);
    loadMapSkinnedModels(root, scene);

    LOG_INFO("Wczytano mapę z {}", path);
    return true;
}

// Usuwa elementy poprzedniej mapy (niebo, ocean, mgłę, modele animowane).
void Engine::clearMapFeatures() {
    if (renderer) {
        renderer->waitIdle();
        renderer->removeFeature("SkydomeRenderer");
        renderer->removeFeature("OceanRenderer");
        renderer->removeFeature("VolumetricFogRenderer");
    }
    currentBrainInstances_.clear();
    currentBrainModel_.reset();
    currentMd2Anim_.reset();
    currentMd2Model_.reset();
    md2Instances_.clear();
    currentOcean_.reset();
    currentSkydome_.reset();
    volumetricFogRenderer_.reset();
}

void Engine::loadMapAtmosphere(const nlohmann::json& root) {
    if (root.contains("ambient")) {
        const json& ambient = root["ambient"];
        setAmbientStrength(ambient.value("strength", 0.28f));
        if (ambient.contains("color")) {
            const Vec3 c = readColor(ambient, "color", {0.35f, 0.38f, 0.45f});
            setAmbientColor(c.x, c.y, c.z);
        }
    }
    if (root.contains("fog")) {
        const json& fog = root["fog"];
        setFogEnabled(fog.value("enabled", true));
        if (fog.contains("color")) {
            const Vec3 c = readColor(fog, "color", {0.575f, 0.68f, 0.88f});
            setFogColor(c.x, c.y, c.z);
        }
        setFogStart(fog.value("start", 150.0f));
        setFogEnd(fog.value("end", 3500.0f));
        setFogDensity(fog.value("density", 0.83f));
    }
}

void Engine::loadMapSkyAndOcean(const nlohmann::json& root) {
    if (root.contains("skydome") && root["skydome"].value("enabled", false)) {
        const json& node = root["skydome"];
        SkydomeConfig cfg;
        cfg.enabled = true;
        cfg.domeScale = node.value("domeScale", 1.6f);
        cfg.timeOfDay = node.value("timeOfDay", 12.0f);
        cfg.cloudCoverage = node.value("cloudCoverage", 0.35f);
        cfg.cloudSpeed0 = node.value("cloudSpeed0", 0.25f);
        cfg.cloudSpeed1 = node.value("cloudSpeed1", 0.12f);
        cfg.cloudScale0 = node.value("cloudScale0", 1200.0f);
        cfg.cloudScale1 = node.value("cloudScale1", 820.0f);
        cfg.sunElevation = node.value("sunElevation", 50.0f);
        cfg.enableSunLight = node.value("enableSunLight", true);
        currentSkydome_ = std::make_shared<SkydomeRenderer>();
        currentSkydome_->configure(cfg, "bin/assets/shaders/skydome", "skydome");
        currentSkydome_->setEnabled(true);
        renderer->addFeature(currentSkydome_);
    }

    if (root.contains("ocean") && root["ocean"].value("enabled", false)) {
        const json& node = root["ocean"];
        OceanConfig cfg;
        cfg.enabled = true;
        cfg.waterLevel = node.value("waterLevel", 20.0f);
        cfg.waveAmplitude = node.value("waveAmplitude", 2.3f);
        cfg.waveSpeed = node.value("waveSpeed", 0.9f);
        cfg.waveFrequency = node.value("waveFrequency", 9.0f);
        cfg.foamThreshold = node.value("foamThreshold", 15.9f);
        currentOcean_ = std::make_shared<OceanRenderer>();
        currentOcean_->configure(cfg, "bin/assets/shaders/ocean", "ocean");
        currentOcean_->setEnabled(true);
        renderer->addFeature(currentOcean_);
    }
}

// Mgła wolumetryczna (tylko gdy mgła jest włączona): gęstość maleje z wysokością, plus lokalne obszary
// gęstszej mgły ("volumes" - prostopadłościany z miękkimi brzegami).
void Engine::loadMapVolumetricFog(const nlohmann::json& root) {
    if (!root.contains("fog") || !root["fog"].value("enabled", true)) return;
    const json& fog = root["fog"];
    volumetricFogRenderer_ = std::make_shared<VolumetricFogRenderer>();
    volumetricFogRenderer_->configure("bin/assets/shaders/fog");
    volumetricFogRenderer_->setFogDensity(fog.value("volumetricDensity", 0.002f));
    volumetricFogRenderer_->setHeightFalloff(fog.value("heightFalloff", 0.004f));
    volumetricFogRenderer_->setSeaLevel(fog.value("seaLevel", 0.0f));
    volumetricFogRenderer_->setFogRadius(fog.value("radius", 1500.0f));
    if (fog.contains("color")) {
        const Vec3 c = readColor(fog, "color", {0.575f, 0.68f, 0.88f});
        volumetricFogRenderer_->setFogColor({c.x, c.y, c.z});
    }

    std::vector<LocalFogVolume> volumes;
    if (fog.contains("volumes") && fog["volumes"].is_array()) {
        for (const json& node : fog["volumes"]) {
            LocalFogVolume volume;
            const Vec3 center = readVec3(node, "center", {0.0f, 0.0f, 0.0f});
            const Vec3 half = readVec3(node, "halfExtents", {100.0f, 20.0f, 100.0f});
            if (node.contains("center")) volume.center = {center.x, center.y, center.z};
            if (node.contains("halfExtents")) volume.halfExtents = {half.x, half.y, half.z};
            volume.density = node.value("density", 0.004f);
            volume.edgeFade = node.value("edgeFade", 0.25f);
            volumes.push_back(volume);
        }
    }
    volumetricFogRenderer_->setLocalVolumes(std::move(volumes));
    volumetricFogRenderer_->setEnabled(true);
    renderer->addFeature(volumetricFogRenderer_);
}

void Engine::loadMapCamera(const nlohmann::json& root, Scene* scene) {
    if (!root.contains("camera")) return;
    const json& node = root["camera"];
    Entity* entity = scene->createEntity("game_camera");
    auto* camera = entity->addComponent<CameraComponent>();
    if (node.contains("position")) entity->transform().position = readVec3(node, "position", {-110.0f, 90.0f, -110.0f});
    if (node.contains("target")) camera->target = readVec3(node, "target", {0.0f, 28.0f, 0.0f});
    camera->fieldOfViewDegrees = node.value("fov", 55.0f);
    camera->nearPlane = node.value("near", 0.5f);
    camera->farPlane = node.value("far", 12000.0f);
    scene->setActiveCamera(camera);
}

// Statyczne modele 3DS. Materiały z pliku (z teksturą z pliku albo zapasową "texture"); "disableAlpha" - model
// rysowany jako nieprzezroczysty. Instancje z listy "instances" albo losowo na terenie ("random_instances").
void Engine::loadMap3dsModels(const nlohmann::json& root, Scene* scene) {
    if (!root.contains("static_models_3ds")) return;
    for (const json& item : root["static_models_3ds"]) {
        const std::string modelPath = item.value("model_path", "");
        Model3ds model;
        if (!model.load(modelPath)) {
            LOG_WARN("nie udało się wczytać 3DS: {}", modelPath);
            continue;
        }

        const std::string fallbackTexture = item.value("texture", "");
        const bool disableAlpha = item.value("disableAlpha", false);
        auto makeOpaque = [](Material& m) {
            m.blendMode = BlendMode::Disabled;
            m.depthWrite = true;
            m.cullMode = CullMode::Back;
        };

        std::vector<MaterialHandle> materials;
        for (size_t i = 0; i < model.materialList.size(); ++i) {
            const auto& src = *model.materialList[i];
            Material material = Convert3dsMaterialToEngineMaterial(src);
            material.name = "map_3ds_mat_" + std::to_string(i) + "_" + modelPath;
            if (disableAlpha) makeOpaque(material);
            const std::string texturePath = src.texturePath.empty() ? fallbackTexture : src.texturePath;
            if (!texturePath.empty()) {
                material.textureHandle = renderer->loadTexture(material.name + "_tex", texturePath, false, disableAlpha);
            }
            materials.push_back(materialManager.add(material));
        }
        if (materials.empty() && !fallbackTexture.empty()) {
            Material material;
            material.name = "map_3ds_mat_default_" + modelPath;
            material.textureHandle = renderer->loadTexture(material.name + "_tex", fallbackTexture, false, disableAlpha);
            makeOpaque(material);
            materials.push_back(materialManager.add(material));
        }

        std::vector<SubMesh> subMeshes;
        for (size_t i = 0; i < model.objectList.size(); ++i) {
            const auto& object = *model.objectList[i];
            const std::string meshName = "map_3ds_mesh_" + std::to_string(i) + "_" + modelPath;
            meshManager->loadMesh(meshName, object.mesh);
            MaterialHandle material{};
            if (!materials.empty()) {
                const int index = object.materialIndex;
                material = materials[(index >= 0 && index < static_cast<int>(materials.size())) ? index : 0];
            }
            subMeshes.push_back({meshName, material});
        }

        std::vector<Vec3> positions;
        const std::shared_ptr<Terrain> terrain = findTerrain(scene);
        if (item.contains("random_instances") && terrain) {
            const json& random = item["random_instances"];
            positions = randomTerrainPositions(*terrain, random.value("count", 0), random.value("min_height", -9999.0f));
        } else if (item.contains("instances")) {
            for (const json& instance : item["instances"]) {
                positions.push_back(readVec3(instance, "position", {0.0f, 0.0f, 0.0f}));
            }
        }

        const InstanceSettings settings = readInstanceSettings(item);
        for (size_t i = 0; i < positions.size(); ++i) {
            spawnModelInstance(scene, "map_3ds", static_cast<int>(i), modelPath, subMeshes, settings, positions[i],
                               {0.0f, 0.0f, 0.0f});
        }
    }
}

// Postacie MD2 (animacja klatkami kluczowymi - siatka liczona co klatkę w Engine::update).
void Engine::loadMapMd2Characters(const nlohmann::json& root, Scene* scene) {
    if (!root.contains("characters_md2")) return;
    for (const json& item : root["characters_md2"]) {
        const std::string name = item.value("name", "goblin");
        const std::string modelPath = item.value("model_path", "");
        currentMd2Model_ = std::make_shared<Md2ModelData>(loadMd2Model(modelPath));
        if (currentMd2Model_->frames.empty()) {
            LOG_WARN("nie udało się wczytać MD2: {}", modelPath);
            currentMd2Model_.reset();
            continue;
        }

        currentMd2Anim_ = std::make_shared<Md2AnimationState>();
        currentMd2Anim_->startFrame = 0;
        currentMd2Anim_->endFrame = static_cast<int>(currentMd2Model_->frames.size()) - 1;
        currentMd2Anim_->speed = item.value("anim_speed", 10.0f);

        const std::string meshName = name + "_mesh";
        meshManager->loadMesh(meshName, currentMd2Model_->triangles);

        Material material;
        material.name = name + "_mat";
        const std::string texturePath = item.value("texture_path", "");
        const std::string normalPath = item.value("normalmap_path", "");
        if (!texturePath.empty()) material.textureHandle = renderer->loadTexture(name + "_tex", texturePath);
        if (!normalPath.empty()) {
            material.normalMapHandle = renderer->loadTexture(name + "_norm", normalPath);
            material.bumpIntensity = 1.0f;
        }
        material.cullMode = CullMode::Back;
        material.blendMode = BlendMode::Disabled;
        material.depthWrite = true;

        Entity* entity = scene->createEntity(name + "_entity");
        entity->addComponent<MeshComponent>(meshName)->materialHandle = materialManager.add(material);
        entity->transform().position = readVec3(item, "position", {0.0f, 0.0f, 0.0f});
        entity->transform().rotation = readVec3(item, "rotation", {0.0f, 0.0f, 0.0f});
        entity->transform().scale = readVec3(item, "scale", {1.0f, 1.0f, 1.0f});
        if (item.value("snap_to_terrain", false)) {
            entity->addComponent<TerrainPlacementComponent>(item.value("terrain_offset", 0.0f));
        }
        md2Instances_.push_back({meshName, currentMd2Model_, currentMd2Anim_});
    }
}

// Statyczne modele glTF: każda siatka (primitive) dostaje materiał PBR z teksturami z pliku.
void Engine::loadMapGltfModels(const nlohmann::json& root, Scene* scene) {
    if (!root.contains("gltf_models")) return;
    for (const json& item : root["gltf_models"]) {
        const std::string modelPath = item.value("model_path", "");
        GltfModel model = loadGltfModel(modelPath);
        if (model.primitives.empty()) {
            LOG_WARN("nie udało się wczytać glTF: {}", modelPath);
            continue;
        }

        std::vector<SubMesh> subMeshes;
        for (size_t i = 0; i < model.primitives.size(); ++i) {
            const auto& primitive = model.primitives[i];
            const std::string meshName = "map_gltf_mesh_" + std::to_string(i) + "_" + modelPath;
            const std::string materialName = "map_gltf_mat_" + std::to_string(i) + "_" + modelPath;
            meshManager->loadMesh(meshName, primitive.mesh);

            Material material;
            material.name = materialName;
            material.cullMode = CullMode::Disabled;
            material.roughnessFactor = 0.5f;
            // Tekstura z pliku (pusta ścieżka = brak tej tekstury w modelu).
            auto texture = [&](const std::string& texturePath, const char* suffix) -> TextureHandle {
                return texturePath.empty() ? TextureHandle{} : renderer->loadTexture(materialName + suffix, texturePath);
            };
            if (!primitive.texturePath.empty()) material.textureHandle = texture(primitive.texturePath, "_tex");
            if (!primitive.normalMapPath.empty()) {
                material.normalMapHandle = texture(primitive.normalMapPath, "_n");
                material.bumpIntensity = 1.0f;
            }
            if (!primitive.ormPath.empty()) material.metallicRoughnessTextureHandle = texture(primitive.ormPath, "_orm");
            if (!primitive.occlusionPath.empty()) material.occlusionTextureHandle = texture(primitive.occlusionPath, "_occ");
            if (!primitive.emissivePath.empty()) material.emissiveTextureHandle = texture(primitive.emissivePath, "_em");
            subMeshes.push_back({meshName, materialManager.add(material)});
        }

        if (!item.contains("instances")) continue;
        const InstanceSettings settings = readInstanceSettings(item);
        int index = 0;
        for (const json& instance : item["instances"]) {
            spawnModelInstance(scene, "map_gltf", index++, modelPath, subMeshes, settings,
                               readVec3(instance, "position", {0.0f, 0.0f, 0.0f}),
                               readVec3(instance, "rotation", {0.0f, 0.0f, 0.0f}));
        }
    }
}

// Modele glTF animowane szkieletem: dane modelu wspólne, każda instancja ma własne macierze kości
// i przesunięty czas animacji (żeby nie ruszały się identycznie).
void Engine::loadMapSkinnedModels(const nlohmann::json& root, Scene* scene) {
    if (!root.contains("gltf_skinned_models")) return;
    currentBrainModel_ = std::make_unique<GltfSkinnedModel>();
    for (const json& item : root["gltf_skinned_models"]) {
        const std::string modelPath = item.value("model_path", "");
        if (!currentBrainModel_->load(renderer.get(), meshManager.get(), &materialManager, assetRegistry.get(), modelPath)) {
            LOG_WARN("nie udało się wczytać animowanego glTF: {}", modelPath);
            continue;
        }

        if (!item.contains("instances")) continue;
        const InstanceSettings settings = readInstanceSettings(item);
        int index = 0;
        for (const json& node : item["instances"]) {
            auto instance = std::make_unique<GltfSkinnedInstance>();
            if (instance->initialize(renderer.get(), *currentBrainModel_)) {
                Entity* rootEntity = instance->instantiate(scene, "map_skinned_" + std::to_string(index) + "_" + modelPath);
                applyInstanceSettings(rootEntity, settings, readVec3(node, "position", {0.0f, 0.0f, 0.0f}),
                                      readVec3(node, "rotation", {0.0f, 0.0f, 0.0f}));
                instance->setAnimationTime(static_cast<float>(index) * 0.7f);
                currentBrainInstances_.push_back(std::move(instance));
            }
            index++;
        }
    }
}

} // namespace engine3d
