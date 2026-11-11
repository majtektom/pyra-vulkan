#include "render/renderer_impl.h"

#include <filesystem>

namespace engine3d {

// Mapy cieni: 4 kaskady słońca (warstwy 0-3) i atlas 2x2 świateł lokalnych (warstwa 4).
void Renderer::RendererImpl::recordShadowPass(VkCommandBuffer cmd, const RenderScene& scene) {
    // Tablica map cieni (5 warstw): z odczytu w shaderze (poprzednia klatka) na cel rysowania głębokości.
    vk::imageBarrier(cmd, {.image = shadowMapImage_,
                           .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                           .srcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .srcAccess = VK_ACCESS_2_NONE,
                           .dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                           .dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                           .layerCount = kShadowLayerCount});

    sortShadowDraws(scene);

    // 1. Kaskady słońca - każda w osobnej warstwie.
    for (const ShadowCaster& caster : shadowCasters_) {
        if (caster.atlasSlot >= 0) continue;
        for (uint32_t cascade = 0; cascade < caster.cascadeCount; ++cascade) {
            beginShadowLayer(cmd, caster.layer + cascade);
            setShadowViewport(cmd, -1);
            drawShadowCasters(cmd, storedLights_[caster.lightIndex].lightViewProj[cascade], Vec3{}, 0.0f, true);
            vkCmdEndRendering(cmd);
        }
    }

    // 2. Światła lokalne - wszystkie w jednej warstwie-atlasie, każde w swojej ćwiartce.
    beginShadowLayer(cmd, kLocalShadowLayer);
    for (const ShadowCaster& caster : shadowCasters_) {
        if (caster.atlasSlot < 0) continue;
        const LightData& l = storedLights_[caster.lightIndex];
        setShadowViewport(cmd, caster.atlasSlot);
        drawShadowCasters(cmd, l.lightViewProj[0], Vec3{l.position[0], l.position[1], l.position[2]}, l.params[2], false);
    }
    vkCmdEndRendering(cmd);

    // Mapy cieni gotowe: zapisy głębokości widoczne dla odczytu w shaderze oświetlenia.
    vk::imageBarrier(cmd, {.image = shadowMapImage_,
                           .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                           .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           .srcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                           .srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           .dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                           .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                           .layerCount = kShadowLayerCount});
}

// Kolejność rysowania cieni: raz na klatkę sortujemy paczki po rodzaju potoku (teren teselowany, zwykłe,
// animowane) i siatce - wtedy w każdej z 8 warstw (4 kaskady + 4 światła) kolejne paczki mają ten sam potok
// i bufory, a DrawBindCache pomija powtórne bindowanie. Głębokość nie jest sortowana: shader cienia jest prawie
// pusty, więc kolejność od przodu niewiele daje. Przy okazji liczymy raz środek i połowę rozmiaru granic (AABB)
// każdej paczki - test "czy paczka trafia w mapę cieni" robimy potem dla wszystkich warstw bez przeliczania.
void Renderer::RendererImpl::sortShadowDraws(const RenderScene& scene) {
    scene.gatherShadowPackets(shadowPacketPtrs_);
    shadowOrder_.clear();
    shadowOrder_.reserve(shadowPacketPtrs_.size());
    for (const RenderPacket* p : shadowPacketPtrs_) {
        ShadowDraw d;
        d.packet = p;
        if (p->heightmapEnabled() && terrainShadowPipeline_ && tessellationEnabled_ && p->indexBuffer != VK_NULL_HANDLE) {
            d.kind = ShadowDrawKind::TessellatedTerrain;
        } else if (p->boneDescriptorSet != VK_NULL_HANDLE) {
            d.kind = ShadowDrawKind::Skinned;
        }
        if (p->hasBounds) {
            d.center = multiply(add(p->boundsMin, p->boundsMax), 0.5f);
            d.extent = multiply(subtract(p->boundsMax, p->boundsMin), 0.5f);
        }
        shadowOrder_.push_back(d);
    }
    std::sort(shadowOrder_.begin(), shadowOrder_.end(), [](const ShadowDraw& a, const ShadowDraw& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.packet->vertexBuffer != b.packet->vertexBuffer) return a.packet->vertexBuffer < b.packet->vertexBuffer;
        return a.packet->indexBuffer < b.packet->indexBuffer;
    });
}

namespace {

// Czy granice paczki mogą trafić w mapę cieni widoku lightViewProj (x/y w [-1, 1] przestrzeni światła).
// Głębokości nie sprawdzamy: obiekt przed płaszczyzną światła i tak rzuca cień (depthClamp).
bool shadowBoundsVisible(const RenderPacket& packet, const Vec3& center, const Vec3& extent, const Matrix4& lightViewProj) {
    if (!packet.hasBounds) return true;
    const auto& m = lightViewProj.values;
    const bool ortho = m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f;
    if (ortho) {
        // Rzut ortogonalny (kaskady słońca): w = 1 dla każdego punktu, więc granice pudełka w przestrzeni
        // światła liczy się wprost ze środka i połowy rozmiaru: |M| * extent (zamiast 8 narożników).
        const float x = m[0] * center.x + m[4] * center.y + m[8] * center.z + m[12];
        const float y = m[1] * center.x + m[5] * center.y + m[9] * center.z + m[13];
        const float ex = std::abs(m[0]) * extent.x + std::abs(m[4]) * extent.y + std::abs(m[8]) * extent.z;
        const float ey = std::abs(m[1]) * extent.x + std::abs(m[5]) * extent.y + std::abs(m[9]) * extent.z;
        const float w = m[15];
        return !(x - ex > w || x + ex < -w || y - ey > w || y + ey < -w);
    }
    // Perspektywa (reflektor, światło punktowe): 8 narożników, każdy z własnym w. Niewidoczne, gdy wszystkie
    // narożniki leżą po tej samej stronie jednej krawędzi kadru.
    int outL = 0, outR = 0, outB = 0, outT = 0;
    for (int c = 0; c < 8; ++c) {
        const Vec3 corner{(c & 1) ? packet.boundsMax.x : packet.boundsMin.x,
                          (c & 2) ? packet.boundsMax.y : packet.boundsMin.y,
                          (c & 4) ? packet.boundsMax.z : packet.boundsMin.z};
        const float x = m[0] * corner.x + m[4] * corner.y + m[8] * corner.z + m[12];
        const float y = m[1] * corner.x + m[5] * corner.y + m[9] * corner.z + m[13];
        const float w = m[3] * corner.x + m[7] * corner.y + m[11] * corner.z + m[15];
        if (w <= 0.0f) continue;   // za "okiem" światła lokalnego - nie oceniamy
        outL += (x < -w) ? 1 : 0;
        outR += (x > w) ? 1 : 0;
        outB += (y < -w) ? 1 : 0;
        outT += (y > w) ? 1 : 0;
    }
    return !(outL == 8 || outR == 8 || outB == 8 || outT == 8);
}

} // namespace

// Rysuje obiekty rzucające cień do bieżącej warstwy (widok światła lightViewProj).
// cullRadius > 0 (światło lokalne): tylko obiekty w jego zasięgu.
void Renderer::RendererImpl::drawShadowCasters(VkCommandBuffer cmd, const Matrix4& lightViewProj, const Vec3& cullCenter,
                                               float cullRadius, bool sun) {
    DrawBindCache bound;   // nowa warstwa = nowe vkCmdBeginRendering - zaczynamy od pustej pamięci
    const float cullR2 = (cullRadius + 30.0f) * (cullRadius + 30.0f);
    for (const ShadowDraw& draw : shadowOrder_) {
        const RenderPacket& packet = *draw.packet;
        // Światło lokalne: najpierw tani test odległości (pozycja z macierzy świata; zapas 30 j. na rozmiar).
        if (cullRadius > 0.0f) {
            const Vec3 p{packet.transform.values[12], packet.transform.values[13], packet.transform.values[14]};
            const Vec3 d = subtract(p, cullCenter);
            if (dot(d, d) > cullR2) continue;
        }
        if (!shadowBoundsVisible(packet, draw.center, draw.extent, lightViewProj)) continue;
        drawShadowCaster(cmd, bound, draw, lightViewProj, sun);
    }
}

void Renderer::RendererImpl::drawShadowCaster(VkCommandBuffer cmd, DrawBindCache& bound, const ShadowDraw& draw,
                                              const Matrix4& lightViewProj, bool sun) {
    const RenderPacket& packet = *draw.packet;
    VkDescriptorSet globalSet = descriptorSets_[currentFrame_];

    // Teren teselowany: cień z tej samej geometrii co na ekranie (teselacja + mapa wysokości).
    if (draw.kind == ShadowDrawKind::TessellatedTerrain) {
        bound.bindGlobalSet(cmd, terrainShadowPipelineLayout_, globalSet);
        bound.bindPipeline(cmd, sun ? terrainShadowSunPipeline_ : terrainShadowPipeline_);
        bound.bindVertexBuffer(cmd, 0, packet.vertexBuffer);
        bound.bindIndexBuffer(cmd, packet.indexBuffer);
        TerrainShadowPushConstants push{};
        push.model = packet.transform;
        push.lightViewProj = lightViewProj;
        push.heightmapTextureSet = packet.terrain->heightmapTextureIndex;
        push.scaleY = packet.terrain->scaleY;
        push.tessellationFactor = packet.tessellationFactor;
        push.layerWorldSize = packet.terrain->layerWorldSize;
        vkCmdPushConstants(cmd, terrainShadowPipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                               VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, packet.indexCount, 1, 0, 0, 0);
        return;
    }

    // Zwykłe i animowane siatki (animowane mają set 1 z macierzami kości i drugi bufor wierzchołków).
    const bool skinned = draw.kind == ShadowDrawKind::Skinned;
    const VkPipelineLayout layout = skinned ? shadowSkinnedPipelineLayout_ : shadowPipelineLayout_;
    VkPipeline pipeline = skinned ? shadowSkinnedPipeline_ : shadowPipeline_;
    VkPipeline sunPipeline = skinned ? shadowSkinnedSunPipeline_ : shadowSunPipeline_;
    bound.bindGlobalSet(cmd, layout, globalSet);
    bound.bindPipeline(cmd, sun && sunPipeline ? sunPipeline : pipeline);
    if (skinned) bound.bindSet1(cmd, layout, packet.boneDescriptorSet);

    bound.bindVertexBuffer(cmd, 0, packet.vertexBuffer);
    if (packet.skinVertexBuffer != VK_NULL_HANDLE) bound.bindVertexBuffer(cmd, 1, packet.skinVertexBuffer);
    if (packet.indexBuffer == VK_NULL_HANDLE) return;
    bound.bindIndexBuffer(cmd, packet.indexBuffer);

    ShadowPushConstants push{};
    push.model = packet.transform;
    push.lightViewProj = lightViewProj;
    // Przezroczyste materiały (liście): piksele z małą alfą nie rzucają cienia.
    push.alphaCutoff = packet.material && packet.material->isTransparent() ? 0.5f : 0.0f;
    push.baseColorTextureSet = packet.diffuseTextureIndex;
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDrawIndexed(cmd, packet.indexCount, 1, 0, 0, 0);
}

// Rozpoczyna rysowanie do jednej warstwy tablicy cieni (z wyczyszczeniem całej warstwy).
void Renderer::RendererImpl::beginShadowLayer(VkCommandBuffer cmd, uint32_t layer) {
    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = shadowMapLayerViews_[layer];
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {kShadowMapSize, kShadowMapSize}};
    info.layerCount = 1;
    info.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &info);
}

// Obszar rysowania: cała warstwa (słońce, atlasSlot < 0) albo ćwiartka atlasu (światło lokalne).
void Renderer::RendererImpl::setShadowViewport(VkCommandBuffer cmd, int atlasSlot) {
    float x = 0.0f, y = 0.0f, size = static_cast<float>(kShadowMapSize);
    if (atlasSlot >= 0) {
        size *= 0.5f;
        x = static_cast<float>(atlasSlot % 2) * size;
        y = static_cast<float>(atlasSlot / 2) * size;
    }
    const VkViewport viewport{x, y, size, size, 0.0f, 1.0f};
    const VkRect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)},
                           {static_cast<uint32_t>(size), static_cast<uint32_t>(size)}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

/**
 * Podział kaskad (Practical Split Scheme): mieszanka podziału logarytmicznego (gęstsze kaskady
 * blisko kamery) i równomiernego, sterowana lambda. Kaskady obejmują głębokości widoku
 * [start, koniec], gdzie start = max(near kamery, dirShadowStart_) - kamera RTS widzi teren
 * dopiero z pewnej odległości, więc nie marnujemy rozdzielczości na pustą przestrzeń.
 * Wynik: dirShadowSplitDepths_[i] = dalsza granica kaskady i (głębokość w przestrzeni widoku).
 */
void Renderer::RendererImpl::computeCascadeSplits(float nearDepth, float startDepth, float farDepth) {
    const float start = std::max(nearDepth, std::min(startDepth, farDepth - 1.0f));
    const float end = std::max(start + 1.0f, farDepth);
    dirShadowSplitStart_ = start;
    for (int i = 0; i < kShadowCascades; ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(kShadowCascades);
        const float logSplit = std::max(start, 0.1f) * std::pow(end / std::max(start, 0.1f), p);
        const float uniformSplit = start + (end - start) * p;
        dirShadowSplitDepths_[i] = dirShadowLambda_ * logSplit + (1.0f - dirShadowLambda_) * uniformSplit;
    }
    dirShadowSplitDepths_[kShadowCascades - 1] = end;
}

/**
 * Zakres głębokości (w przestrzeni widoku), w którym kamera widzi geometrię: z granic paczek
 * nieprzezroczystych (siatki, węzły terenu), które mogą być w kadrze. Pozwala rozłożyć kaskady cieni
 * tam, gdzie coś jest - np. kamera wysoko nad terenem nie marnuje kaskad na pusty odcinek między sobą
 * a ziemią, a oddalenie kamery nie "wypycha" terenu poza zasięg cieni.
 */
void Renderer::RendererImpl::updateVisibleDepthRange(const RenderScene& scene, const RenderView& v) {
    visDepthValid_ = false;
    const float sx = std::abs(v.projection.values[0]), sy = std::abs(v.projection.values[5]);
    float mn = 1e30f, mx = 0.0f;
    for (const auto& p : scene.getOpaquePackets()) {
        if (!p.hasBounds) continue;
        float dmin = 1e30f, dmax = -1e30f;
        bool behind = false;
        int outL = 0, outR = 0, outT = 0, outB = 0;
        for (int i = 0; i < 8; ++i) {
            const Vec3 c{(i & 1) ? p.boundsMax.x : p.boundsMin.x, (i & 2) ? p.boundsMax.y : p.boundsMin.y,
                         (i & 4) ? p.boundsMax.z : p.boundsMin.z};
            const Vec3 vp = transformPoint(v.view, c);
            const float d = -vp.z;   // głębokość przed kamerą
            dmin = std::min(dmin, d);
            dmax = std::max(dmax, d);
            if (d <= 0.01f) {
                behind = true;
                continue;
            }
            outR += (vp.x * sx > d) ? 1 : 0;
            outL += (-vp.x * sx > d) ? 1 : 0;
            outT += (vp.y * sy > d) ? 1 : 0;
            outB += (-vp.y * sy > d) ? 1 : 0;
        }
        if (dmax <= 0.0f) continue;   // cały za kamerą
        if (!behind && (outR == 8 || outL == 8 || outT == 8 || outB == 8)) continue;   // poza kadrem
        mn = std::min(mn, std::max(dmin, 0.0f));
        mx = std::max(mx, dmax);
    }
    if (mx > 0.0f) {
        // Wygładzanie w czasie: zakres zmienia się skokowo, gdy węzły terenu wchodzą w kadr lub zmieniają LOD -
        // bez wygładzania kaskady (a z nimi ostrość cieni) przeskakiwałyby. Przy dużej zmianie (teleport kamery,
        // nowa scena) przyjmujemy wartość od razu.
        const bool jump = !visDepthSmoothed_ || std::abs(mn - visMinDepth_) > 0.5f * std::max(visMinDepth_, 50.0f) ||
                          std::abs(mx - visMaxDepth_) > 0.5f * std::max(visMaxDepth_, 50.0f);
        const float k = jump ? 1.0f : 0.08f;
        visMinDepth_ += (mn - visMinDepth_) * k;
        visMaxDepth_ += (mx - visMaxDepth_) * k;
        visDepthSmoothed_ = true;
        visDepthValid_ = true;
    }
}

/**
 * Wybiera światła rzucające cień w tej klatce i przydziela im miejsce w tablicy map cieni:
 *  - pierwsze światło kierunkowe (słońce) -> kaskady w warstwach 0..kShadowCascades-1,
 *  - światła lokalne (reflektor, punktowe): do kLocalShadowSlots najbliższych kamerze, których zasięg
 *    sięga w obszar cieni (odległość - zasięg < zasięg cieni) -> ćwiartki atlasu w warstwie kLocalShadowLayer.
 * Pozostałe światła oświetlają scenę normalnie, ale bez cienia (position.w = -1).
 */
void Renderer::RendererImpl::assignShadowSlots(const Vec3& cameraPos) {
    shadowCasters_.clear();
    if (!shadowsEnabled_) {
        // Cienie wyłączone: żadne światło nie ma warstwy w mapie cieni (shader i tak sprawdza shadowsEnabled).
        for (uint32_t i = 0; i < storedLightCount_; ++i) storedLights_[i].position[3] = -1.0f;
        return;
    }
    auto& local = shadowCandidates_;   // (indeks światła, odległość kamery od zasięgu)
    local.clear();
    bool sunAssigned = false;
    for (uint32_t i = 0; i < storedLightCount_; ++i) {
        LightData& l = storedLights_[i];
        l.position[3] = -1.0f;
        const int type = static_cast<int>(l.direction[3]);
        if (type == 1) {
            if (!sunAssigned) {
                sunAssigned = true;
                // Słońce pod horyzontem (kierunek do światła skierowany w dół) nie rzuca cieni -
                // mapa cieni "od spodu" dawałaby bezsensowne cienie terenu w nocy.
                const float len = std::sqrt(l.direction[0] * l.direction[0] + l.direction[1] * l.direction[1] +
                                            l.direction[2] * l.direction[2]);
                if (len > 1e-4f && l.direction[1] / len < -0.02f) continue;
                l.position[3] = 0.0f;
                l.params[3] = static_cast<float>(kShadowCascades);
                shadowCasters_.push_back({i, 0u, static_cast<uint32_t>(kShadowCascades), -1});
            }
            continue;
        }
        const float range = l.params[2];
        if (range <= 0.0f) continue;
        const Vec3 d{l.position[0] - cameraPos.x, l.position[1] - cameraPos.y, l.position[2] - cameraPos.z};
        const float distanceToRange = std::sqrt(dot(d, d)) - range;   // < 0: kamera w zasięgu światła
        if (distanceToRange > dirShadowHalfSize_) continue;             // światło za daleko - cień niewidoczny
        local.emplace_back(i, distanceToRange);
    }
    // Potrzebujemy tylko kLocalShadowSlots najbliższych - częściowe sortowanie zamiast pełnego.
    const size_t take = std::min<size_t>(local.size(), kLocalShadowSlots);
    std::partial_sort(local.begin(), local.begin() + static_cast<std::ptrdiff_t>(take), local.end(),
                      [](const auto& a, const auto& b) { return a.second < b.second; });
    for (uint32_t slot = 0; slot < take; ++slot) {
        LightData& l = storedLights_[local[slot].first];
        l.position[3] = static_cast<float>(kLocalShadowLayer);
        l.params[3] = static_cast<float>(slot);
        shadowCasters_.push_back({local[slot].first, kLocalShadowLayer, 1u, static_cast<int>(slot)});
    }
}

std::array<Vec3, 8> Renderer::RendererImpl::buildCameraFrustumCorners(const RenderView& view) const {
    const Matrix4 invViewProj = inverse(multiply(view.projection, view.view));
    std::array<Vec3, 8> corners{};
    size_t index = 0;
    for (int z = 0; z < 2; ++z) {
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                const Vec3 ndc = {
                    x == 0 ? -1.0f : 1.0f,
                    y == 0 ? -1.0f : 1.0f,
                    z == 0 ? 0.0f : 1.0f
                };
                corners[index++] = projectPoint(invViewProj, ndc);
            }
        }
    }
    return corners;
}

void Renderer::RendererImpl::createShadowResources() {
    VkDevice device = vulkanContext_->device();
    VkPhysicalDevice pd = vulkanContext_->physicalDevice();

    vk::createImage(device, pd, kShadowMapSize, kShadowMapSize, 1,
                    VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    shadowMapImage_, shadowMapMemory_, kShadowLayerCount);

    shadowMapView_ = vk::createImageView(device, shadowMapImage_, VK_FORMAT_D32_SFLOAT,
                                          VK_IMAGE_ASPECT_DEPTH_BIT, 1, kShadowLayerCount, VK_IMAGE_VIEW_TYPE_2D_ARRAY);

    LOG_INFO("Mapy cieni: {} warstw {}x{} (D32) = {} MB VRAM (kaskady słońca: {}, atlas świateł lokalnych: {} x {}^2)",
             kShadowLayerCount, kShadowMapSize, kShadowMapSize,
             static_cast<uint64_t>(kShadowLayerCount) * kShadowMapSize * kShadowMapSize * 4 / (1024 * 1024),
             kShadowCascades, kLocalShadowSlots, kShadowMapSize / 2);
    shadowMapLayerViews_.resize(kShadowLayerCount);
    for (uint32_t i = 0; i < kShadowLayerCount; i++) {
        shadowMapLayerViews_[i] = vk::createImageView(device, shadowMapImage_, VK_FORMAT_D32_SFLOAT,
                                                       VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1, VK_IMAGE_VIEW_TYPE_2D);
        // Każdy widok pokazuje jedną warstwę tablicy map cieni
        vkDestroyImageView(device, shadowMapLayerViews_[i], nullptr);
        
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = shadowMapImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_D32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = i;
        viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &viewInfo, nullptr, &shadowMapLayerViews_[i]);
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = 1.0f;
    vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler_);

    registerTextureInBindless("shadow_map", shadowMapView_, shadowSampler_);

    for (int i = 0; i < kMaxFrames; i++) {
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowMapView_;
        shadowInfo.sampler = shadowSampler_;

        VkWriteDescriptorSet shadowWrite{};
        shadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrite.dstSet = descriptorSets_[i];
        shadowWrite.dstBinding = 2;
        shadowWrite.dstArrayElement = 0;
        shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowWrite.descriptorCount = 1;
        shadowWrite.pImageInfo = &shadowInfo;
        vkUpdateDescriptorSets(device, 1, &shadowWrite, 0, nullptr);
    }

    transitionShadowMapLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Renderer::RendererImpl::transitionShadowMapLayout(VkImageLayout newLayout) {
    VkCommandBuffer cmd = beginSingleTimeCommands();

    // Przejście całej tablicy map cieni; pełna bariera - wołane tylko przy tworzeniu zasobów.
    vk::imageBarrier(cmd, {.image = shadowMapImage_,
                           .oldLayout = shadowMapLayout_,
                           .newLayout = newLayout,
                           .srcStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .srcAccess = VK_ACCESS_2_MEMORY_WRITE_BIT,
                           .dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .dstAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                           .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                           .layerCount = kShadowLayerCount});
    endSingleTimeCommands(cmd);
    shadowMapLayout_ = newLayout;
}

// Wspólne ustawienia potoków map cieni: tylko głębokość (D32, bez celów koloru) i przesunięcie głębokości
// przeciw "shadow acne". Każdy potok ma dwa warianty: światła lokalne i słońce.
// Słońce (rzut ortograficzny) dostaje depthClamp - obiekty przed bliską płaszczyzną kaskady (np. wzgórze daleko
// w stronę słońca) są dociskane do niej, zamiast znikać z mapy cieni. W rzucie perspektywicznym (światła lokalne)
// depthClamp wyłącza obcinanie geometrii za "okiem" światła i psuje mapę - stąd osobny wariant.
void Renderer::RendererImpl::buildShadowPipelinePair(vk::GraphicsPipelineBuilder& builder, VkPipelineLayout layout,
                                                     VkPipeline& localPipeline, VkPipeline& sunPipeline,
                                                     const std::string& name) {
    VkDevice device = vulkanContext_->device();
    builder.depthBias(1.25f, 1.75f).depthFormat(VK_FORMAT_D32_SFLOAT);
    localPipeline = builder.depthClamp(false).build(device, layout, name);
    sunPipeline = builder.depthClamp(vulkanContext_->isDepthClampSupported()).build(device, layout, name + "_sun");
}

/**
 * Potok map cieni dla terenu teselowanego (shader terrain_shadow): ta sama teselacja i wysokość z mapy
 * co teren na ekranie, więc cień powstaje z widocznej geometrii. Tworzony tylko, gdy GPU obsługuje teselację.
 */
void Renderer::RendererImpl::createTerrainShadowPipeline() {
    if (!vulkanContext_->isTessellationSupported()) return;
    VkDevice device = vulkanContext_->device();
    const std::string dir = "bin/assets/shaders/terrain_shadow/terrain_shadow.";
    for (const char* ext : {"vert.spv", "tesc.spv", "tese.spv", "frag.spv"}) {
        if (!std::filesystem::exists(dir + ext)) {
            LOG_WARN("Brak shaderów terrain_shadow - cienie terenu z siatki CPU");
            return;
        }
    }
    const vk::ShaderModule vert(device, dir + "vert.spv");
    const vk::ShaderModule hull(device, dir + "tesc.spv");
    const vk::ShaderModule domain(device, dir + "tese.spv");
    const vk::ShaderModule frag(device, dir + "frag.spv");

    terrainShadowPipelineLayout_ = vk::createPipelineLayout(
        device, {descriptorSetLayout_}, sizeof(TerrainShadowPushConstants),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
            VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        "terrain_shadow");
    vk::GraphicsPipelineBuilder builder;
    builder.shader(VK_SHADER_STAGE_VERTEX_BIT, vert, "vertexMain")
        .shader(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, hull, "hullMain")
        .shader(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, domain, "domainMain")
        .shader(VK_SHADER_STAGE_FRAGMENT_BIT, frag, "fragmentMain")
        .vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()))
        .patches(3)
        .cull(VK_CULL_MODE_NONE);   // teren to "skorupa" - cień rzucają obie strony
    buildShadowPipelinePair(builder, terrainShadowPipelineLayout_, terrainShadowPipeline_, terrainShadowSunPipeline_,
                            "terrain_shadow");
}

void Renderer::RendererImpl::createShadowPipeline() {
    VkDevice device = vulkanContext_->device();
    const vk::ShaderModule vert(device, "bin/assets/shaders/shadow_depth/shadow_depth.vert.spv");
    const vk::ShaderModule frag(device, "bin/assets/shaders/shadow_depth/shadow_depth.frag.spv");

    shadowPipelineLayout_ = vk::createPipelineLayout(device, {descriptorSetLayout_}, sizeof(ShadowPushConstants),
                                                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, "shadow");
    vk::GraphicsPipelineBuilder builder;
    // Tylko atrybuty czytane przez vertex shader (cienie: pozycja + UV dla alpha-testu).
    builder.shaders(vert, frag)
        .vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()));
    buildShadowPipelinePair(builder, shadowPipelineLayout_, shadowPipeline_, shadowSunPipeline_, "shadow");
}

void Renderer::RendererImpl::createShadowSkinnedPipeline() {
    VkDevice device = vulkanContext_->device();
    const vk::ShaderModule vert(device, "bin/assets/shaders/shadow_depth_skinned/shadow_depth_skinned.vert.spv");
    const vk::ShaderModule frag(device, "bin/assets/shaders/shadow_depth_skinned/shadow_depth_skinned.frag.spv");

    shadowSkinnedPipelineLayout_ = vk::createPipelineLayout(
        device, {descriptorSetLayout_, boneDescriptorSetLayout_}, sizeof(ShadowPushConstants),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, "shadow_skinned");
    vk::GraphicsPipelineBuilder builder;
    builder.shaders(vert, frag)
        .vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()))
        .vertexInput(SkinVertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), SkinVertex::attributeDescriptions()));
    buildShadowPipelinePair(builder, shadowSkinnedPipelineLayout_, shadowSkinnedPipeline_, shadowSkinnedSunPipeline_,
                            "shadow_skinned");
}

} // namespace engine3d
