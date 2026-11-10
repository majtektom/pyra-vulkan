#include <algorithm>
#include <cstring>
#include "render/vulkan_util.h"
#include "render/renderer.h"
#include "core/mesh_manager.h"

namespace engine3d {

static_assert(MeshManager::kBufferRetireFrames == Renderer::kMaxFrames + 1,
              "bufor musi przeleżeć o jedną klatkę dłużej, niż jest klatek w locie");

MeshManager::MeshManager(std::shared_ptr<VulkanContext> context) : context_(context) {}

void MeshManager::beginFrame() {
    ++frameIndex_;
    // Bufory zwolnione dawno temu i nieużyte ponownie (np. po zmianie rozmiaru) — zwalniamy pamięć.
    const uint64_t keepFrames = kBufferRetireFrames * 4;
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(), [&](const RetiredBuffer& r) {
        return r.releaseFrame + keepFrames < frameIndex_;
    }), retired_.end());
}

std::unique_ptr<VulkanBuffer> MeshManager::acquireBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    for (auto it = retired_.begin(); it != retired_.end(); ++it) {
        if (it->releaseFrame <= frameIndex_ &&
            it->buffer->size() == size && it->buffer->usage() == usage) {
            auto buffer = std::move(it->buffer);
            retired_.erase(it);
            return buffer;
        }
    }
    return std::make_unique<VulkanBuffer>(
        context_, size, usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

void MeshManager::retire(std::unique_ptr<VulkanBuffer> buffer) {
    if (!buffer) return;
    retired_.push_back({frameIndex_ + kBufferRetireFrames, std::move(buffer)});
}

void MeshManager::loadMesh(const std::string& name, const MeshData& meshData) {
    auto gpuMesh = std::make_shared<GpuMesh>();
    
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * meshData.vertices.size();
    if (vertexBufferSize > 0) {
        gpuMesh->vertexBuffer = std::make_unique<VulkanBuffer>(
            context_, vertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        gpuMesh->vertexBuffer->upload(meshData.vertices.data(), vertexBufferSize);
    }

    // Granice siatki (lokalne) - renderer dopasowuje do nich zakres kaskad cieni.
    if (!meshData.vertices.empty()) {
        Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        for (const auto& v : meshData.vertices) {
            mn = {std::min(mn.x, v.position[0]), std::min(mn.y, v.position[1]), std::min(mn.z, v.position[2])};
            mx = {std::max(mx.x, v.position[0]), std::max(mx.y, v.position[1]), std::max(mx.z, v.position[2])};
        }
        gpuMesh->hasBounds = true;
        gpuMesh->boundsMin = mn;
        gpuMesh->boundsMax = mx;
    }

    gpuMesh->indexCount = static_cast<uint32_t>(meshData.indices.size());
    if (!meshData.indices.empty()) {
        VkDeviceSize indexBufferSize = sizeof(uint32_t) * meshData.indices.size();
        gpuMesh->indexBuffer = std::make_unique<VulkanBuffer>(
            context_, indexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        gpuMesh->indexBuffer->upload(meshData.indices.data(), indexBufferSize);
    }
    
    // Siatka o tej nazwie mogła być już rysowana — jej bufory odkładamy, zamiast niszczyć od razu.
    if (auto old = meshes_.find(name); old != meshes_.end() && old->second) {
        retire(std::move(old->second->vertexBuffer));
        retire(std::move(old->second->indexBuffer));
        retire(std::move(old->second->instanceBuffer));
    }
    meshes_[name] = gpuMesh;
    if (assetRegistry_) {
        assetRegistry_->registerMesh(name);
    }
}

void MeshManager::updateMesh(const std::string& name, const MeshData& meshData) {
    auto it = meshes_.find(name);
    if (it == meshes_.end()) return;

    auto& gpuMesh = it->second;

    // Nie nadpisujemy bufora, z którego mogą czytać klatki w locie — zapis idzie do innego
    // bufora, a obecny wraca do puli dopiero po kBufferRetireFrames klatkach.
    VkDeviceSize vSize = sizeof(Vertex) * meshData.vertices.size();
    if (vSize > 0) {
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        const VkDeviceSize capacity = gpuMesh->vertexBuffer ? std::max(vSize, gpuMesh->vertexBuffer->size()) : vSize;
        auto next = acquireBuffer(capacity, usage);
        next->upload(meshData.vertices.data(), vSize);
        retire(std::move(gpuMesh->vertexBuffer));
        gpuMesh->vertexBuffer = std::move(next);
    }

    if (!meshData.indices.empty()) {
        VkDeviceSize iSize = sizeof(uint32_t) * meshData.indices.size();
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        const VkDeviceSize capacity = gpuMesh->indexBuffer ? std::max(iSize, gpuMesh->indexBuffer->size()) : iSize;
        auto next = acquireBuffer(capacity, usage);
        next->upload(meshData.indices.data(), iSize);
        retire(std::move(gpuMesh->indexBuffer));
        gpuMesh->indexBuffer = std::move(next);
        gpuMesh->indexCount = static_cast<uint32_t>(meshData.indices.size());
    }
}

std::shared_ptr<GpuMesh> MeshManager::getMesh(const std::string& name) {
    auto it = meshes_.find(name);
    return it != meshes_.end() ? it->second : nullptr;
}

std::optional<MeshHandle> MeshManager::handle(const std::string& name) const {
    if (!assetRegistry_) {
        return std::nullopt;
    }
    return assetRegistry_->meshHandle(name);
}

void MeshManager::loadInstanceBuffer(const std::string& name, VkDeviceSize size) {
    auto it = meshes_.find(name);
    if (it == meshes_.end()) return;

    auto& gpuMesh = it->second;
    retire(std::move(gpuMesh->instanceBuffer));
    gpuMesh->instanceBuffer = acquireBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void MeshManager::updateInstanceBuffer(const std::string& name, const void* data, VkDeviceSize size, uint32_t instanceCount) {
    auto it = meshes_.find(name);
    if (it == meshes_.end()) return;

    auto& gpuMesh = it->second;
    if (!gpuMesh->instanceBuffer) return;

    // Cząsteczki/sprite'y aktualizują instancje co klatkę — tak jak w updateMesh piszemy
    // do innego bufora o tej samej pojemności, a obecny odkładamy na kBufferRetireFrames klatek.
    if (data && size > 0) {
        auto next = acquireBuffer(gpuMesh->instanceBuffer->size(), gpuMesh->instanceBuffer->usage());
        next->upload(data, size);
        retire(std::move(gpuMesh->instanceBuffer));
        gpuMesh->instanceBuffer = std::move(next);
    }
    gpuMesh->instanceCount = instanceCount;
}

} // namespace engine3d
