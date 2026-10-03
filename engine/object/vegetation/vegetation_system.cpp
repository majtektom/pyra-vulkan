#include "object/vegetation/vegetation_system.h"
#include "core/entity.h"
#include "core/mesh_component.h"
#include "core/mesh_manager.h"
#include "object/terrain/terrain_placement_component.h"
#include "render/material_manager.h"
#include "render/material_system.h"
#include "core/logger.h"

#include <algorithm>
#include <cmath>

namespace engine3d {

VegetationSystem::VegetationSystem(MeshManager& meshManager, MaterialManager& materialManager,
                                   MaterialSystem& materialSystem)
    : meshManager_(meshManager), materialManager_(materialManager), materialSystem_(materialSystem) {
    rng_.seed(std::random_device{}());
}

void VegetationSystem::addSpecies(const VegetationSpecies& species) {
    SpeciesInstance si;
    si.species = species;
    speciesInstances_.push_back(si);
}

void VegetationSystem::clear() {
    speciesInstances_.clear();
}

MeshData VegetationSystem::createGrassBladeMesh(float width, float height) {
    MeshData mesh;
    float hw = width * 0.5f;

    mesh.vertices = {
        {{-hw, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        {{ hw, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{ hw, height, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-hw, height, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.0f, 0.0f, -hw}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        {{0.0f, 0.0f,  hw}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{0.0f, height,  hw}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, height, -hw}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};
    return mesh;
}

MeshData VegetationSystem::createBushMesh(float radius, float height) {
    MeshData mesh;

    auto addCross = [&](float y, float r) {
        float hw = r * 0.5f;
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({{-hw, y - height * 0.5f, 0.0f}, {0,0,1}, {1,1,1}, {0,1}});
        mesh.vertices.push_back({{ hw, y - height * 0.5f, 0.0f}, {0,0,1}, {1,1,1}, {1,1}});
        mesh.vertices.push_back({{ hw, y + height * 0.5f, 0.0f}, {0,0,1}, {1,1,1}, {1,0}});
        mesh.vertices.push_back({{-hw, y + height * 0.5f, 0.0f}, {0,0,1}, {1,1,1}, {0,0}});
        mesh.vertices.push_back({{0.0f, y - height * 0.5f, -hw}, {0,0,1}, {1,1,1}, {0,1}});
        mesh.vertices.push_back({{0.0f, y - height * 0.5f,  hw}, {0,0,1}, {1,1,1}, {1,1}});
        mesh.vertices.push_back({{0.0f, y + height * 0.5f,  hw}, {0,0,1}, {1,1,1}, {1,0}});
        mesh.vertices.push_back({{0.0f, y + height * 0.5f, -hw}, {0,0,1}, {1,1,1}, {0,0}});
        mesh.indices.push_back(base + 0); mesh.indices.push_back(base + 1); mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 2); mesh.indices.push_back(base + 3); mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 4); mesh.indices.push_back(base + 5); mesh.indices.push_back(base + 6);
        mesh.indices.push_back(base + 6); mesh.indices.push_back(base + 7); mesh.indices.push_back(base + 4);
    };

    addCross(0.0f, radius * 1.5f);
    addCross(height * 0.3f, radius * 1.2f);
    addCross(height * 0.6f, radius * 0.9f);

    return mesh;
}

void VegetationSystem::generate(Scene& scene, float worldSize, float terrainCenterX,
                                 float terrainCenterZ,
                                 std::function<float(float, float)> getHeightFunc) {
    std::uniform_real_distribution<float> posDist(-worldSize * 0.5f, worldSize * 0.5f);
    std::uniform_real_distribution<float> scaleDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);

    auto slopeAt = [&](float x, float z) -> float {
        float eps = 1.0f;
        float hx = getHeightFunc(x + eps, z);
        float hz = getHeightFunc(x, z + eps);
        float dx = (hx - getHeightFunc(x, z)) / eps;
        float dz = (hz - getHeightFunc(x, z)) / eps;
        return std::atan2(std::sqrt(dx * dx + dz * dz), 1.0f) * 180.0f / PI;
    };

    int entityIndex = 0;
    for (auto& si : speciesInstances_) {
        int count = static_cast<int>(si.species.density * worldSize * 0.08f);
        count = std::max(count, 1);

        LOG_INFO("Generating {} instances for '{}'", count, si.species.name);

        for (int i = 0; i < count; ++i) {
            float x = posDist(rng_) + terrainCenterX;
            float z = posDist(rng_) + terrainCenterZ;
            float y = getHeightFunc(x, z);

            if (y < si.species.minHeight || y > si.species.maxHeight) continue;

            float slope = slopeAt(x, z);
            if (slope > si.species.slopeMax) continue;

            float s = scaleDist(rng_);
            float scale = si.species.minScale + s * (si.species.maxScale - si.species.minScale);
            float rotY = rotDist(rng_);

            std::string entName = si.species.name + "_" + std::to_string(entityIndex++);
            Entity* entity = scene.createEntity(entName);
            entity->transform().position = {x, y, z};
            entity->transform().scale = {scale, scale, scale};
            entity->transform().rotation = {0.0f, rotY, 0.0f};

            auto* mc = entity->addComponent<MeshComponent>(si.species.meshName);
            mc->materialHandle = si.species.materialHandle;
            mc->castShadows = true;

            if (si.species.snapToTerrain) {
                entity->addComponent<TerrainPlacementComponent>(0.0f);
            }
        }
    }
}

void VegetationSystem::generatePaired(Scene& scene, float worldSize, float terrainCenterX, float terrainCenterZ,
                                       std::function<float(float, float)> getHeightFunc,
                                       const VegetationSpecies& speciesA, const VegetationSpecies& speciesB) {
    std::uniform_real_distribution<float> posDist(-worldSize * 0.5f, worldSize * 0.5f);
    std::uniform_real_distribution<float> scaleDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);

    auto slopeAt = [&](float x, float z) -> float {
        float eps = 1.0f;
        float hx = getHeightFunc(x + eps, z);
        float hz = getHeightFunc(x, z + eps);
        float dx = (hx - getHeightFunc(x, z)) / eps;
        float dz = (hz - getHeightFunc(x, z)) / eps;
        return std::atan2(std::sqrt(dx * dx + dz * dz), 1.0f) * 180.0f / PI;
    };

    int density = std::max(static_cast<int>(speciesA.density * worldSize * 0.08f), 1);
    int entityIndex = 0;

    LOG_INFO("Generating {} paired instances for '{}' / '{}'", density, speciesA.name, speciesB.name);

    for (int i = 0; i < density; ++i) {
        float x = posDist(rng_) + terrainCenterX;
        float z = posDist(rng_) + terrainCenterZ;
        float y = getHeightFunc(x, z);

        if (y < speciesA.minHeight || y > speciesA.maxHeight) continue;
        if (y < speciesB.minHeight || y > speciesB.maxHeight) continue;

        float slope = slopeAt(x, z);
        if (slope > speciesA.slopeMax) continue;
        if (slope > speciesB.slopeMax) continue;

        float s = scaleDist(rng_);
        float trunkScale = speciesA.minScale + s * (speciesA.maxScale - speciesA.minScale);
        float crownScale = speciesB.minScale + s * (speciesB.maxScale - speciesB.minScale);
        float rotY = rotDist(rng_);

        auto createEntity = [&](const VegetationSpecies& sp, float scale, const Vec3& pos) {
            std::string entName = sp.name + "_" + std::to_string(entityIndex);
            Entity* entity = scene.createEntity(entName);
            entity->transform().position = pos;
            entity->transform().scale = {scale, scale, scale};
            entity->transform().rotation = {0.0f, rotY, 0.0f};

            auto* mc = entity->addComponent<MeshComponent>(sp.meshName);
            mc->materialHandle = sp.materialHandle;
            mc->castShadows = true;

            if (sp.snapToTerrain) {
                entity->addComponent<TerrainPlacementComponent>(0.0f);
            }
        };

        createEntity(speciesA, trunkScale, {x, y, z});
        createEntity(speciesB, crownScale, {x, y + 10.0f * trunkScale, z});

        entityIndex++;
    }
}

void VegetationSystem::update(Scene& scene, float deltaTime) {
    windTime_ += deltaTime * 0.5f;
}

} // namespace engine3d