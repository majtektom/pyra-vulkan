#include "core/mesh_component.h"
#include "core/entity.h"
#include "core/mesh_manager.h"

namespace engine3d {

MeshComponent::MeshComponent() : Component("Mesh") {}

MeshComponent::MeshComponent(const std::string& meshName)
    : Component("Mesh"), meshName(meshName) {}

MeshComponent::MeshComponent(const MeshData& meshData)
    : Component("Mesh"), meshData(meshData) {}

void MeshComponent::collectRenderPackets(std::vector<RenderPacket>& packets, MeshManager& meshManager) {
    if (!isActive() || !owner) return;

    auto gpuMesh = meshManager.getMesh(meshName);
    if (!gpuMesh) return;

    RenderPacket packet;
    packet.vertexBuffer = gpuMesh->vertexBuffer ? gpuMesh->vertexBuffer->handle() : VK_NULL_HANDLE;
    packet.indexBuffer = gpuMesh->indexBuffer ? gpuMesh->indexBuffer->handle() : VK_NULL_HANDLE;
    packet.indexCount = gpuMesh->indexCount;

    packet.transform = owner->transform().getLocalMatrix();
    packet.castShadows = castShadows;

    if (materialInstance) {
        packet.material = materialInstance;
        packet.textureName = materialInstance->getTextureName();
        packet.bumpIntensity = materialInstance->getBumpIntensity();
        packet.materialColor = materialInstance->getColor();
        packet.renderPriority = materialInstance->getRenderPriority();
        packet.metallicFactor = materialInstance->getMetallicFactor();
        packet.roughnessFactor = materialInstance->getRoughnessFactor();
        packet.pbrMetallicRoughnessTextureName = materialInstance->getMetallicRoughnessTextureName();
        packet.pbrOcclusionTextureName = materialInstance->getOcclusionTextureName();
        packet.pbrEmissiveTextureName = materialInstance->getEmissiveTextureName();
        packet.normalMapName = materialInstance->getNormalMapName();
    }
    packet.skinVertexBuffer = skinVertexBuffer;
    packet.boneDescriptorSet = boneDescriptorSet;
    packet.instanceBuffer = gpuMesh->instanceBuffer ? gpuMesh->instanceBuffer->handle() : VK_NULL_HANDLE;
    packet.instanceCount = gpuMesh->instanceCount;
    
    packets.push_back(packet);
}

} // namespace engine3d
