#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <SDL3_image/SDL_image.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "core/math.h"
#include "core/entity.h"
#include "core/mesh_manager.h"
#include "camera/camera_component.h"
#include "camera/camera_picking.h"
#include "object/terrain/terrain.h"
#include "object/terrain/terrain_index_generator.h"
#include "object/terrain/terrain_heightmap_generator.h"

namespace engine3d {

// Mapa wysokości z obrazu (SDL3_image: PNG, BMP, JPG, TGA...). Obraz zamieniamy na RGBA (8 bitów na kanał),
// a wysokość to jasność piksela 0..255 (dla obrazu w skali szarości R = G = B).
static std::vector<float> loadHeightmap(const std::string& path, int& w, int& h) {
    SDL_Surface* loaded = IMG_Load(path.c_str());
    if (!loaded) throw std::runtime_error("Cannot load heightmap: " + path + " - " + SDL_GetError());
    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba) throw std::runtime_error("Cannot convert heightmap: " + path + " - " + SDL_GetError());
    w = rgba->w;
    h = rgba->h;
    std::vector<float> hm(static_cast<size_t>(w) * h);
    for (int z = 0; z < h; ++z) {
        const auto* row = static_cast<const unsigned char*>(rgba->pixels) + static_cast<size_t>(z) * rgba->pitch;
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = row + x * 4;
            hm[static_cast<size_t>(z) * w + x] = static_cast<float>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);   // jasność
        }
    }
    SDL_DestroySurface(rgba);
    return hm;
}



bool Terrain::loadFromImage(const std::string& path, float scaleXZ, float scaleY,
                             float texScale, int maxLodLevels) {
    int w, h;
    heightmap_ = loadHeightmap(path, w, h);
    hmSize_ = w;
    sizeX = w; sizeZ = h;
    scaleXZ_ = scaleXZ;
    scaleY_ = scaleY;
    texScale_ = texScale;
    maxLodLevels_ = maxLodLevels;

    float halfWorld = (w / 2.0f) * scaleXZ;
    if (root_) {
        deleteTree(root_);
        delete root_;
        visibleNodes_.clear();
    }
    root_ = new Node{};
    root_->cx = halfWorld;
    root_->cz = halfWorld;
    root_->sizeXZ = halfWorld;
    root_->minY = 0; root_->maxY = 256.0f * scaleY;
    root_->lodLevel = 0;

    gridRes_ = 16;
    buildTree(root_);
    buildNodeVertices(root_);
    findNeighbors(root_);
    nodeCount = 0;

    return true;
}

void Terrain::setScale(float scaleXZ, float scaleY, float texScale, int maxLodLevels) {
    scaleXZ_ = scaleXZ;
    scaleY_ = scaleY;
    texScale_ = texScale;
    maxLodLevels_ = maxLodLevels;
}

void Terrain::buildMesh() {
    int w = hmSize_;
    sizeX = w; sizeZ = w;
    float halfWorld = (w / 2.0f) * scaleXZ_;
    if (!root_) {
        root_ = new Node{};
        root_->cx = halfWorld;
        root_->cz = halfWorld;
        root_->sizeXZ = halfWorld;
        root_->minY = 0; root_->maxY = 256.0f * scaleY_;
    } else {
        deleteTree(root_);
        delete root_;
        visibleNodes_.clear();
        root_ = new Node{};
        root_->cx = halfWorld;
        root_->cz = halfWorld;
        root_->sizeXZ = halfWorld;
        root_->minY = 0; root_->maxY = 256.0f * scaleY_;
    }
    gridRes_ = 16;
    buildTree(root_);
    buildNodeVertices(root_);
    findNeighbors(root_);
    nodeCount = 0;
}

void Terrain::smoothEdges() {
    int S = hmSize_;
    if (S < 4) return;
    for (int y = 0; y < S; ++y) {
        heightmap_[y * S] = 0;
        heightmap_[y * S + 1] = heightmap_[y * S + 3] / 3.0f;
        heightmap_[y * S + 2] = heightmap_[y * S + 3] / 1.5f;

        heightmap_[S - 1 + y * S] = 0;
        heightmap_[S - 1 + y * S - 1] = heightmap_[S - 1 + y * S - 3] / 3.0f;
        heightmap_[S - 1 + y * S - 2] = heightmap_[S - 1 + y * S - 3] / 1.5f;

        heightmap_[y] = 0;
        heightmap_[y + S] = heightmap_[y + S * 3] / 3.0f;
        heightmap_[y + S * 2] = heightmap_[y + S * 3] / 1.5f;

        heightmap_[S * (S - 1) + y] = 0;
        heightmap_[S * (S - 1) + y - S] = heightmap_[S * (S - 1) + y - S * 3] / 3.0f;
        heightmap_[S * (S - 1) + y - S * 2] = heightmap_[S * (S - 1) + y - S * 3] / 1.5f;
    }
}

void Terrain::releaseMeshes(MeshManager& mgr) {
    if (meshName_.empty()) return;
    mgr.removeMesh(meshName_);
    mgr.removeMeshesWithPrefix(meshName_ + "_");   // węzły i indeksy LOD: <nazwa>_node_*, <nazwa>_indices_*
}

void Terrain::uploadTo(MeshManager& mgr) {
    if (meshName_.empty()) {
        static int id = 0;
        meshName_ = "terrain_" + std::to_string(++id);
    }
    meshManager_ = &mgr;
    
    // Assign unique IDs to leaf nodes
    int nextId = 0;
    assignNodeIds(root_, nextId);
    
    // Upload vertex buffers for all leaf nodes
    uploadNodeMeshes(root_, mgr, meshName_);
    
    // Upload shared LOD index buffers
    TerrainIndexGenerator indexGen(gridRes_);
    for (int lod = 0; lod < maxLodLevels_; ++lod) {
        for (int config = 0; config < 16; ++config) {
            std::vector<uint32_t> indices;
            indexGen.generate(lod, config, indices);
            
            MeshData indexMesh;
            indexMesh.indices = std::move(indices);
            
            std::string name = meshName_ + "_indices_lod" + std::to_string(lod) + "_config" + std::to_string(config);
            mgr.loadMesh(name, indexMesh);
        }
    }
}

Vec3 Terrain::worldOffset() const {
    const float halfWorld = (hmSize_ * 0.5f) * scaleXZ_;
    return Vec3{-halfWorld, 0.0f, -halfWorld};
}

// --- QuadTree construction ---

void Terrain::buildTree(Node* node) {
    float half = node->sizeXZ / 2.0f;
    int halfHm = (int)(half / scaleXZ_);
    if (halfHm < 16 || node->lodLevel >= maxLodLevels_) return;

    for (int i = 0; i < 4; ++i) {
        auto* child = new Node{};
        child->cx = node->cx + ((i == 1 || i == 3) ? half : -half);
        child->cz = node->cz + ((i < 2) ? half : -half);
        child->sizeXZ = half;
        child->lodLevel = node->lodLevel + 1;
        child->parent = node;

        int cxHm = (int)(child->cx / scaleXZ_);
        int czHm = (int)(child->cz / scaleXZ_);
        int hsHm = (int)(half / scaleXZ_);
        child->minY = 1e9f; child->maxY = -1e9f;
        int z0 = std::max(0, czHm - hsHm), z1 = std::min(hmSize_ - 1, czHm + hsHm);
        int x0 = std::max(0, cxHm - hsHm), x1 = std::min(hmSize_ - 1, cxHm + hsHm);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                float h = heightmap_[z * hmSize_ + x] * scaleY_;
                if (h < child->minY) child->minY = h;
                if (h > child->maxY) child->maxY = h;
            }
        node->children[i] = child;
        buildTree(child);
    }
    nodeCount++;
}

void Terrain::buildNodeVertices(Node* node) {
    // Left empty: vertices generated per-node in uploadNodeMeshes
}

void Terrain::findNeighbors(Node* node) {
    if (!node) return;
    float hs = node->sizeXZ;
    float cx = node->cx, cz = node->cz;
    // neighbor[0]: Left (West) -x
    Node* n = findNodeAt(root_, cx - hs * 2, cz); if (n) node->neighbor[0] = n;
    // neighbor[1]: Top (North) +z
    n = findNodeAt(root_, cx, cz + hs * 2); if (n) node->neighbor[1] = n;
    // neighbor[2]: Right (East) +x
    n = findNodeAt(root_, cx + hs * 2, cz); if (n) node->neighbor[2] = n;
    // neighbor[3]: Bottom (South) -z
    n = findNodeAt(root_, cx, cz - hs * 2); if (n) node->neighbor[3] = n;
    for (int i = 0; i < 4; ++i)
        if (node->children[i]) findNeighbors(node->children[i]);
}

Terrain::Node* Terrain::findNodeAt(Node* node, float cx, float cz) {
    if (!node) return nullptr;
    if (node == root_) {
        float maxBound = node->sizeXZ * 2.0f;
        if (cx < 0.0f || cx > maxBound || cz < 0.0f || cz > maxBound) {
            return nullptr;
        }
    }
    float eps = node->sizeXZ * 0.1f;
    if (std::abs(node->cx - cx) < eps && std::abs(node->cz - cz) < eps && !node->children[0])
        return node;
    if (node->children[0]) {
        int i = (cx > node->cx ? 1 : 0) + (cz < node->cz ? 2 : 0); // Corrected quadrant index!
        if (i >= 0 && i < 4) return findNodeAt(node->children[i], cx, cz);
    }
    if (!node->children[0]) return node;
    return nullptr;
}

// --- Per-frame update ---

namespace {
void collectLeaves(Terrain::Node* node, std::vector<Terrain::Node*>& leaves) {
    if (!node) return;
    if (node->children[0]) {
        for (int i = 0; i < 4; ++i) {
            collectLeaves(node->children[i], leaves);
        }
    } else {
        leaves.push_back(node);
    }
}
}

void Terrain::update(const CameraComponent* cam, MeshManager& meshManager, const std::string& meshPrefix) {
    if (!root_ || !cam) return;
    
    Vec3 camPos = cam->getOwner()->transform().position;
    updateLeafLods(root_, camPos);

    // Enforce neighbor LOD difference constraint (diff <= 1)
    // Symmetric relaxation: adjust both directions in one pass for faster convergence
    std::vector<Node*> leaves;
    collectLeaves(root_, leaves);
    bool changed = true;
    int maxIterations = 100; // safety limit
    while (changed && --maxIterations > 0) {
        changed = false;
        for (auto* node : leaves) {
            for (int i = 0; i < 4; ++i) {
                Node* neighbor = node->neighbor[i];
                if (!neighbor) continue;
                
                // If this node is too coarse (high LOD number), make it more detailed
                if (node->lodLevel > neighbor->lodLevel + 1) {
                    node->lodLevel = neighbor->lodLevel + 1;
                    changed = true;
                }
                // If neighbor is too coarse, make it more detailed (symmetric)
                if (neighbor->lodLevel > node->lodLevel + 1) {
                    neighbor->lodLevel = node->lodLevel + 1;
                    changed = true;
                }
            }
        }
    }

    visibleNodes_.clear();
    updateNodes(root_, cam);
    visibleCount = (int)visibleNodes_.size();

    // Cienie z terenu poza kadrem: liście niewidoczne, ale w zasięgu shadowCasterDistance_ od kamery.
    // Bez nich wzgórze za kamerą nie rzucało cienia w kadr, a cień pojawiał się i znikał przy obrocie kamery.
    shadowNodes_.clear();
    const Vec3 shadowCamPos = cam->getOwner()->transform().position;
    const Vec3 offset = worldOffset();
    for (auto* node : leaves) {
        if (node->visible) continue;
        const float dx = node->cx + offset.x - shadowCamPos.x, dz = node->cz + offset.z - shadowCamPos.z;
        const float reach = shadowCasterDistance_ + node->sizeXZ * 0.75f;
        if (dx * dx + dz * dz < reach * reach) shadowNodes_.push_back(node);
    }
}

void Terrain::updateNodes(Node* node, const CameraComponent* cam) {
    if (!node) return;
    node->visible = false;

    // Frustum check using sphere/distance test in world space
    Vec3 camPos = cam->getOwner()->transform().position;
    float radius = node->sizeXZ * 1.5f;
    bool inFrustum = true;
    {
        Vec3 offset = worldOffset();
        float nodeWorldX = node->cx + offset.x;
        float nodeWorldZ = node->cz + offset.z;
        float centerY = (node->minY + node->maxY) * 0.5f;
        Vec3 nodeWorldCenter{nodeWorldX, centerY, nodeWorldZ};
        
        Vec3 toNode = subtract(nodeWorldCenter, camPos);
        float dist = std::sqrt(dot(toNode, toNode));
        
        // 1. Zbyt daleko (poza farPlane kamery)
        if (dist > cam->farPlane + radius) {
            inFrustum = false;
        } else {
            // 2. Za kamerą (back-of-camera culling)
            Vec3 camForward = normalize(subtract(cam->target, camPos));
            if (dot(toNode, camForward) < -radius) {
                inFrustum = false;
            }
        }
    }

    if (!inFrustum) {
        return;
    }

    if (node->children[0]) {
        for (int i = 0; i < 4; ++i) {
            updateNodes(node->children[i], cam);
        }
    } else {
        node->visible = true;
        visibleNodes_.push_back(node);
    }
}


void Terrain::assignNodeIds(Node* node, int& nextId) {
    if (!node) return;
    if (node->children[0]) {
        for (int i = 0; i < 4; ++i) {
            assignNodeIds(node->children[i], nextId);
        }
    } else {
        node->meshId = nextId++;
    }
}

void Terrain::uploadNodeMeshes(Node* node, MeshManager& mgr, const std::string& prefix) {
    if (!node) return;
    if (node->children[0]) {
        for (int i = 0; i < 4; ++i) {
            uploadNodeMeshes(node->children[i], mgr, prefix);
        }
        return;
    }

    // Leaf node - generate and upload its vertices
    MeshData nodeMesh;
    nodeMesh.vertices.reserve((gridRes_ + 1) * (gridRes_ + 1));

    float hs = node->sizeXZ;
    int cxHm = (int)(node->cx / scaleXZ_);
    int czHm = (int)(node->cz / scaleXZ_);
    int hsHm = (int)(hs / scaleXZ_);

    // Generate vertices using transposed layout: Outer: x, Inner: z
    for (int x = 0; x <= gridRes_; ++x) {
        for (int z = 0; z <= gridRes_; ++z) {
            int ix = cxHm - hsHm + (x * hsHm * 2) / gridRes_;
            int iz = czHm - hsHm + (z * hsHm * 2) / gridRes_;
            ix = std::max(0, std::min(hmSize_ - 1, ix));
            iz = std::max(0, std::min(hmSize_ - 1, iz));
            float h = heightmap_[iz * hmSize_ + ix] * scaleY_;
            
            float px = (float)ix * scaleXZ_;
            float pz = (float)iz * scaleXZ_;
            
            nodeMesh.vertices.push_back({
                {px, h, pz}, {0,0,1}, {1,1,1},
                {(float)ix * texScale_ / hmSize_, (float)iz * texScale_ / hmSize_}
            });
        }
    }

    // Calculate normals using transposed layout
    for (int x = 0; x <= gridRes_; ++x) {
        for (int z = 0; z <= gridRes_; ++z) {
            int idx = x * (gridRes_ + 1) + z;
            float hL = (x > 0) ? nodeMesh.vertices[idx - (gridRes_ + 1)].position[1] : nodeMesh.vertices[idx].position[1];
            float hR = (x < gridRes_) ? nodeMesh.vertices[idx + (gridRes_ + 1)].position[1] : nodeMesh.vertices[idx].position[1];
            float hD = (z > 0) ? nodeMesh.vertices[idx - 1].position[1] : nodeMesh.vertices[idx].position[1];
            float hU = (z < gridRes_) ? nodeMesh.vertices[idx + 1].position[1] : nodeMesh.vertices[idx].position[1];
            float Y_comp = 2.0f * (16.0f / (float)gridRes_) * scaleXZ_;
            Vec3 n{hL - hR, Y_comp, hD - hU};
            n = normalize(n);
            nodeMesh.vertices[idx].normal = {n.x, n.y, n.z};
        }
    }

    std::string name = prefix + "_node_" + std::to_string(node->meshId);
    mgr.loadMesh(name, nodeMesh);
}

void Terrain::updateLeafLods(Node* node, const Vec3& camPos) {
    if (!node) return;
    if (node->children[0]) {
        for (int i = 0; i < 4; ++i) {
            updateLeafLods(node->children[i], camPos);
        }
    } else {
        Vec3 offset = worldOffset();
        float nodeWorldX = node->cx + offset.x;
        float nodeWorldZ = node->cz + offset.z;
        float centerY = (node->minY + node->maxY) * 0.5f;
        float dist = std::sqrt((camPos.x - nodeWorldX) * (camPos.x - nodeWorldX) +
                               (camPos.y - centerY) * (camPos.y - centerY) +
                               (camPos.z - nodeWorldZ) * (camPos.z - nodeWorldZ));
        
        // Chropowatość/nachylenie jako stosunek różnicy wysokości do rozmiaru węzła
        float heightDiff = node->maxY - node->minY;
        float slope = heightDiff / (node->sizeXZ + 0.1f);
        // Multiplier zwiększa odległość przejścia dla stromych/górzystych obszarów (maksymalnie 4.0x)
        float multiplier = std::min(1.0f + slope * 2.0f, 4.0f);

        node->lodLevel = 0;
        for (int pp = 0; pp < maxLodLevels_; ++pp) {
            float threshold = (node->sizeXZ * 2.0f * (pp + 1) + node->sizeXZ * 2.0f) * multiplier;
            if (dist < threshold) {
                break;
            }
            if (pp < maxLodLevels_ - 1) {
                node->lodLevel = pp + 1;
            }
        }
    }
}

void Terrain::collectRenderPackets(
    std::vector<RenderPacket>& packets,
    MeshManager& meshManager,
    const Matrix4& worldTransform,
    MaterialHandle materialHandle,
    std::shared_ptr<MaterialInstance> materialInstance,
    bool castShadows,
    bool layerBlendEnabled,
    uint32_t layerCount,
    const std::array<TextureHandle, kMaxTerrainLayers>& layerTextureHandles,
    const std::array<TextureHandle, kMaxTerrainLayers>& layerMaskHandles,
    float layerWorldSize,
    TextureHandle heightmapTextureHandle,
    bool heightmapEnabled,
    bool tessellationActive,
    float tessellationLevel) 
{
    // Jedna powierzchnia (warstwy, mapa wysokości) dla wszystkich węzłów - paczki trzymają tylko wskaźnik.
    if (!surface_) surface_ = std::make_shared<TerrainSurface>();
    surface_->layerBlendEnabled = layerBlendEnabled;
    surface_->layerCount = std::min(layerCount, kMaxTerrainLayers);
    surface_->layerTextureHandles = layerTextureHandles;
    surface_->layerMaskHandles = layerMaskHandles;
    surface_->layerWorldSize = layerWorldSize;
    surface_->heightmapTextureHandle = heightmapTextureHandle;
    surface_->heightmapEnabled = heightmapEnabled;
    surface_->scaleY = scaleY_;

    // Najpierw węzły widoczne, potem węzły tylko do cieni (poza kadrem).
    std::vector<Node*> nodes = visibleNodes_;
    const size_t visibleEnd = nodes.size();
    nodes.insert(nodes.end(), shadowNodes_.begin(), shadowNodes_.end());
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        Node* node = nodes[ni];
        std::string nodeMeshName = meshName_ + "_node_" + std::to_string(node->meshId);
        auto gpuMesh = meshManager.getMesh(nodeMeshName);
        if (!gpuMesh) continue;

        bool needLeftStitch = false, needRightStitch = false;
        bool needBottomStitch = false, needTopStitch = false;
        
        // Safety: enforce LOD diff <= 1 at render time (catches any remaining violations)
        for (int i = 0; i < 4; ++i) {
            if (node->neighbor[i] && node->neighbor[i]->lodLevel > node->lodLevel + 1) {
                node->neighbor[i]->lodLevel = node->lodLevel + 1;
            }
            if (node->neighbor[i] && node->lodLevel > node->neighbor[i]->lodLevel + 1) {
                node->lodLevel = node->neighbor[i]->lodLevel + 1;
            }
        }
        
        if (node->neighbor[0] && (node->neighbor[0]->lodLevel > node->lodLevel)) {
            needLeftStitch = true;
        }
        if (node->neighbor[1] && (node->neighbor[1]->lodLevel > node->lodLevel)) {
            needTopStitch = true;
        }
        if (node->neighbor[2] && (node->neighbor[2]->lodLevel > node->lodLevel)) {
            needRightStitch = true;
        }
        if (node->neighbor[3] && (node->neighbor[3]->lodLevel > node->lodLevel)) {
            needBottomStitch = true;
        }
        
        int stitchConfig = TerrainIndexGenerator::computeStitchConfig(
            needLeftStitch, needRightStitch, needBottomStitch, needTopStitch);

        int cpuLod = 0;
        float tessFactor = 1.0f;
        if (tessellationActive) {
            cpuLod = (node->lodLevel == 0) ? 0 : (node->lodLevel - 1);
            tessFactor = tessellationLevel / (1 << node->lodLevel);
            if (tessFactor < 1.0f) tessFactor = 1.0f;
        } else {
            cpuLod = node->lodLevel;
            tessFactor = 1.0f;
        }

        std::string indexMeshName = meshName_ + "_indices_lod" + std::to_string(cpuLod) + "_config" + std::to_string(stitchConfig);
        auto gpuIndexMesh = meshManager.getMesh(indexMeshName);
        if (!gpuIndexMesh) continue;

        RenderPacket packet;
        packet.vertexBuffer = gpuMesh->vertexBuffer ? gpuMesh->vertexBuffer->handle() : VK_NULL_HANDLE;
        packet.indexBuffer = gpuIndexMesh->indexBuffer ? gpuIndexMesh->indexBuffer->handle() : VK_NULL_HANDLE;
        packet.indexCount = gpuIndexMesh->indexCount;
        packet.transform = worldTransform;
        packet.castShadows = castShadows;
        packet.shadowOnly = ni >= visibleEnd;
        // Granice węzła (układ lokalny terenu, jak wierzchołki siatki) - do dopasowania kaskad cieni.
        {
            const float h = node->sizeXZ * 0.5f;
            packet.hasBounds = true;
            transformBounds(worldTransform, Vec3{node->cx - h, node->minY, node->cz - h}, Vec3{node->cx + h, node->maxY, node->cz + h},
                            packet.boundsMin, packet.boundsMax);
        }
        packet.materialHandle = materialHandle;
        packet.material = materialInstance;

        if (materialInstance) {
            packet.bumpIntensity = materialInstance->getBumpIntensity();
            packet.materialColor = materialInstance->getColor();
            packet.renderPriority = materialInstance->getRenderPriority();
            packet.metallicFactor = materialInstance->getMetallicFactor();
            packet.roughnessFactor = materialInstance->getRoughnessFactor();
        }

        packet.skinVertexBuffer = VK_NULL_HANDLE;
        packet.boneDescriptorSet = VK_NULL_HANDLE;
        packet.instanceBuffer = VK_NULL_HANDLE;
        packet.instanceCount = 0;
        packet.terrain = surface_;
        packet.tessellationFactor = tessFactor;   // teselacja zależna od LOD węzła

        // Teren teselowany rzuca cień przez potok terrain_shadow (renderer), z tej samej geometrii co na ekranie.
        packets.push_back(std::move(packet));
    }
}


Terrain::~Terrain() {
    visibleNodes_.clear();
    deleteTree(root_);
    delete root_;
    root_ = nullptr;
}

void Terrain::deleteTree(Node* node) {
    if (!node) return;
    for (int i = 0; i < 4; ++i) {
        if (node->children[i]) {
            deleteTree(node->children[i]);
            delete node->children[i];
            node->children[i] = nullptr;
        }
    }
}

// --- Height query ---

float Terrain::getHeight(float x, float z) const {
    if (hmSize_ == 0) return 0;
    float gx = (x / scaleXZ_) + hmSize_ / 2.0f;
    float gz = (z / scaleXZ_) + hmSize_ / 2.0f;
    int ix = (int)gx, iz = (int)gz;
    if (ix < 0 || ix >= hmSize_ - 1 || iz < 0 || iz >= hmSize_ - 1) return 0;
    float fx = gx - ix, fz = gz - iz;
    float h00 = heightmap_[iz * hmSize_ + ix], h10 = heightmap_[iz * hmSize_ + ix + 1];
    float h01 = heightmap_[(iz+1) * hmSize_ + ix], h11 = heightmap_[(iz+1) * hmSize_ + ix + 1];
    return ((h00 * (1-fx) + h10 * fx) * (1-fz) + (h01 * (1-fx) + h11 * fx) * fz) * scaleY_;
}

bool Terrain::containsXZ(float x, float z) const {
    if (hmSize_ == 0) return false;
    const float half = (hmSize_ * 0.5f - 1.0f) * scaleXZ_;
    return x > -half && x < half && z > -half && z < half;
}

Vec3 Terrain::getNormal(float x, float z) const {
    const float e = scaleXZ_;
    const float hl = getHeight(x - e, z);
    const float hr = getHeight(x + e, z);
    const float hd = getHeight(x, z - e);
    const float hu = getHeight(x, z + e);
    return normalize(Vec3{hl - hr, 2.0f * e, hd - hu});
}

bool Terrain::raycast(const Ray& ray, float maxDistance, Vec3& outHit) const {
    if (hmSize_ == 0) return false;

    // Krok marszu: pół komórki heightmapy - wystarczająco gęsto, by nie przeskoczyć wzgórza.
    const float step = std::max(0.25f, scaleXZ_ * 0.5f);
    float prevT = 0.0f;
    bool prevAbove = true;
    bool havePrev = false;

    for (float t = 0.0f; t <= maxDistance; t += step) {
        const Vec3 p = ray.at(t);
        if (!containsXZ(p.x, p.z)) {
            havePrev = false;
            continue;
        }
        const bool above = p.y > getHeight(p.x, p.z);
        if (havePrev && prevAbove && !above) {
            // Bisekcja w przedziale [prevT, t]
            float lo = prevT, hi = t;
            for (int i = 0; i < 12; ++i) {
                const float mid = 0.5f * (lo + hi);
                const Vec3 m = ray.at(mid);
                if (m.y > getHeight(m.x, m.z)) lo = mid; else hi = mid;
            }
            outHit = ray.at(hi);
            outHit.y = getHeight(outHit.x, outHit.z);
            return true;
        }
        if (!havePrev && !above) {
            // Start pod terenem (np. kamera w zboczu) - traktujemy jako trafienie.
            outHit = p;
            outHit.y = getHeight(p.x, p.z);
            return true;
        }
        prevT = t;
        prevAbove = above;
        havePrev = true;
    }
    return false;
}

void Terrain::setHeightmap(const std::vector<float>& data, int size) {
    if (size <= 0 || static_cast<int>(data.size()) < size * size) return;
    heightmap_.assign(data.begin(), data.begin() + static_cast<size_t>(size) * size);
    hmSize_ = size;
    sizeX = size;
    sizeZ = size;
}

// --- Generators (unchanged) ---

void Terrain::generateFault(int size, int iterations, int maxDelta, int minDelta, int itersPerFilter, float filter) {
    TerrainHeightmapGenerator::generateFault(size, iterations, maxDelta, minDelta,
                                              itersPerFilter, filter, heightmap_, hmSize_);
}

void Terrain::generatePlasma(int size, float roughness) {
    TerrainHeightmapGenerator::generatePlasma(size, roughness, heightmap_, hmSize_);
}

void Terrain::generateDeposition(int size, int jumps, int peakWalk, int minPart, int maxPart, float caldera) {
    TerrainHeightmapGenerator::generateDeposition(size, jumps, peakWalk,
                                                   minPart, maxPart, caldera, heightmap_, hmSize_);
}

bool Terrain::loadFromJson(const std::string& path) {
    std::ifstream f(path); if (!f) return false;
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto strVal = [&](const std::string& key) -> std::string {
        auto p = json.find("\"" + key + "\"");
        if (p == std::string::npos) return "";
        p = json.find(":", p); if (p == std::string::npos) return "";
        p++;
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n')) p++;
        if (json[p] == '"') {
            p++; auto e = json.find("\"", p); if (e == std::string::npos) return "";
            return json.substr(p, e - p);
        }
        if (json[p] == '-' || (json[p] >= '0' && json[p] <= '9')) {
            auto e = p; while (e < json.size() && (json[e] == '-' || json[e] == '.' || (json[e] >= '0' && json[e] <= '9'))) e++;
            return json.substr(p, e - p);
        }
        return "";
    };

    std::string hmFile = strVal("heightMap"); if (hmFile.empty()) return false;
    std::string baseDir = path;
    auto slash = baseDir.find_last_of("/\\"); if (slash != std::string::npos) baseDir = baseDir.substr(0, slash + 1);
    hmFile = baseDir + hmFile;

    float sxz = 10, sy = 2, ts = 20;
    std::string v = strVal("scaleMap"); if (!v.empty()) sxz = std::stof(v);
    v = strVal("scaleHeight"); if (!v.empty()) sy = std::stof(v);
    v = strVal("scaleTexture"); if (!v.empty()) ts = std::stof(v);

    auto layersStart = json.find("\"layers\"");
    if (layersStart != std::string::npos) {
        layersStart = json.find("[", layersStart); if (layersStart == std::string::npos) goto done;
        auto layersEnd = json.find("]", layersStart); if (layersEnd == std::string::npos) goto done;
        std::string layersStr = json.substr(layersStart + 1, layersEnd - layersStart - 1);

        size_t objStart = 0;
        while ((objStart = layersStr.find("{", objStart)) != std::string::npos) {
            auto objEnd = layersStr.find("}", objStart); if (objEnd == std::string::npos) break;
            std::string obj = layersStr.substr(objStart, objEnd - objStart + 1);

            auto getObjVal = [&](const std::string& o, const std::string& key) -> std::string {
                auto p = o.find("\"" + key + "\""); if (p == std::string::npos) return "";
                p = o.find(":", p); if (p == std::string::npos) return "";
                p++; while (p < o.size() && (o[p] == ' ' || o[p] == '\t')) p++;
                if (o[p] == '"') { p++; auto e = o.find("\"", p); if (e == std::string::npos) return ""; return o.substr(p, e - p); }
                return "";
            };

            TerrainLayer layer;
            layer.maskPath = baseDir + getObjVal(obj, "mask");
            layer.texturePath = baseDir + getObjVal(obj, "texture");
            if (!layer.maskPath.empty() && layer.texturePath != baseDir)
                layers.push_back(layer);
            objStart = objEnd + 1;
        }
    }
done:
    return loadFromImage(hmFile, sxz, sy, ts, 6);
}

namespace {
    int countLeavesHelper(const Terrain::Node* n) {
        if (!n) return 0;
        if (n->children[0]) {
            int c = 0;
            for (int i = 0; i < 4; ++i) c += countLeavesHelper(n->children[i]);
            return c;
        }
        return 1;
    }
}

int Terrain::getTotalLeafCount() const {
    return countLeavesHelper(root_);
}

} // namespace engine3d
