#pragma once

#include <string>
#include <vector>
#include <memory>
#include <random>
#include <functional>

#include "scene/scene.h"
#include "core/types.h"
#include "render/render_types.h"

namespace engine3d {

class MeshManager;
class MaterialManager;
class MaterialSystem;

enum class VegetationType {
    Grass,
    Bush,
    Tree,
    Flower
};

struct VegetationSpecies {
    std::string name;
    VegetationType type;
    std::string meshName;
    MaterialHandle materialHandle{};
    float minScale = 0.8f;
    float maxScale = 1.5f;
    float density = 0.5f;
    float minHeight = 0.0f;
    float maxHeight = 1000.0f;
    float slopeMax = 45.0f;
    bool snapToTerrain = true;
};

class VegetationSystem : public ISceneSystem {
public:
    VegetationSystem(MeshManager& meshManager, MaterialManager& materialManager,
                     MaterialSystem& materialSystem);

    void update(Scene& scene, float deltaTime) override;
    const char* name() const override { return "VegetationSystem"; }

    void addSpecies(const VegetationSpecies& species);

    void generate(Scene& scene, float worldSize, float terrainCenterX, float terrainCenterZ,
                  std::function<float(float, float)> getHeightFunc);

    void generatePaired(Scene& scene, float worldSize, float terrainCenterX, float terrainCenterZ,
                        std::function<float(float, float)> getHeightFunc,
                        const VegetationSpecies& speciesA, const VegetationSpecies& speciesB);

    void setWindTime(float time) { windTime_ = time; }
    float getWindTime() const { return windTime_; }
    void setWindStrength(float strength) { windStrength_ = strength; }
    float getWindStrength() const { return windStrength_; }

    void clear();

    static MeshData createGrassBladeMesh(float width, float height);
    static MeshData createBushMesh(float radius, float height);

private:
    struct SpeciesInstance {
        VegetationSpecies species;
        std::vector<Vec3> positions;
        std::vector<float> scales;
        std::vector<float> rotations;
    };

    MeshManager& meshManager_;
    MaterialManager& materialManager_;
    MaterialSystem& materialSystem_;
    std::vector<SpeciesInstance> speciesInstances_;
    std::mt19937 rng_;
    float windTime_ = 0.0f;
    float windStrength_ = 0.3f;
};

} // namespace engine3d