#include <algorithm>
#include <cstring>
#include "render/vulkan_util.h"
#include "render/renderer.h"
#include "core/mesh_manager.h"

namespace engine3d {

static_assert(MeshManager::kBufferRetireFrames == Renderer::kMaxFrames + 1,
              "bufor musi przeleżeć o jedną klatkę dłużej, niż jest klatek w locie");

MeshManager::MeshManager(std::shared_ptr<VulkanContext> context) : context_(context) {}

MeshManager::~MeshManager() {
    collectFinishedUploads(true);
}

std::unique_ptr<VulkanBuffer> MeshManager::createStaticBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    auto buffer = std::make_unique<VulkanBuffer>(context_, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    PendingCopy copy;
    copy.staging = std::make_unique<VulkanBuffer>(context_, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    copy.staging->upload(data, size);
    copy.dst = buffer->handle();
    copy.size = size;
    pending_.push_back(std::move(copy));
    return buffer;
}

/**
 * Wszystkie kopie zebrane w tej klatce (nowe siatki, przebudowane drogi itp.) idą jednym buforem poleceń.
 * Bariera na końcu: zapis kopii (TRANSFER) musi być widoczny dla odczytu wierzchołków/indeksów (VERTEX_INPUT).
 * Bariera obejmuje wszystkie późniejsze polecenia na tej samej kolejce - także render klatki, zgłoszony po nas.
 * Bufory pośrednie zwalniamy dopiero, gdy fence powie, że GPU skończyło kopiować.
 */
void MeshManager::flushUploads() {
    collectFinishedUploads(false);
    if (pending_.empty()) return;
    VkDevice device = context_->device();

    UploadBatch batch;
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = context_->commandPool();
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &alloc, &batch.cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(batch.cmd, &begin);
    for (auto& copy : pending_) {
        VkBufferCopy region{0, 0, copy.size};
        vkCmdCopyBuffer(batch.cmd, copy.staging->handle(), copy.dst, 1, &region);
        batch.staging.push_back(std::move(copy.staging));
    }
    vk::memoryBarrier(batch.cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                      VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT);
    vkEndCommandBuffer(batch.cmd);
    pending_.clear();

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(device, &fenceInfo, nullptr, &batch.fence);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &batch.cmd;
    vkQueueSubmit(context_->graphicsQueue(), 1, &submit, batch.fence);
    inFlight_.push_back(std::move(batch));
}

void MeshManager::collectFinishedUploads(bool wait) {
    VkDevice device = context_->device();
    for (auto it = inFlight_.begin(); it != inFlight_.end();) {
        if (wait) vkWaitForFences(device, 1, &it->fence, VK_TRUE, UINT64_MAX);
        if (vkGetFenceStatus(device, it->fence) != VK_SUCCESS) {
            ++it;
            continue;
        }
        vkDestroyFence(device, it->fence, nullptr);
        vkFreeCommandBuffers(device, context_->commandPool(), 1, &it->cmd);
        it = inFlight_.erase(it);   // bufory pośrednie zwalniają się razem z wpisem
    }
}

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
        if (it->releaseFrame <= frameIndex_ && it->buffer->hostVisible() &&
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
    // Siatka zastąpiona/usunięta, zanim jej dane poszły do GPU - kopia jest już zbędna.
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [&](const PendingCopy& c) { return c.dst == buffer->handle(); }),
                   pending_.end());
    retired_.push_back({frameIndex_ + kBufferRetireFrames, std::move(buffer)});
}

void MeshManager::loadMesh(const std::string& name, const MeshData& meshData) {
    auto gpuMesh = std::make_shared<GpuMesh>();
    
    VkDeviceSize vertexBufferSize = sizeof(Vertex) * meshData.vertices.size();
    if (vertexBufferSize > 0) {
        gpuMesh->vertexBuffer = createStaticBuffer(meshData.vertices.data(), vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
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
        gpuMesh->indexBuffer = createStaticBuffer(meshData.indices.data(), indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
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

void MeshManager::removeMesh(const std::string& name) {
    auto it = meshes_.find(name);
    if (it == meshes_.end()) return;
    if (it->second) {
        retire(std::move(it->second->vertexBuffer));
        retire(std::move(it->second->indexBuffer));
        retire(std::move(it->second->instanceBuffer));
    }
    meshes_.erase(it);
}

void MeshManager::removeMeshesWithPrefix(const std::string& prefix) {
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) {
            if (it->second) {
                retire(std::move(it->second->vertexBuffer));
                retire(std::move(it->second->indexBuffer));
                retire(std::move(it->second->instanceBuffer));
            }
            it = meshes_.erase(it);
        } else {
            ++it;
        }
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
