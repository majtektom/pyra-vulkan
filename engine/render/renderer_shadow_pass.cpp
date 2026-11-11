#include "render/renderer_impl.h"

namespace engine3d {

// Mapy cieni: 4 kaskady słońca (warstwy 0-3) i atlas 2x2 świateł lokalnych (warstwa 4).
void Renderer::RendererImpl::recordShadowPass(VkCommandBuffer cmd, const RenderScene& scene) {
    VkImageMemoryBarrier shadowBarrier{};
    shadowBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    shadowBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    shadowBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    shadowBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    shadowBarrier.image = shadowMapImage_;
    shadowBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    shadowBarrier.subresourceRange.baseMipLevel = 0;
    shadowBarrier.subresourceRange.levelCount = 1;
    shadowBarrier.subresourceRange.baseArrayLayer = 0;
    shadowBarrier.subresourceRange.layerCount = kShadowLayerCount;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &shadowBarrier);

    // Rysuje obiekty rzucające cień do bieżącego widoku cienia (macierz lvp).
    // cullCenter/cullRadius > 0: tylko obiekty w zasięgu światła lokalnego.
    // Kolejność rysowania cieni: raz na klatkę sortujemy paczki po rodzaju potoku (teren teselowany,
    // zwykłe, animowane) i siatce - wtedy w każdej z 8 warstw (4 kaskady + 4 światła) kolejne paczki
    // mają ten sam potok i bufory, a DrawBindCache pomija powtórne bindowanie. Głębokość tu nie jest
    // sortowana: shader cienia jest prawie pusty, więc kolejność front-to-back niewiele daje.
    // Przy okazji liczymy raz na klatkę środek i połowę rozmiaru granic (AABB) każdej paczki - test
    // "czy paczka trafia w mapę cieni" robimy potem dla 4 kaskad i świateł lokalnych bez ich przeliczania.
    {
        scene.gatherShadowPackets(shadowPacketPtrs_);
        shadowOrder_.clear();
        shadowOrder_.reserve(shadowPacketPtrs_.size());
        for (const RenderPacket* p : shadowPacketPtrs_) {
            ShadowDraw d;
            d.packet = p;
            d.kind = (p->heightmapEnabled() && terrainShadowPipeline_ && tessellationEnabled_ && p->indexBuffer != VK_NULL_HANDLE)
                         ? 0 : (p->boneDescriptorSet != VK_NULL_HANDLE ? 2 : 1);
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

    auto drawShadowCasters = [&](const Matrix4& lvp, const Vec3& cullCenter, float cullRadius, bool sun) {
        DrawBindCache bound;   // nowa warstwa = nowe vkCmdBeginRendering - zaczynamy od pustej pamięci
        // Rzut ortogonalny (kaskady słońca): w = 1 dla każdego punktu, więc granice pudełka w przestrzeni
        // światła liczy się wprost ze środka i połowy rozmiaru: |M| * extent (zamiast 8 narożników).
        const auto& m = lvp.values;
        const bool ortho = m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f;
        const float cullR2 = (cullRadius + 30.0f) * (cullRadius + 30.0f);
        for (const ShadowDraw& draw : shadowOrder_) {
            const RenderPacket& packet = *draw.packet;
            // Światło lokalne: najpierw tani test odległości (pozycja z macierzy świata; zapas 30 j. na rozmiar).
            if (cullRadius > 0.0f) {
                const Vec3 p{packet.transform.values[12], packet.transform.values[13], packet.transform.values[14]};
                const Vec3 d = subtract(p, cullCenter);
                if (dot(d, d) > cullR2) continue;
            }
            // Obiekt w całości poza obszarem mapy cieni (x/y poza [-1, 1] w przestrzeni światła) - pomijamy.
            // Głębokości nie sprawdzamy: obiekt przed płaszczyzną światła i tak rzuca cień (depthClamp).
            if (packet.hasBounds && ortho) {
                const Vec3& c = draw.center;
                const Vec3& e = draw.extent;
                const float x = m[0] * c.x + m[4] * c.y + m[8] * c.z + m[12];
                const float y = m[1] * c.x + m[5] * c.y + m[9] * c.z + m[13];
                const float ex = std::abs(m[0]) * e.x + std::abs(m[4]) * e.y + std::abs(m[8]) * e.z;
                const float ey = std::abs(m[1]) * e.x + std::abs(m[5]) * e.y + std::abs(m[9]) * e.z;
                const float w = m[15];
                if (x - ex > w || x + ex < -w || y - ey > w || y + ey < -w) continue;
            } else if (packet.hasBounds) {
                // Perspektywa (reflektor, światło punktowe): 8 narożników, każdy z własnym w.
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
                if (outL == 8 || outR == 8 || outB == 8 || outT == 8) continue;
            }
            // Teren teselowany: cień z tej samej geometrii co na ekranie (teselacja + mapa wysokości).
            if (draw.kind == 0) {
                bound.bindGlobalSet(cmd, terrainShadowPipelineLayout_, descriptorSets_[currentFrame_]);
                bound.bindPipeline(cmd, sun ? terrainShadowSunPipeline_ : terrainShadowPipeline_);
                bound.bindVertexBuffer(cmd, 0, packet.vertexBuffer);
                bound.bindIndexBuffer(cmd, packet.indexBuffer);
                TerrainShadowPushConstants tp{};
                tp.model = packet.transform;
                tp.lightViewProj = lvp;
                tp.heightmapTextureSet = packet.terrain->heightmapTextureIndex;
                tp.scaleY = packet.terrain->scaleY;
                tp.tessellationFactor = packet.tessellationFactor;
                tp.layerWorldSize = packet.terrain->layerWorldSize;
                vkCmdPushConstants(cmd, terrainShadowPipelineLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                       VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(tp), &tp);
                vkCmdDrawIndexed(cmd, packet.indexCount, 1, 0, 0, 0);
                continue;
            }
            bool isSkinned = (packet.boneDescriptorSet != VK_NULL_HANDLE);

            if (isSkinned) {
                bound.bindGlobalSet(cmd, shadowSkinnedPipelineLayout_, descriptorSets_[currentFrame_]);
                bound.bindPipeline(cmd, sun && shadowSkinnedSunPipeline_ ? shadowSkinnedSunPipeline_ : shadowSkinnedPipeline_);
                bound.bindSet1(cmd, shadowSkinnedPipelineLayout_, packet.boneDescriptorSet);
            } else {
                bound.bindGlobalSet(cmd, shadowPipelineLayout_, descriptorSets_[currentFrame_]);
                bound.bindPipeline(cmd, sun && shadowSunPipeline_ ? shadowSunPipeline_ : shadowPipeline_);
            }

            bound.bindVertexBuffer(cmd, 0, packet.vertexBuffer);
            if (packet.skinVertexBuffer != VK_NULL_HANDLE) bound.bindVertexBuffer(cmd, 1, packet.skinVertexBuffer);
            if (packet.indexBuffer != VK_NULL_HANDLE) bound.bindIndexBuffer(cmd, packet.indexBuffer);

            ShadowPushConstants push{};
            push.model = packet.transform;
            push.lightViewProj = lvp;
            push.alphaCutoff = packet.material && packet.material->isTransparent() ? 0.5f : 0.0f;
            push.baseColorTextureSet = packet.diffuseTextureIndex;

            VkPipelineLayout pushLayout = isSkinned ? shadowSkinnedPipelineLayout_ : shadowPipelineLayout_;
            vkCmdPushConstants(cmd, pushLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push), &push);

            if (packet.indexBuffer != VK_NULL_HANDLE)
                vkCmdDrawIndexed(cmd, packet.indexCount, 1, 0, 0, 0);
        }
    };

    // Rozpoczyna rysowanie do jednej warstwy tablicy cieni (z wyczyszczeniem całej warstwy).
    auto beginShadowLayer = [&](uint32_t layer) {
        VkRenderingAttachmentInfo shadowDepthAttachment{};
        shadowDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        shadowDepthAttachment.imageView = shadowMapLayerViews_[layer];
        shadowDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        shadowDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        shadowDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        shadowDepthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo shadowRenderingInfo{};
        shadowRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        shadowRenderingInfo.renderArea = {{0, 0}, {kShadowMapSize, kShadowMapSize}};
        shadowRenderingInfo.layerCount = 1;
        shadowRenderingInfo.colorAttachmentCount = 0;
        shadowRenderingInfo.pDepthAttachment = &shadowDepthAttachment;
        vkCmdBeginRendering(cmd, &shadowRenderingInfo);
    };

    // Obszar rysowania: cała warstwa (słońce) albo ćwiartka atlasu (światło lokalne).
    auto setShadowViewport = [&](int atlasSlot) {
        float x = 0.0f, y = 0.0f, size = static_cast<float>(kShadowMapSize);
        if (atlasSlot >= 0) {
            size *= 0.5f;
            x = static_cast<float>(atlasSlot % 2) * size;
            y = static_cast<float>(atlasSlot / 2) * size;
        }
        VkViewport viewport{x, y, size, size, 0.0f, 1.0f};
        VkRect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)},
                         {static_cast<uint32_t>(size), static_cast<uint32_t>(size)}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    };

    // 1. Kaskady słońca - każda w osobnej warstwie.
    for (const ShadowCaster& caster : shadowCasters_) {
        if (caster.atlasSlot >= 0) continue;
        for (uint32_t cascade = 0; cascade < caster.cascadeCount; ++cascade) {
            beginShadowLayer(caster.layer + cascade);
            setShadowViewport(-1);
            drawShadowCasters(storedLights_[caster.lightIndex].lightViewProj[cascade], Vec3{}, 0.0f, true);
            vkCmdEndRendering(cmd);
        }
    }

    // 2. Światła lokalne - wszystkie w jednej warstwie-atlasie, każde w swojej ćwiartce.
    beginShadowLayer(kLocalShadowLayer);
    for (const ShadowCaster& caster : shadowCasters_) {
        if (caster.atlasSlot < 0) continue;
        const LightData& l = storedLights_[caster.lightIndex];
        setShadowViewport(caster.atlasSlot);
        drawShadowCasters(l.lightViewProj[0], Vec3{l.position[0], l.position[1], l.position[2]}, l.params[2], false);
    }
    vkCmdEndRendering(cmd);

    shadowBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    shadowBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    shadowBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &shadowBarrier);
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
        // Manually set the base array layer for each view
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

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = shadowMapLayout_;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadowMapImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = kShadowLayerCount;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    if (shadowMapLayout_ == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (shadowMapLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (shadowMapLayout_ == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    endSingleTimeCommands(cmd);
    shadowMapLayout_ = newLayout;
}

/**
 * Potok map cieni dla terenu teselowanego (shader terrain_shadow): ta sama teselacja i wysokość z mapy
 * co teren na ekranie, więc cień powstaje z widocznej geometrii. Dwa warianty: słońce (depthClamp)
 * i światła lokalne. Tworzony tylko, gdy GPU obsługuje teselację.
 */
void Renderer::RendererImpl::createTerrainShadowPipeline() {
    if (!vulkanContext_->isTessellationSupported()) return;
    VkDevice device = vulkanContext_->device();
    const std::string dir = "bin/assets/shaders/terrain_shadow/terrain_shadow.";
    std::vector<char> codes[4];
    try {
        codes[0] = vk::readFile(dir + "vert.spv");
        codes[1] = vk::readFile(dir + "tesc.spv");
        codes[2] = vk::readFile(dir + "tese.spv");
        codes[3] = vk::readFile(dir + "frag.spv");
    } catch (...) {
        LOG_WARN("Brak shaderów terrain_shadow - cienie terenu z siatki CPU");
        return;
    }
    const VkShaderStageFlagBits stageBits[4] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    const char* entries[4] = {"vertexMain", "hullMain", "domainMain", "fragmentMain"};
    VkShaderModule modules[4];
    VkPipelineShaderStageCreateInfo stages[4]{};
    for (int i = 0; i < 4; ++i) {
        modules[i] = vk::createShaderModule(device, codes[i]);
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = stageBits[i];
        stages[i].module = modules[i];
        stages[i].pName = entries[i];
    }

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = vk::filterVertexAttributes(codes[0], Vertex::attributeDescriptions());
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    VkPipelineTessellationStateCreateInfo tess{};
    tess.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tess.patchControlPoints = 3;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Domain shader HLSL z "triangle_cw" odwraca kolejność względem siatki - jak w potoku terenu
    // na ekranie (ten sam frontFace/cull), żeby rzucały cień te same ściany co widać.
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;   // teren to "skorupa" - cień rzucają obie strony
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                           VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(TerrainShadowPushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &terrainShadowPipelineLayout_);

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 4;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = &tess;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = terrainShadowPipelineLayout_;

    rasterizer.depthClampEnable = VK_FALSE;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &terrainShadowPipeline_);
    rasterizer.depthClampEnable = vulkanContext_->isDepthClampSupported() ? VK_TRUE : VK_FALSE;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &terrainShadowSunPipeline_);
    for (auto m : modules) vkDestroyShaderModule(device, m, nullptr);
}

void Renderer::RendererImpl::createShadowPipeline() {
    VkDevice device = vulkanContext_->device();

    auto vertCode = vk::readFile("bin/assets/shaders/shadow_depth/shadow_depth.vert.spv");
    auto fragCode = vk::readFile("bin/assets/shaders/shadow_depth/shadow_depth.frag.spv");
    VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "vertexMain";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "fragmentMain";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    auto bindingDesc = Vertex::bindingDescription();
    // Tylko atrybuty czytane przez vertex shader (np. cienie: pozycja + UV).
    auto attrDescs = vk::filterVertexAttributes(vertCode, Vertex::attributeDescriptions());

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    rasterizer.depthClampEnable = VK_FALSE;   // wariant ze spłaszczaniem (słońce) tworzony niżej

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynStates;

    constexpr uint32_t kShadowPushSize = sizeof(ShadowPushConstants);
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = kShadowPushSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowPipelineLayout_);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowPipelineLayout_;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    pipelineInfo.pNext = &renderingInfo;

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline_);
    // Wariant dla słońca (rzut ortograficzny): depthClamp - obiekty przed bliską płaszczyzną kaskady
    // (np. wzgórze daleko w stronę słońca) są dociskane do niej, zamiast znikać z mapy cieni.
    // Tylko dla słońca: w rzucie perspektywicznym (światła lokalne) depthClamp wyłącza obcinanie
    // geometrii za "okiem" światła i psuje mapę.
    rasterizer.depthClampEnable = vulkanContext_->isDepthClampSupported() ? VK_TRUE : VK_FALSE;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowSunPipeline_);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
}

void Renderer::RendererImpl::createShadowSkinnedPipeline() {
    VkDevice device = vulkanContext_->device();

    auto vertCode = vk::readFile("bin/assets/shaders/shadow_depth_skinned/shadow_depth_skinned.vert.spv");
    auto fragCode = vk::readFile("bin/assets/shaders/shadow_depth_skinned/shadow_depth_skinned.frag.spv");
    VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "vertexMain";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "fragmentMain";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();

    VkVertexInputBindingDescription skinBinding{};
    skinBinding.binding = 1;
    skinBinding.stride = sizeof(uint32_t) * 4 + sizeof(float) * 4;
    skinBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription skinAttrs[2]{};
    skinAttrs[0].binding = 1;
    skinAttrs[0].location = 4;
    skinAttrs[0].format = VK_FORMAT_R32G32B32A32_UINT;
    skinAttrs[0].offset = 0;
    skinAttrs[1].binding = 1;
    skinAttrs[1].location = 5;
    skinAttrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    skinAttrs[1].offset = sizeof(uint32_t) * 4;

    std::vector<VkVertexInputBindingDescription> bindings = {bindingDesc, skinBinding};
    std::vector<VkVertexInputAttributeDescription> allAttrs(attrDescs.begin(), attrDescs.end());
    allAttrs.push_back(skinAttrs[0]);
    allAttrs.push_back(skinAttrs[1]);
    allAttrs = vk::filterVertexAttributes(vertCode, allAttrs);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttrs.size());
    vertexInput.pVertexAttributeDescriptions = allAttrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    rasterizer.depthClampEnable = VK_FALSE;   // wariant ze spłaszczaniem (słońce) tworzony niżej

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynStates;

    constexpr uint32_t kShadowPushSize = sizeof(ShadowPushConstants);
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = kShadowPushSize;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, boneDescriptorSetLayout_};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowSkinnedPipelineLayout_);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowSkinnedPipelineLayout_;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    pipelineInfo.pNext = &renderingInfo;

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowSkinnedPipeline_);
    rasterizer.depthClampEnable = vulkanContext_->isDepthClampSupported() ? VK_TRUE : VK_FALSE;   // wariant słońca
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowSkinnedSunPipeline_);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
}

} // namespace engine3d
