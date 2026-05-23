#include "mesh_manager.h"
#include "vulkan_util.h"
#include <cstring>

namespace engine3d {

MeshManager::MeshManager(std::shared_ptr<VulkanContext> context) : context_(context) {}

void MeshManager::loadMesh(const std::string& name, const MeshData& meshData) {
    auto gpuMesh = std::make_shared<GpuMesh>();
    
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * meshData.vertices.size();
    gpuMesh->vertexBuffer = std::make_unique<VulkanBuffer>(
        context_, vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT // Simplified for now
    );
    gpuMesh->vertexBuffer->upload(meshData.vertices.data(), vertexBufferSize);

    gpuMesh->indexCount = static_cast<uint32_t>(meshData.indices.size());
    if (!meshData.indices.empty()) {
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * meshData.indices.size();
        gpuMesh->indexBuffer = std::make_unique<VulkanBuffer>(
            context_, indexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        );
        gpuMesh->indexBuffer->upload(meshData.indices.data(), indexBufferSize);
    }
    
    meshes_[name] = gpuMesh;
}

void MeshManager::updateMesh(const std::string& name, const MeshData& meshData) {
    auto it = meshes_.find(name);
    if (it == meshes_.end()) return;

    auto& gpuMesh = it->second;
    
    VkDeviceSize vSize = sizeof(Vertex) * meshData.vertices.size();
    if (vSize > 0) {
        gpuMesh->vertexBuffer->upload(meshData.vertices.data(), vSize);
    }

    if (!meshData.indices.empty()) {
        VkDeviceSize iSize = sizeof(uint32_t) * meshData.indices.size();
        gpuMesh->indexBuffer->upload(meshData.indices.data(), iSize);
        gpuMesh->indexCount = static_cast<uint32_t>(meshData.indices.size());
    }
}

std::shared_ptr<GpuMesh> MeshManager::getMesh(const std::string& name) {
    auto it = meshes_.find(name);
    return it != meshes_.end() ? it->second : nullptr;
}

} // namespace engine3d
