#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include "core/asset_registry.h"
#include "render/backend/vulkan_context.h"
#include "render/backend/vulkan_resources.h"
#include "core/types.h"

namespace engine3d {

struct GpuMesh {
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
    std::unique_ptr<VulkanBuffer> instanceBuffer;
    uint32_t indexCount;
    uint32_t instanceCount = 0;
};

class MeshManager {
public:
    MeshManager(std::shared_ptr<VulkanContext> context);

    // Podpina rejestr typowanych uchwytow zasobow.
    void setAssetRegistry(AssetRegistry* registry) { assetRegistry_ = registry; }
    
    void loadMesh(const std::string& name, const MeshData& meshData);
    void updateMesh(const std::string& name, const MeshData& meshData);
    void loadInstanceBuffer(const std::string& name, VkDeviceSize size);
    void updateInstanceBuffer(const std::string& name, const void* data, VkDeviceSize size, uint32_t instanceCount);
    std::shared_ptr<GpuMesh> getMesh(const std::string& name);

    // Zwraca uchwyt mesha po nazwie, jesli mesh byl zarejestrowany.
    std::optional<MeshHandle> handle(const std::string& name) const;

private:
    std::shared_ptr<VulkanContext> context_;
    std::unordered_map<std::string, std::shared_ptr<GpuMesh>> meshes_;
    AssetRegistry* assetRegistry_ = nullptr;
};

} // namespace engine3d
