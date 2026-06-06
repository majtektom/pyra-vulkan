#include "scene/render_extraction_system.h"

#include "core/entity.h"
#include "core/mesh_component.h"
#include "core/asset_registry.h"
#include "core/mesh_manager.h"
#include "core/job_system.h"
#include "render/material_manager.h"
#include "render/material_system.h"
#include "render/renderer.h"

namespace engine3d {

RenderExtractionSystem::RenderExtractionSystem(
    MaterialManager& materialManager,
    MaterialSystem& materialSystem,
    MeshManager& meshManager,
    AssetRegistry& assetRegistry,
    JobSystem& jobs,
    const Renderer& renderer)
    : materialManager_(materialManager),
      materialSystem_(materialSystem),
      meshManager_(meshManager),
      assetRegistry_(assetRegistry),
      jobs_(jobs),
      renderer_(renderer) {}

void RenderExtractionSystem::update(Scene& scene, float /*deltaTime*/) {
    scene.clearRenderPackets();

    const auto& entities = scene.getEntities();
    const size_t entityCount = entities.size();
    if (entityCount == 0) return;

    // ------------------------------------------------------------------
    // Krok 1 (single-thread): Rozwiązywanie materiałów.
    // resolveMaterialIfNeeded() modyfikuje MeshComponent::materialInstance.
    // Musi być sekwencyjne – shared_ptr assignment nie jest thread-safe.
    // ------------------------------------------------------------------
    for (const auto& entity : entities) {
        if (!entity->isActive()) continue;
        auto* mc = entity->getComponent<MeshComponent>();
        if (!mc) continue;
        resolveMaterialIfNeeded(*mc);
    }

    // ------------------------------------------------------------------
    // Krok 2 (multi-thread): Równoległa ekstrakcja paczek renderowania.
    //
    // Każdy wątek roboczy JobSystem ma STAŁY indeks (0..N-1) przypisany raz
    // przy starcie puli. JobSystem::currentWorkerIndex() zwraca ten indeks.
    // To eliminuje wszelkie wyścigi przy przypisywaniu buforów lokalnych.
    //
    // Bezpieczeństwo wątkowe:
    //  a) localBuffers[i] – wyłącznie wątek i pisze do swojego bufora.
    //  b) getMesh() – tylko odczyt z mapy (brak zapisu w tym czasie).
    //  c) getWorldMatrix() – tylko odczyt (TransformSystem zakończył).
    //  d) materialInstance – tylko odczyt (Krok 1 zakończył).
    //  e) registerTexture() – thread-safe (shared_mutex w AssetRegistry).
    // ------------------------------------------------------------------
    const size_t nWorkers = jobs_.threadCount();
    // N buforów – po jednym na każdy wątek roboczy (główny wątek nie przetwarza chunków).
    std::vector<std::vector<RenderPacket>> localBuffers(nWorkers);
    {
        const size_t estPerBuf = std::max<size_t>(4, entityCount / nWorkers + 4);
        for (auto& buf : localBuffers) buf.reserve(estPerBuf);
    }

    jobs_.parallelFor(entityCount, [&](size_t begin, size_t end) {
        // Stały indeks wątku - nigdy nie zmienia się przez cały czas życia silnika.
        const size_t wIdx = JobSystem::currentWorkerIndex();

        // Defensywna walidacja: jeśli z jakiegoś powodu indeks jest poza zakresem,
        // pomijamy (nie powinno się zdarzyć - parallelFor wykonuje tylko wątki robocze).
        if (wIdx >= nWorkers) return;

        auto& myBuf = localBuffers[wIdx];

        for (size_t i = begin; i < end; ++i) {
            const auto& entity = entities[i];
            if (!entity->isActive()) continue;

            auto* mc = entity->getComponent<MeshComponent>();
            if (!mc || !mc->isActive()) continue;

            const size_t prevCount = myBuf.size();
            mc->collectRenderPackets(myBuf, meshManager_);

            for (size_t pi = prevCount; pi < myBuf.size(); ++pi) {
                myBuf[pi].materialHandle = mc->materialHandle;
                resolvePacketAssetHandles(myBuf[pi]);
                renderer_.prepareRenderPacket(myBuf[pi]);
            }
        }
    });

    // ------------------------------------------------------------------
    // Krok 3 (single-thread): Scalanie lokalnych buforów wątków.
    // ------------------------------------------------------------------
    auto& packets = scene.mutableRenderPackets();
    size_t total = 0;
    for (const auto& buf : localBuffers) total += buf.size();
    packets.reserve(total);
    for (auto& buf : localBuffers) {
        for (auto& p : buf) packets.push_back(std::move(p));
    }
}

void RenderExtractionSystem::resolveMaterialIfNeeded(MeshComponent& meshComponent) {
    if (meshComponent.materialInstance) return;

    const Material* material = meshComponent.materialHandle.isValid()
        ? materialManager_.get(meshComponent.materialHandle)
        : nullptr;
    if (!material) return;

    meshComponent.materialInstance = materialSystem_.getOrCreateMaterialInstance(*material);
}

void RenderExtractionSystem::resolvePacketAssetHandles(RenderPacket& packet) {
    if (!packet.material) return;

    packet.diffuseTextureHandle = packet.material->getDiffuseHandle();
    packet.normalTextureHandle = packet.material->getNormalHandle();
    packet.metallicRoughnessTextureHandle = packet.material->getMetallicRoughnessHandle();
    packet.occlusionTextureHandle = packet.material->getOcclusionHandle();
    packet.emissiveTextureHandle = packet.material->getEmissiveHandle();
}

} // namespace engine3d