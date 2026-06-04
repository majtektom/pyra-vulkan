#pragma once

#include "core/types.h"
#include "core/math.h"
#include "core/asset_registry.h"
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>


namespace engine3d {

class Renderer;
class MeshManager;
class MaterialManager;
class AssetRegistry;
class Scene;
class Entity;


struct GltfPrimitive {
    MeshData mesh;
    int materialIndex = -1;
    std::string materialName;
    std::string texturePath;
    std::string normalMapPath;
    std::string ormPath;
    std::string emissivePath;
    std::string occlusionPath;
    std::array<float, 4> baseColor{1,1,1,1};
    std::vector<std::array<uint8_t, 4>> joints;
    std::vector<std::array<float, 4>> weights;
};

struct GltfAnimChannel {
    int nodeIndex = -1;
    std::string path;
    std::vector<float> times;
    std::vector<float> values;
};

struct GltfAnimation {
    std::string name;
    std::vector<GltfAnimChannel> channels;
};

struct GltfSkin {
    std::vector<int> joints;
    std::vector<Matrix4> inverseBindMatrices;
};

struct GltfModel {
    std::vector<GltfPrimitive> primitives;
    std::vector<GltfAnimation> animations;
    std::vector<GltfSkin> skins;
    std::vector<Matrix4> nodeTransforms;
    std::vector<Matrix4> nodeLocalTransforms;
    std::vector<int> nodeParents;
    std::vector<Vec3> nodeTranslations;
    std::vector<Vec3> nodeScales;
};

GltfModel loadGltfModel(const std::string& path);
void evaluateAnimation(const GltfModel& model, int animIdx, float time, std::vector<Matrix4>& jointMatrices);
float getAnimationDuration(const GltfModel& model, int animIdx);

struct GltfPrimitiveInstanceInfo {
    std::string meshName;
    MaterialHandle materialHandle;
    VkBuffer skinVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory skinVertexMemory = VK_NULL_HANDLE;
};

class GltfSkinnedModel {
public:
    GltfSkinnedModel() = default;
    ~GltfSkinnedModel();

    GltfSkinnedModel(const GltfSkinnedModel&) = delete;
    GltfSkinnedModel& operator=(const GltfSkinnedModel&) = delete;

    bool load(Renderer* renderer, MeshManager* meshManager, MaterialManager* materialManager, AssetRegistry* assets, const std::string& path);
    void cleanup();

    const std::vector<GltfPrimitiveInstanceInfo>& getPrimitives() const { return primitives_; }
    const GltfModel& getModel() const { return model_; }

private:
    GltfModel model_;
    std::vector<GltfPrimitiveInstanceInfo> primitives_;
    VkDevice device_ = VK_NULL_HANDLE;
};

class GltfSkinnedInstance {
public:
    GltfSkinnedInstance() = default;
    ~GltfSkinnedInstance();

    GltfSkinnedInstance(const GltfSkinnedInstance&) = delete;
    GltfSkinnedInstance& operator=(const GltfSkinnedInstance&) = delete;
    
    GltfSkinnedInstance(GltfSkinnedInstance&& other) noexcept;
    GltfSkinnedInstance& operator=(GltfSkinnedInstance&& other) noexcept;

    bool initialize(Renderer* renderer, const GltfSkinnedModel& model);
    Entity* instantiate(Scene* scene, const std::string& namePrefix);
    void update(float dt, int animIdx = 0, float speed = 1.0f);
    void cleanup();

    Entity* getRootEntity() const { return rootEntity_; }
    float getAnimationTime() const { return animTime_; }
    void setAnimationTime(float time) { animTime_ = time; }

private:
    Renderer* renderer_ = nullptr;
    const GltfSkinnedModel* model_ = nullptr;
    VkBuffer boneBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory boneMemory_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet boneDescriptorSet_ = VK_NULL_HANDLE;

    Entity* rootEntity_ = nullptr;
    std::vector<Entity*> partEntities_;
    float animTime_ = 0.0f;
};

} // namespace engine3d

