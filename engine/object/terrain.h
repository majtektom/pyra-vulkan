#pragma once

#include "types.h"
#include "math.h"
#include "../render/render_feature.h"
#include <string>
#include <vector>
#include <array>
#include <vulkan/vulkan.h>

namespace engine3d {

class MeshManager;
class CameraComponent;
class Renderer;
class Engine;

struct TerrainLayer {
    std::string maskPath;
    std::string texturePath;
    std::string texName;
    std::string maskName;
};

class Terrain : public IRenderFeature {
public:
    Terrain() = default;
    ~Terrain() override;

    void init(Renderer* renderer) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    bool isActive() const override { return ready_; }
    const std::string& getName() const override { static std::string n = "Terrain"; return n; }
    Stage getStage() const override { return Stage::AfterOpaque; }

    bool loadFromImage(const std::string& path, float scaleXZ = 10.0f, float scaleY = 2.0f,
                       float texScale = 20.0f, int maxLodLevels = 5);
    bool loadFromJson(const std::string& path);

    void generateFault(int size, int iterations, int maxDelta, int minDelta, int itersPerFilter, float filter);
    void generatePlasma(int size, float roughness);
    void generateDeposition(int size, int jumps, int peakWalk, int minPart, int maxPart, float caldera);
    void buildMesh(float scaleXZ, float scaleY, float texScale);
    void smoothEdges();

    void uploadTo(MeshManager& mgr);
    void setupMultiTexture(Engine& engine);
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
        int lodLevel = 0;
        Node* children[4]{};
        Node* parent = nullptr;
        Node* neighbor[4]{};
        bool visible = false;
        int meshId = -1;
        int neighborLodDiff[4]{};
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
    void cleanupPipeline();

    std::vector<float> heightmap_;
    float scaleXZ_ = 1.0f, scaleY_ = 1.0f, texScale_ = 1.0f;
    int maxLodLevels_ = 5;
    int hmSize_ = 0;
    int gridRes_ = 0;
    Node* root_ = nullptr;
    std::vector<Node*> visibleNodes_;
    Renderer* renderer_ = nullptr;
    Engine* engine_ = nullptr;
    bool ready_ = false;
    int numLayers_ = 0;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};

} // namespace engine3d
