#pragma once

#include <vector>
#include <string>
#include "core/types.h"

namespace engine3d {

struct Md2FrameData {
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
};

struct Md2Triangle {
    unsigned short vi[3];
    unsigned short ti[3];
};

struct Md2ModelData {
    std::vector<Md2FrameData> frames;
    std::vector<std::array<float, 2>> texCoords;
    std::vector<Md2Triangle> tris;
    MeshData triangles;
    float radius = 0.0f;
};

struct Md2AnimationState {
    int startFrame = 0;
    int endFrame = 0;
    float interpol = 0.0f;
    float speed = 10.0f;
    int currentFrame = -1;
    int nextFrame = -1;
};

Md2ModelData loadMd2Model(const std::string& path);
void animateMd2(const Md2ModelData& model, Md2AnimationState& state, float dt, MeshData& outMesh);

} // namespace engine3d
