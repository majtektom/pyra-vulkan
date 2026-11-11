#pragma once

#include <functional>
#include <vector>
#include "render/render_types.h"

namespace engine3d {

/**
 * Paczki geometrii jednej klatki, podzielone na listy przebiegów.
 * Cienie: zamiast kopiować każdą rzucającą cień paczkę do osobnej listy (kopia RenderPacket = ~400 B
 * i licznik shared_ptr materiału), zapamiętujemy, gdzie leży (lista + indeks). Gdy cienie są wyłączone
 * (setCollectShadows(false)), lista cieni w ogóle nie powstaje.
 */
class RenderScene {
public:
    void clear() {
        opaquePackets.clear();
        transparentPackets.clear();
        shadowOnlyPackets.clear();
        shadowRefs.clear();
    }

    void setCollectShadows(bool collect) { collectShadows = collect; }

    void addPacket(RenderPacket packet) {
        const bool castShadows = collectShadows && packet.castShadows && packet.vertexBuffer != VK_NULL_HANDLE;
        if (packet.shadowOnly) {
            if (castShadows) {
                shadowRefs.push_back({ShadowList::ShadowOnly, static_cast<uint32_t>(shadowOnlyPackets.size())});
                shadowOnlyPackets.push_back(std::move(packet));
            }
            return;
        }
        const bool isTransparent = packet.material && packet.material->isTransparent();
        auto& list = isTransparent ? transparentPackets : opaquePackets;
        if (castShadows) {
            shadowRefs.push_back({isTransparent ? ShadowList::Transparent : ShadowList::Opaque,
                                  static_cast<uint32_t>(list.size())});
        }
        list.push_back(std::move(packet));
    }

    const std::vector<RenderPacket>& getOpaquePackets() const { return opaquePackets; }
    const std::vector<RenderPacket>& getTransparentPackets() const { return transparentPackets; }

    // Wszystkie paczki klatki (też "tylko cień") - np. do jednorazowego przygotowania wspólnych danych.
    template <typename F>
    void forEachPacket(F&& fn) const {
        for (const auto& p : opaquePackets) fn(p);
        for (const auto& p : transparentPackets) fn(p);
        for (const auto& p : shadowOnlyPackets) fn(p);
    }

    // Paczki rzucające cień - wskaźniki ważne do następnego clear()/addPacket (wołać po zebraniu paczek).
    size_t shadowPacketCount() const { return shadowRefs.size(); }
    void gatherShadowPackets(std::vector<const RenderPacket*>& out) const {
        out.clear();
        out.reserve(shadowRefs.size());
        for (const ShadowRef& r : shadowRefs) {
            const auto& list = r.list == ShadowList::Opaque ? opaquePackets
                               : r.list == ShadowList::Transparent ? transparentPackets : shadowOnlyPackets;
            out.push_back(&list[r.index]);
        }
    }

private:
    enum class ShadowList : uint8_t { Opaque, Transparent, ShadowOnly };
    struct ShadowRef {
        ShadowList list;
        uint32_t index;
    };
    std::vector<RenderPacket> opaquePackets;
    std::vector<RenderPacket> transparentPackets;
    std::vector<RenderPacket> shadowOnlyPackets;   // tylko cień (np. niewidoczny model rzucający cień)
    std::vector<ShadowRef> shadowRefs;
    bool collectShadows = true;
};

class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual void execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                         VkDescriptorSet globalDescriptorSet) = 0;
    virtual const std::string& getName() const = 0;
};

} // namespace engine3d
