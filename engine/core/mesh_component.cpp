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

    // Uzywamy world matrix wyliczonej przez TransformSystem,
    // aby poprawnie wspierac hierarchie parent-child.
    packet.transform = owner->transform().getWorldMatrix();
    packet.castShadows = castShadows;
    // Instancje rozsiane po świecie i siatki animowane szkieletowo nie mają wiarygodnych granic.
    if (gpuMesh->hasBounds && !gpuMesh->instanceBuffer && skinVertexBuffer == VK_NULL_HANDLE) {
        packet.hasBounds = true;
        transformBounds(packet.transform, gpuMesh->boundsMin, gpuMesh->boundsMax, packet.boundsMin, packet.boundsMax);
    }
    packet.materialHandle = materialHandle;

    if (materialInstance) {
        packet.material = materialInstance;
        packet.bumpIntensity = materialInstance->getBumpIntensity();
        packet.materialColor = materialInstance->getColor();
        packet.renderPriority = materialInstance->getRenderPriority();
        packet.metallicFactor = materialInstance->getMetallicFactor();
        packet.roughnessFactor = materialInstance->getRoughnessFactor();
    }
    packet.skinVertexBuffer = skinVertexBuffer;
    packet.boneDescriptorSet = boneDescriptorSet;
    packet.instanceBuffer = gpuMesh->instanceBuffer ? gpuMesh->instanceBuffer->handle() : VK_NULL_HANDLE;
    packet.instanceCount = gpuMesh->instanceCount;
    if (layerBlendEnabled) {
        // Siatka z warstwami tekstur (np. teren z jednej siatki) - dane warstw we wspólnej strukturze.
        if (!surface_) surface_ = std::make_shared<TerrainSurface>();
        surface_->layerBlendEnabled = true;
        surface_->layerCount = std::min(layerCount, kMaxTerrainLayers);
        surface_->layerTextureHandles = layerTextureHandles;
        surface_->layerMaskHandles = layerMaskHandles;
        surface_->layerWorldSize = layerWorldSize;
        packet.terrain = surface_;
    }

    packets.push_back(std::move(packet));
}

} // namespace engine3d
