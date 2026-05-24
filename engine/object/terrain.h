#pragma once

#include "types.h"
#include "math.h"
#include <string>
#include <vector>
#include <array>

namespace engine3d {

class MeshManager;
class CameraComponent;
class Renderer;
class Engine;
class MaterialManager;

struct TerrainLayer {
    std::string maskPath;
    std::string texturePath;
    std::string texName;
    std::string maskName;
};

class Terrain {
public:
    Terrain() = default;
    ~Terrain();

    bool loadFromImage(const std::string& path, float scaleXZ = 10.0f, float scaleY = 2.0f,
                       float texScale = 20.0f, int maxLodLevels = 5);
    bool loadFromJson(const std::string& path);

    void generateFault(int size, int iterations, int maxDelta, int minDelta, int itersPerFilter, float filter);
    void generatePlasma(int size, float roughness);
    void generateDeposition(int size, int jumps, int peakWalk, int minPart, int maxPart, float caldera);
    void buildMesh(float scaleXZ, float scaleY, float texScale);
    void smoothEdges();

    void uploadTo(MeshManager& mgr);
    void setupLayers(Renderer& renderer, Engine& engine, MaterialManager& mats);
    const std::string& meshName() const { return meshName_; }

    void update(const CameraComponent* cam, MeshManager& meshManager, const std::string& meshPrefix);
    void uploadMeshes(MeshManager& meshManager, const std::string& meshPrefix);

    float getHeight(float x, float z) const;

    int sizeX = 0, sizeZ = 0;
    int nodeCount = 0;
    int visibleCount = 0;
    MeshData mesh;
    std::vector<TerrainLayer> layers;
    std::string meshName_;

private:
    struct Node {
        float cx, cz;
        float sizeXZ;
        float minY, maxY;
        int lodLevel = 0;       // 0 = root, max = leaf
        Node* children[4]{};
        Node* parent = nullptr;
        Node* neighbor[4]{};    // 0=left, 1=right, 2=bottom, 3=top
        bool visible = false;
        int meshId = -1;
        int neighborLodDiff[4]{};  // LOD difference with each neighbor
        // Now global mesh, only offsets:
        int vertexOffset = 0;
        int indexOffset = 0;
        int indexCount = 0;
    };

    void buildTree(Node* node);
    void buildNodeVertices(Node* node);
    void findNeighbors(Node* node);
    Node* findNodeAt(Node* node, float cx, float cz);
    void updateNodes(Node* node, const CameraComponent* cam);
    void generateStitchingIndices(Node* node);
    void collectAndUpload(Node* node, MeshManager& mgr, const std::string& prefix, int& slot);
    void releaseTree(Node* node);
    void deleteTree(Node* node);

    std::vector<float> heightmap_;
    float scaleXZ_ = 1.0f, scaleY_ = 1.0f, texScale_ = 1.0f;
    int maxLodLevels_ = 5;
    int hmSize_ = 0;
    int gridRes_ = 0;  // resolution of leaf nodes
    Node* root_ = nullptr;
    std::vector<Node*> visibleNodes_;
};

} // namespace engine3d
