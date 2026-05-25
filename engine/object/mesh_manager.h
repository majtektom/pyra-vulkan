#pragma once

#include "backend/vulkan_context.h"
#include "backend/vulkan_resources.h"
#include "types.h"
#include <string>
#include <unordered_map>
#include <memory>

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
    
    void loadMesh(const std::string& name, const MeshData& meshData);
    void updateMesh(const std::string& name, const MeshData& meshData);
    void loadInstanceBuffer(const std::string& name, VkDeviceSize size);
    void updateInstanceBuffer(const std::string& name, const void* data, VkDeviceSize size, uint32_t instanceCount);
    std::shared_ptr<GpuMesh> getMesh(const std::string& name);

private:
    std::shared_ptr<VulkanContext> context_;
    std::unordered_map<std::string, std::shared_ptr<GpuMesh>> meshes_;
};

} // namespace engine3d
