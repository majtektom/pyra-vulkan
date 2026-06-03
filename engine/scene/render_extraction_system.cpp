#include "scene/render_extraction_system.h"

#include "core/entity.h"
#include "core/mesh_component.h"
#include "core/asset_registry.h"
#include "core/mesh_manager.h"
#include "render/material_manager.h"
#include "render/material_system.h"

namespace engine3d {

RenderExtractionSystem::RenderExtractionSystem(
    MaterialManager& materialManager,
    MaterialSystem& materialSystem,
    MeshManager& meshManager,
    AssetRegistry& assetRegistry)
    : materialManager_(materialManager),
      materialSystem_(materialSystem),
      meshManager_(meshManager),
      assetRegistry_(assetRegistry) {}

void RenderExtractionSystem::update(Scene& scene, float /*deltaTime*/) {
    // Krok 1: Czyścimy paczki geometrii wygenerowane w poprzedniej klatce.
    scene.clearRenderPackets();
    auto& packets = scene.mutableRenderPackets();

    // Krok 2: Iterujemy po wszystkich encjach na scenie.
    for (const auto& entity : scene.getEntities()) {
        if (!entity->isActive()) {
            continue; // Ignorujemy obiekty nieaktywne (np. wyłączone w hierarchii).
        }

        // Krok 3: Sprawdzamy, czy encja posiada MeshComponent (komponent do rysowania modelu 3D).
        auto* meshComponent = entity->getComponent<MeshComponent>();
        if (!meshComponent) {
            continue;
        }

        // Krok 4: Upewniamy się, że materiał powiązany z siatką został załadowany i przygotowany.
        resolveMaterialIfNeeded(*meshComponent);
        
        // Zapamiętujemy bieżącą liczbę paczek przed zbieraniem nowych, 
        // aby móc przetworzyć wyłącznie nowo dodane paczki dla tej encji.
        const auto previousPacketCount = packets.size();
        
        // Komponent siatki rozbija model na poszczególne części (sub-meshe) 
        // i dodaje ich paczki renderowania do wektora.
        meshComponent->collectRenderPackets(packets, meshManager_);

        // Krok 5: Wiążemy nowo wyekstrahowane paczki z zasobami.
        for (size_t packetIdx = previousPacketCount; packetIdx < packets.size(); ++packetIdx) {
            // Przypisujemy uchwyt materiału bezpośrednio do paczki.
            packets[packetIdx].materialHandle = meshComponent->materialHandle;
            
            // Pobieramy uchwyty GPU dla wszystkich tekstur (Diffuse, Normal, Metallic, Roughness, Occlusion, Emissive).
            // Przetłumaczenie nazw tekstur na uchwyty na tym etapie oszczędza czas procesora w rendererze niskopoziomowym.
            resolvePacketAssetHandles(packets[packetIdx]);
        }
    }
}

void RenderExtractionSystem::resolveMaterialIfNeeded(MeshComponent& meshComponent) {
    if (meshComponent.materialInstance) {
        return; // Materiał jest już w pełni zainicjalizowany.
    }

    const Material* material = meshComponent.materialHandle.isValid()
        ? materialManager_.get(meshComponent.materialHandle)
        : nullptr;

    if (!material) {
        return; // Brak przypisanego materiału.
    }

    // Pobieramy lub kompilujemy instancję materiału za pośrednictwem systemu materiałów.
    meshComponent.materialInstance = materialSystem_.getOrCreateMaterialInstance(*material);
}

void RenderExtractionSystem::resolvePacketAssetHandles(RenderPacket& packet) {
    if (!packet.material) {
        return;
    }

    // Pobieramy z rejestru zasobów (AssetRegistry) unikalne uchwyty GPU dla każdej tekstury PBR:
    
    // Tekstura koloru bazowego (albedo/diffuse):
    const auto& baseColorTextureName = packet.material->getTextureName();
    if (!baseColorTextureName.empty()) {
        packet.diffuseTextureHandle = assetRegistry_.registerTexture(baseColorTextureName);
    }

    // Tekstura normalnych (normal map - symulacja chropowatości powierzchni poprzez zaburzanie wektorów normalnych):
    const auto& normalTextureName = packet.material->getNormalMapName();
    if (!normalTextureName.empty()) {
        packet.normalTextureHandle = assetRegistry_.registerTexture(normalTextureName);
    }

    // Tekstura metaliczności i szorstkości (Metallic-Roughness):
    const auto& metallicRoughnessTextureName = packet.material->getMetallicRoughnessTextureName();
    if (!metallicRoughnessTextureName.empty()) {
        packet.metallicRoughnessTextureHandle = assetRegistry_.registerTexture(metallicRoughnessTextureName);
    }

    // Tekstura okluzji otoczenia (Ambient Occlusion - symulacja cieni w szczelinach geometrii):
    const auto& occlusionTextureName = packet.material->getOcclusionTextureName();
    if (!occlusionTextureName.empty()) {
        packet.occlusionTextureHandle = assetRegistry_.registerTexture(occlusionTextureName);
    }

    // Tekstura emisji światła (Emissive map - świecenie własne obiektu):
    const auto& emissiveTextureName = packet.material->getEmissiveTextureName();
    if (!emissiveTextureName.empty()) {
        packet.emissiveTextureHandle = assetRegistry_.registerTexture(emissiveTextureName);
    }
}

} // namespace engine3d