#include "mesh_component.h"
#include "entity.h"
#include "transform_component.h"
#include "../object/mesh_manager.h"

namespace engine3d {

MeshComponent::MeshComponent() : Component("Mesh") {}

MeshComponent::MeshComponent(const std::string& meshName)
    : Component("Mesh"), meshName(meshName) {}

MeshComponent::MeshComponent(const MeshData& meshData)
    : Component("Mesh"), meshData(meshData) {}

void MeshComponent::collectRenderPackets(std::vector<RenderPacket>& packets, MeshManager& meshManager) {
    if (!active || !materialInstance) return;

    auto gpuMesh = meshManager.getMesh(meshName);
    if (!gpuMesh) return;

    RenderPacket packet;
    packet.vertexBuffer = gpuMesh->vertexBuffer ? gpuMesh->vertexBuffer->handle() : VK_NULL_HANDLE;
    packet.indexBuffer = gpuMesh->indexBuffer ? gpuMesh->indexBuffer->handle() : VK_NULL_HANDLE;
    packet.indexCount = gpuMesh->indexCount;

    if (auto* transform = owner->getComponent<TransformComponent>()) {
        packet.transform = transform->getModelMatrix();
    } else {
        packet.transform = Matrix4::identity();
    }

    packet.material = materialInstance;
    packet.textureName = materialInstance->getTextureName();
    
    packets.push_back(packet);
}

} // namespace engine3d
