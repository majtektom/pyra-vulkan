#pragma once

#include "types.h"
#include "math.h"
#include <cstdint>
#include <string>
#include <vector>

namespace engine3d {

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
};

GltfModel loadGltfModel(const std::string& path);
void evaluateAnimation(const GltfModel& model, int animIdx, float time, std::vector<Matrix4>& jointMatrices);
float getAnimationDuration(const GltfModel& model, int animIdx);

} // namespace engine3d
