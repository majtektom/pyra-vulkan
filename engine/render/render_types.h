#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include "core/asset_registry.h"
#include "core/math.h"
#include "core/types.h"

namespace engine3d {

constexpr uint32_t kMaxTerrainLayers = 12;

struct VulkanPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

class MaterialInstance {
public:
    virtual ~MaterialInstance() = default;
    virtual void bind(VkCommandBuffer commandBuffer, uint32_t currentFrame) = 0;
    virtual const VulkanPipeline& getPipeline() const = 0;
    virtual bool isTransparent() const = 0;
    virtual TextureHandle getDiffuseHandle() const = 0;
    virtual float getBumpIntensity() const = 0;
    virtual std::array<float,4> getColor() const = 0;
    virtual int getRenderPriority() const = 0;
    virtual float getMetallicFactor() const = 0;
    virtual float getRoughnessFactor() const = 0;
    virtual TextureHandle getMetallicRoughnessHandle() const = 0;
    virtual TextureHandle getOcclusionHandle() const = 0;
    virtual TextureHandle getEmissiveHandle() const = 0;
    virtual TextureHandle getNormalHandle() const = 0;
    // Potok "tylko głębokość" dla przezroczystych materiałów z depthPrepass (nullptr = brak prepassu).
    virtual const VulkanPipeline* getDepthPrepassPipeline() const { return nullptr; }
};

// G-Buffer (rendering odroczony): 4 tekstury - albedo+metal, normalna+chropowatość, AO+emisja, pozycja świata.
// Pozycja w 32-bitowych floatach (w 16 bitach przy współrzędnych ~300 pojawiał się shadow acne).
constexpr int kGBufferAttachmentCount = 4;
inline VkFormat gbufferAttachmentFormat(int attachment) {
    return attachment == 3 ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
}

/**
 * Powierzchnia terenu: warstwy tekstur i mapa wysokości. Jedna na teren (albo siatkę z warstwami), wspólna
 * dla wszystkich jego paczek - paczka trzyma tylko wskaźnik, zamiast kopiować ~300 B danych w każdej klatce.
 * Indeksy bindless (część "uzupełnia renderer") ustala Renderer raz na klatkę na wątku renderowania.
 */
struct TerrainSurface {
    bool layerBlendEnabled = false;
    uint32_t layerCount = 0;
    std::array<TextureHandle, kMaxTerrainLayers> layerTextureHandles{};
    std::array<TextureHandle, kMaxTerrainLayers> layerMaskHandles{};
    float layerWorldSize = 1.0f;
    TextureHandle heightmapTextureHandle{};
    bool heightmapEnabled = false;
    float scaleY = 1.0f;
    // uzupełnia renderer:
    std::array<uint32_t, kMaxTerrainLayers> layerTextureIndices{};
    std::array<uint32_t, kMaxTerrainLayers> layerMaskIndices{};
    uint32_t heightmapTextureIndex = 0;
};

struct RenderPacket {
    VkBuffer vertexBuffer;
    VkBuffer indexBuffer;
    VkBuffer skinVertexBuffer = VK_NULL_HANDLE;
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    uint32_t instanceCount = 0;
    uint32_t indexCount;
    Matrix4 transform;
    std::shared_ptr<MaterialInstance> material;
    MaterialHandle materialHandle{};
    TextureHandle diffuseTextureHandle{};
    uint32_t diffuseTextureIndex = 0;
    TextureHandle normalTextureHandle{};
    uint32_t normalTextureIndex = 0;
    TextureHandle metallicRoughnessTextureHandle{};
    uint32_t metallicRoughnessTextureIndex = 0;
    TextureHandle occlusionTextureHandle{};
    uint32_t occlusionTextureIndex = 0;
    TextureHandle emissiveTextureHandle{};
    uint32_t emissiveTextureIndex = 0;
    std::shared_ptr<TerrainSurface> terrain;   // teren / siatka z warstwami tekstur (nullptr - zwykła siatka)
    float bumpIntensity = 1.0f;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    std::array<float,4> materialColor{1,1,1,1};
    float tessellationFactor = 1.0f;           // teren: teselacja węzła (zależna od jego LOD)
    VkDescriptorSet boneDescriptorSet = VK_NULL_HANDLE;
    float distanceToCamera = 0.0f;
    int renderPriority = 0;
    float windTime = 0.0f;
    float windStrength = 0.0f;
    bool forwardOpaquePass = false;
    // Własny shader materiału, który zapisuje G-Buffer (np. teren z teselacją) - rysowany w przebiegu G-Buffer
    // swoim potokiem; oświetlenie, cienie i mgłę liczy dla niego przebieg deferred.
    bool gbufferMaterialPass = false;
    bool castShadows = true;
    // Tylko do mapy cieni (nie jest rysowana na ekranie) - np. teren poza kadrem, który rzuca cień w kadr.
    bool shadowOnly = false;
    // Prostopadłościan otaczający w układzie świata (false = nieznany, np. instancje) - do dopasowania cieni.
    bool hasBounds = false;
    Vec3 boundsMin{}, boundsMax{};

    bool layerBlendEnabled() const { return terrain && terrain->layerBlendEnabled; }
    bool heightmapEnabled() const { return terrain && terrain->heightmapEnabled; }
};

// Granice w świecie: 8 narożników lokalnego prostopadłościanu przekształconych macierzą.
inline void transformBounds(const Matrix4& m, const Vec3& localMin, const Vec3& localMax, Vec3& outMin, Vec3& outMax) {
    outMin = {1e30f, 1e30f, 1e30f};
    outMax = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < 8; ++i) {
        const Vec3 c{(i & 1) ? localMax.x : localMin.x, (i & 2) ? localMax.y : localMin.y, (i & 4) ? localMax.z : localMin.z};
        const Vec3 w = transformPoint(m, c);
        outMin = {std::min(outMin.x, w.x), std::min(outMin.y, w.y), std::min(outMin.z, w.z)};
        outMax = {std::max(outMax.x, w.x), std::max(outMax.y, w.y), std::max(outMax.z, w.z)};
    }
}

struct RenderView {
    Matrix4 view;
    Matrix4 projection;
    Vec3 cameraPosition;
    Vec3 cameraRight;
    Vec3 cameraUp;
};

class MeshManager;

} // namespace engine3d
