#include "core/profiler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "render/backend/vulkan_context.h"
#include "render/backend/vulkan_swapchain.h"
#include "render/standard_passes.h"
#include "platform/platform.h"
#include "core/entity.h"
#include "core/mesh_component.h"
#include "camera/camera_component.h"
#include "render/render_pass.h"
#include "render/texture_manager.h"
#include "render/vulkan_util.h"
#include "render/texture_asset_loader.h"
#include "render/renderer.h"
#include "render/render_feature.h"
#include "render/render_orchestrator.h"
#include "render/material_manager.h"
#include "core/asset_registry.h"
#include "core/logger.h"

namespace engine3d {

// Triple buffering: CPU może przygotowywać klatkę N+2 gdy GPU renderuje N.
// Eliminuje vkWaitForFences stall który ograniczał do ~120fps.
constexpr int kMaxFrames = Renderer::kMaxFrames;
constexpr int kMaxDescriptorSets = 64;

constexpr int kMaxLights = 16;
constexpr int kShadowCascades = 4;

// --- Układ pamięci map cieni (jedna tablica tekstur głębokości kShadowMapSize x kShadowMapSize) ---
// Warstwy 0..kShadowCascades-1: kaskady słońca (tylko pierwsze światło kierunkowe - cień na całej mapie).
// Warstwa kLocalShadowLayer: atlas 2x2 dla świateł o ograniczonym zasięgu (reflektory, punktowe),
// każde dostaje ćwiartkę (kShadowMapSize/2). Cień dostają tylko najbliższe kamerze światła lokalne.
// Wcześniej każde z 16 świateł miało 4 warstwy (64 warstwy = ok. 1 GB VRAM), dziś 5 warstw (ok. 80 MB).
constexpr uint32_t kLocalShadowSlots = 4;
constexpr uint32_t kLocalShadowLayer = kShadowCascades;
constexpr uint32_t kShadowLayerCount = kShadowCascades + 1;

struct LightData {
    Matrix4 lightViewProj[kShadowCascades];
    float position[4];    // xyz = world position (point/spot), w = warstwa mapy cieni (-1 = światło bez cienia)
    float direction[4];   // xyz = normalized direction OF light rays, w = type: 1=dir, 2=point, 3=spot
    float color[4];       // rgb = color, w = intensity multiplier
    float params[4];      // spot: innerCutoffCos, outerCutoffCos, range; w = liczba kaskad (słońce) / ćwiartka atlasu (lokalne)
};

// Światło rzucające cień w bieżącej klatce (wynik assignShadowSlots).
struct ShadowCaster {
    uint32_t lightIndex;
    uint32_t layer;
    uint32_t cascadeCount;   // słońce: kShadowCascades, lokalne: 1
    int atlasSlot;           // -1 dla słońca, 0..3 dla świateł lokalnych
};

struct GlobalUBO {
    Matrix4 view;
    Matrix4 proj;
    Matrix4 invViewProj;
    float camRight[4];
    float camUp[4];
    float cameraPos[4];
    float ambientColor[4];
    float ambientStrength;
    uint32_t lightingEnabled;
    uint32_t lightCount;
    uint32_t shadowsEnabled;
    LightData lights[kMaxLights];
    float directionalShadowSplits[4];
    uint32_t directionalShadowCascadeCount;
    float _shadowPadding[3];
    float fogColor[4];
    float fogStart;
    float fogEnd;
    float fogDensity;
    uint32_t fogEnabled;
    float windTime;
    float windStrength;
    // Mgła wojny (fog of war): tekstura widoczności nad prostokątem mapy [origin, origin + size] (XZ).
    float fowOriginX;
    float fowOriginZ;
    float fowSize;
    float fowExplored;     // jasność terenu odkrytego, ale nie widzianego teraz
    uint32_t fowEnabled;
    uint32_t fowTextureIndex;   // indeks tekstury widoczności bieżącej klatki w tablicy bindless (shader terenu)
};

struct TextureResources {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t mipLevels = 1;
    AddressMode addressMode = AddressMode::Repeat;
};

constexpr int kMaxBindlessTextures = 1024;

class Renderer::RendererImpl {
public:
    RendererImpl(
        Platform* platform,
        MaterialManager* materialManager,
        std::shared_ptr<VulkanContext> context,
        AssetRegistry* assetRegistry)
        : platform_(platform),
          materialManager_(materialManager),
          vulkanContext_(context),
          assetRegistry_(assetRegistry) {}

    ~RendererImpl() { cleanup(); }

    bool initialize(const std::string& appName, bool enableValidationLayers) {
        if (!vulkanContext_) {
            LOG_ERROR("Vulkan context is null during renderer initialization");
            return false;
        }
        try {
            swapchain_ = std::make_unique<VulkanSwapchain>(vulkanContext_, platform_->width(), platform_->height(),
                                                           presentMode_ == PresentMode::Immediate ? VK_PRESENT_MODE_IMMEDIATE_KHR :
                                                           presentMode_ == PresentMode::Mailbox ? VK_PRESENT_MODE_MAILBOX_KHR :
                                                           VK_PRESENT_MODE_FIFO_KHR);

            createUniformBuffers();
            createDefaultTexture();
            createDefaultNormalTexture();
            createDefaultOrmTexture();
            createDefaultBlackTexture();
            createDescriptorResources();

            // Register default textures
            registerTextureInBindless("default", defaultImageView_, defaultSampler_);
            registerTextureInBindless("default_normal", defaultNormalImageView_, defaultNormalSampler_);
            registerTextureInBindless("default_orm", defaultOrmImageView_, defaultOrmSampler_);
            registerTextureInBindless("default_black", defaultBlackImageView_, defaultBlackSampler_);

            defaultDiffuseIndex_ = getTextureIndex("default");
            defaultNormalIndex_ = getTextureIndex("default_normal");
            defaultOrmIndex_ = getTextureIndex("default_orm");
            defaultEmissiveIndex_ = getTextureIndex("default_black");
            defaultMaskIndex_ = getTextureIndex("default_black");

            passes_.push_back(std::make_unique<OpaquePass>());
            passes_.push_back(std::make_unique<TransparentDepthPrepass>());
            passes_.push_back(std::make_unique<TransparentPass>());

            createSyncObjects();
            createCommandBuffers();
            createShadowResources();
            createShadowPipeline();
            createShadowSkinnedPipeline();
            createTerrainShadowPipeline();
            createFogOfWarResources();
            createGBufferResources();
            createSceneColorResources();
            LOG_INFO("Deferred PBR Renderer initialized.");
            return true;
        } catch (const std::exception& e) {
            LOG_CRITICAL("Renderer Init Error: {}", e.what());
            return false;
        }
    }

    void render(const RenderScene& scene, const RenderView& view) {
        if (!vulkanContext_ || !vulkanContext_->device()) return;

        // Zminimalizowane okno ma powierzchnię 0x0 — swapchaina nie da się utworzyć ani użyć.
        if (platform_->isMinimized()) return;

        if (platform_->wasResized()) {
            platform_->clearResizeFlag();
            recreateSwapChain();
        }

        if (!swapchain_) return;
        Profiler::get().lap("render.paczki (renderFrame)");

        VkDevice device = vulkanContext_->device();

        if (synchronizationMode_ == SynchronizationMode::Timeline) {
            VkSemaphoreWaitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &renderTimelineSemaphores_[currentFrame_];
            waitInfo.pValues = &timelineCounters_[currentFrame_];
            vkWaitSemaphores(device, &waitInfo, UINT64_MAX);
        } else {
            vkWaitForFences(device, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
        }
        Profiler::get().lap("render.czekanie na GPU");

        uint32_t imageIndex = 0;
        try {
            imageIndex = swapchain_->acquireNextImage(imageAvailableSemaphores_[currentFrame_]);
        } catch (const std::runtime_error&) {
            recreateSwapChain();
            currentFrame_ = (currentFrame_ + 1) % kMaxFrames;
            return;
        }
        if (synchronizationMode_ != SynchronizationMode::Timeline) {
            vkResetFences(device, 1, &inFlightFences_[currentFrame_]);
        }

        Profiler::get().lap("render.acquire");
        updateVisibleDepthRange(scene, view);
        updateUniformBuffer(view);
        Profiler::get().lap("render.przygotowanie");

        VkCommandBuffer cmd = commandBuffers_[currentFrame_];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        uploadFogOfWarIfChanged(cmd);

        // Pre-render stage (outside render passes, e.g. for compute dispatches)
        for (auto& feature : features_) {
            if (feature->isActive()) {
                feature->preRender(cmd, currentFrame_, view);
            }
        }

        if (shadowsEnabled_) {
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
            auto drawShadowCasters = [&](const Matrix4& lvp, const Vec3& cullCenter, float cullRadius, bool sun) {
                for (const auto& packet : scene.getShadowPackets()) {
                    // Obiekt w całości poza obszarem mapy cieni (x/y poza [-1, 1] w przestrzeni światła) - pomijamy.
                    // Głębokości nie sprawdzamy: obiekt przed płaszczyzną światła i tak rzuca cień (depthClamp).
                    if (packet.hasBounds) {
                        int outL = 0, outR = 0, outB = 0, outT = 0;
                        for (int c = 0; c < 8; ++c) {
                            const Vec3 corner{(c & 1) ? packet.boundsMax.x : packet.boundsMin.x,
                                              (c & 2) ? packet.boundsMax.y : packet.boundsMin.y,
                                              (c & 4) ? packet.boundsMax.z : packet.boundsMin.z};
                            const auto& m = lvp.values;
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
                    if (cullRadius > 0.0f) {
                        // Pozycja obiektu z macierzy świata; zapas 30 j. na jego rozmiar.
                        const Vec3 p{packet.transform.values[12], packet.transform.values[13], packet.transform.values[14]};
                        const Vec3 d = subtract(p, cullCenter);
                        if (dot(d, d) > (cullRadius + 30.0f) * (cullRadius + 30.0f)) continue;
                    }
                    // Teren teselowany: cień z tej samej geometrii co na ekranie (teselacja + mapa wysokości).
                    const bool tessTerrain = packet.heightmapEnabled && terrainShadowPipeline_ && tessellationEnabled_ &&
                                             packet.indexBuffer != VK_NULL_HANDLE;
                    if (tessTerrain) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sun ? terrainShadowSunPipeline_ : terrainShadowPipeline_);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainShadowPipelineLayout_, 0, 1,
                                                &descriptorSets_[currentFrame_], 0, nullptr);
                        VkBuffer tvb[] = {packet.vertexBuffer};
                        VkDeviceSize toff[] = {0};
                        vkCmdBindVertexBuffers(cmd, 0, 1, tvb, toff);
                        vkCmdBindIndexBuffer(cmd, packet.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        TerrainShadowPush tp{};
                        tp.model = packet.transform;
                        tp.lvp = lvp;
                        tp.heightmapTextureSet = packet.heightmapTextureIndex;
                        tp.scaleY = packet.scaleY;
                        tp.tessellationFactor = packet.tessellationFactor;
                        tp.layerWorldSize = packet.layerWorldSize;
                        vkCmdPushConstants(cmd, terrainShadowPipelineLayout_,
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                               VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(tp), &tp);
                        vkCmdDrawIndexed(cmd, packet.indexCount, 1, 0, 0, 0);
                        continue;
                    }
                    bool isSkinned = (packet.boneDescriptorSet != VK_NULL_HANDLE);

                    if (isSkinned) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sun && shadowSkinnedSunPipeline_ ? shadowSkinnedSunPipeline_ : shadowSkinnedPipeline_);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowSkinnedPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowSkinnedPipelineLayout_, 1, 1, &packet.boneDescriptorSet, 0, nullptr);
                    } else {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sun && shadowSunPipeline_ ? shadowSunPipeline_ : shadowPipeline_);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
                    }

                    VkBuffer vbs[] = {packet.vertexBuffer};
                    VkDeviceSize off[] = {0};
                    vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
                    if (packet.skinVertexBuffer != VK_NULL_HANDLE) {
                        VkBuffer skinVBs[] = {packet.skinVertexBuffer};
                        VkDeviceSize skinOffs[] = {0};
                        vkCmdBindVertexBuffers(cmd, 1, 1, skinVBs, skinOffs);
                    }
                    if (packet.indexBuffer != VK_NULL_HANDLE)
                        vkCmdBindIndexBuffer(cmd, packet.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                    struct ShadowPush { Matrix4 model; Matrix4 lvp; float alphaCutoff; uint32_t texSet; float _pad[2]; } push;
                    push.model = packet.transform;
                    push.lvp = lvp;
                    push.alphaCutoff = packet.material && packet.material->isTransparent() ? 0.5f : 0.0f;
                    push.texSet = packet.diffuseTextureIndex;
                    push._pad[0] = 0; push._pad[1] = 0;

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

        auto getTextureIndex = [this](const std::string& name) -> uint32_t {
            auto it = textureNameToIndex_.find(name);
            return it != textureNameToIndex_.end() ? it->second : 0;
        };

        Profiler::get().lap("render.cienie (nagrywanie)");
        // === GBUFFER PASS ===
        // Transition GBuffer from SHADER_READ to COLOR_ATTACHMENT
        VkImageMemoryBarrier gbToColorBarriers[4] = {};
        for (int a = 0; a < kGBufferAttachments; a++) {
            gbToColorBarriers[a].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            gbToColorBarriers[a].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            gbToColorBarriers[a].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            gbToColorBarriers[a].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            gbToColorBarriers[a].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            gbToColorBarriers[a].image = gbufferImages_[a][currentFrame_];
            gbToColorBarriers[a].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            gbToColorBarriers[a].subresourceRange.levelCount = 1;
            gbToColorBarriers[a].subresourceRange.layerCount = 1;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 4, gbToColorBarriers);

        // Depth swapchaina: zawartość z poprzedniego użycia nie jest potrzebna (loadOp = CLEAR),
        // więc przechodzimy z UNDEFINED. Obsługuje to też pierwsze użycie obrazu po (re)kreacji.
        VkImageMemoryBarrier depthInitBarrier{};
        depthInitBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthInitBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthInitBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthInitBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthInitBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthInitBarrier.image = swapchain_->depthImage(imageIndex);
        depthInitBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthInitBarrier.subresourceRange.levelCount = 1;
        depthInitBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &depthInitBarrier);

        VkRenderingAttachmentInfo gbColorAttachments[4] = {};
        for (int a = 0; a < kGBufferAttachments; a++) {
            gbColorAttachments[a].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            gbColorAttachments[a].imageView = gbufferViews_[a][currentFrame_];
            gbColorAttachments[a].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            gbColorAttachments[a].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            gbColorAttachments[a].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            float clearZero[4] = {0,0,0,0};
            memcpy(gbColorAttachments[a].clearValue.color.float32, clearZero, sizeof(clearZero));
        }

        VkRenderingAttachmentInfo gbDepthAttachment{};
        gbDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        gbDepthAttachment.imageView = swapchain_->depthImageView(imageIndex);
        gbDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        gbDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        gbDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        gbDepthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo gbRenderingInfo{};
        gbRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        gbRenderingInfo.renderArea = {{0, 0}, swapchain_->extent()};
        gbRenderingInfo.layerCount = 1;
        gbRenderingInfo.colorAttachmentCount = kGBufferAttachments;
        gbRenderingInfo.pColorAttachments = gbColorAttachments;
        gbRenderingInfo.pDepthAttachment = &gbDepthAttachment;

        vkCmdBeginRendering(cmd, &gbRenderingInfo);

        VkViewport viewport{0.0f, 0.0f,
            static_cast<float>(swapchain_->extent().width),
            static_cast<float>(swapchain_->extent().height),
            0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, swapchain_->extent()};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // GBuffer: render opaque geometry (non-skinned)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline_);

        const auto& opaquePackets = scene.getOpaquePackets();
        Profiler::get().count("paczki nieprzezroczyste", static_cast<double>(opaquePackets.size()));
        Profiler::get().count("paczki cieni", static_cast<double>(scene.getShadowPackets().size()));

        for (const auto& packet : opaquePackets) {
            if (packet.vertexBuffer == VK_NULL_HANDLE) continue;
            if (packet.forwardOpaquePass) continue;

            bool isSkinned = (packet.boneDescriptorSet != VK_NULL_HANDLE);

            if (isSkinned) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferSkinnedPipeline_);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferSkinnedPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferSkinnedPipelineLayout_, 1, 1, &packet.boneDescriptorSet, 0, nullptr);
            } else {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline_);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
            }

            VkBuffer vbs[] = {packet.vertexBuffer};
            VkDeviceSize off[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);

            if (packet.skinVertexBuffer != VK_NULL_HANDLE) {
                VkBuffer skinVBs[] = {packet.skinVertexBuffer};
                VkDeviceSize skinOffs[] = {0};
                vkCmdBindVertexBuffers(cmd, 1, 1, skinVBs, skinOffs);
            }
            if (packet.indexBuffer != VK_NULL_HANDLE)
                vkCmdBindIndexBuffer(cmd, packet.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            struct {
                Matrix4 m;
                std::array<float,4> bc;
                float met;
                float rough;
                float bump;
                uint32_t bts;
                uint32_t pts;
                uint32_t nts;
                uint32_t ots;
                uint32_t ets;
                float amc;
                uint32_t layerBlendEnabled;
                uint32_t layerCount;
                float layerWorldSize;
                uint32_t layerBaseTextureSets[kMaxTerrainLayers];
                uint32_t layerMaskTextureSets[kMaxTerrainLayers];
            } push{};
            push.m = packet.transform; push.bc = packet.materialColor;
            push.met = packet.metallicFactor; push.rough = packet.roughnessFactor;
            push.bump = packet.bumpIntensity; push.bts = packet.diffuseTextureIndex;
            push.pts = packet.metallicRoughnessTextureIndex; push.nts = packet.normalTextureIndex;
            push.ots = packet.occlusionTextureIndex; push.ets = packet.emissiveTextureIndex;
            push.amc = 0.5f;
            push.layerBlendEnabled = packet.layerBlendEnabled ? 1u : 0u;
            push.layerCount = packet.layerCount;
            push.layerWorldSize = packet.layerWorldSize;
            for (uint32_t i = 0; i < kMaxTerrainLayers; ++i) {
                push.layerBaseTextureSets[i] = packet.layerTextureIndices[i];
                push.layerMaskTextureSets[i] = packet.layerMaskIndices[i];
            }

            VkPipelineLayout layout = isSkinned ? gbufferSkinnedPipelineLayout_ : gbufferPipelineLayout_;
            vkCmdPushConstants(cmd, layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push), &push);

            if (packet.indexBuffer != VK_NULL_HANDLE)
                vkCmdDrawIndexed(cmd, packet.indexCount, 1, 0, 0, 0);
        }
        vkCmdEndRendering(cmd);

        // Barrier: GBuffer color → SHADER_READ, depth stays as DEPTH_ATTACHMENT for swapchain
        VkImageMemoryBarrier gbToReadBarriers[4] = {};
        for (int a = 0; a < kGBufferAttachments; a++) {
            gbToReadBarriers[a].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            gbToReadBarriers[a].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            gbToReadBarriers[a].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            gbToReadBarriers[a].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            gbToReadBarriers[a].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            gbToReadBarriers[a].image = gbufferImages_[a][currentFrame_];
            gbToReadBarriers[a].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            gbToReadBarriers[a].subresourceRange.levelCount = 1;
            gbToReadBarriers[a].subresourceRange.layerCount = 1;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 4, gbToReadBarriers);

        Profiler::get().lap("render.gbuffer (nagrywanie)");
        // === SWAPCHAIN RENDERING ===
        // Transition swapchain color image
        VkImageMemoryBarrier colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.srcAccessMask = 0;
        colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.image = swapchain_->images()[imageIndex];
        colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorBarrier.subresourceRange.levelCount = 1;
        colorBarrier.subresourceRange.layerCount = 1;
        // srcStage musi być COLOR_ATTACHMENT_OUTPUT (etap, na którym czeka semafor acquire),
        // inaczej przejście layoutu może się wykonać, zanim silnik prezentacji zwolni obraz.
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &colorBarrier);

        // During GBuffer pass, depth was written (from clear=1.0, then opaque depths < 1.0).
        // The depth attachment stays in DEPTH_ATTACHMENT layout — no barrier needed.
        VkRenderingAttachmentInfo swColorAttachment{};
        swColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swColorAttachment.imageView = swapchain_->imageView(imageIndex);
        swColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        swColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        swColorAttachment.clearValue.color = {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};

        VkRenderingAttachmentInfo swDepthAttachment{};
        swDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swDepthAttachment.imageView = swapchain_->depthImageView(imageIndex);
        swDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        swDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        swDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, swapchain_->extent()};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &swColorAttachment;
        renderingInfo.pDepthAttachment = &swDepthAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);

        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // PART 1: Render scene (without ocean) to swapchain
        // ===================================================
        executeFeatures(cmd, view, IRenderFeature::Stage::BeforeOpaque);

        // Deferred lighting pass (fullscreen, reads GBuffer)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipelineLayout_, 1, 1, &deferredDescriptorSets_[currentFrame_], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        for (auto& pass : passes_) {
            if (pass->getName() == "OpaquePass") {
                pass->execute(cmd, currentFrame_, scene, descriptorSets_[currentFrame_]);
            }
        }

        executeFeatures(cmd, view, IRenderFeature::Stage::AfterOpaque);

        // Wstępny zapis głębokości przezroczystych obiektów z Material::depthPrepass - tutaj, bo w części 2
        // bufor głębokości jest tylko do odczytu (próbkuje go ocean).
        for (auto& pass : passes_) {
            if (pass->getName() == "TransparentDepthPrepass") {
                pass->execute(cmd, currentFrame_, scene, descriptorSets_[currentFrame_]);
            }
        }

        // End first pass — scene render to swapchain is done
        vkCmdEndRendering(cmd);

        // Copy swapchain content to scene color texture for ocean reflection/refraction
        VkImageMemoryBarrier sceneBarriers[4] = {};

        // Swapchain: COLOR_ATTACHMENT → TRANSFER_SRC
        sceneBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        sceneBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sceneBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sceneBarriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        sceneBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        sceneBarriers[0].image = swapchain_->images()[imageIndex];
        sceneBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        sceneBarriers[0].subresourceRange.levelCount = 1;
        sceneBarriers[0].subresourceRange.layerCount = 1;

        // Scene color: SHADER_READ_ONLY → TRANSFER_DST
        sceneBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        sceneBarriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sceneBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sceneBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneBarriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        sceneBarriers[1].image = sceneColorImages_[currentFrame_];
        sceneBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        sceneBarriers[1].subresourceRange.levelCount = 1;
        sceneBarriers[1].subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 2, sceneBarriers);

        // Copy swapchain → scene color
        VkImageCopy copyRegion{};
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.extent = {swapchain_->extent().width, swapchain_->extent().height, 1};
        vkCmdCopyImage(cmd,
            swapchain_->images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            sceneColorImages_[currentFrame_], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copyRegion);

        // Scene color: TRANSFER_DST → SHADER_READ_ONLY
        sceneBarriers[2].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        sceneBarriers[2].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sceneBarriers[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sceneBarriers[2].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        sceneBarriers[2].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneBarriers[2].image = sceneColorImages_[currentFrame_];
        sceneBarriers[2].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        sceneBarriers[2].subresourceRange.levelCount = 1;
        sceneBarriers[2].subresourceRange.layerCount = 1;

        // Swapchain: TRANSFER_SRC → COLOR_ATTACHMENT
        sceneBarriers[3].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        sceneBarriers[3].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        // Część 2 wczytuje kolor (loadOp = LOAD) i blenduje, więc potrzebny jest też odczyt.
        sceneBarriers[3].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sceneBarriers[3].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        sceneBarriers[3].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        sceneBarriers[3].image = swapchain_->images()[imageIndex];
        sceneBarriers[3].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        sceneBarriers[3].subresourceRange.levelCount = 1;
        sceneBarriers[3].subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 2, &sceneBarriers[2]);

        // Update bindless descriptors for this frame
        registerSceneColorForFrame(currentFrame_);

        // Transition swapchain depth from ATTACHMENT to READ_ONLY for SSR sampling
        VkImageMemoryBarrier depthReadBarrier{};
        depthReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthReadBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        // Depth jest w części 2 próbkowany przez shadery ORAZ używany do testu głębi (loadOp = LOAD).
        depthReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        depthReadBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthReadBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthReadBarrier.image = swapchain_->depthImage(imageIndex);
        depthReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthReadBarrier.subresourceRange.levelCount = 1;
        depthReadBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &depthReadBarrier);

        registerSceneDepthForFrame(currentFrame_, imageIndex);
        registerGBufferWorldPosForFrame(currentFrame_);

        // PART 2: Continue rendering on swapchain — ocean + transparent + overlay
        // =====================================================================
        VkRenderingAttachmentInfo swColorAttachment2{};
        swColorAttachment2.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swColorAttachment2.imageView = swapchain_->imageView(imageIndex);
        swColorAttachment2.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swColorAttachment2.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        swColorAttachment2.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        // Depth in READ_ONLY layout — can be sampled by shaders while still used for depth test
        VkRenderingAttachmentInfo swDepthAttachment2{};
        swDepthAttachment2.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swDepthAttachment2.imageView = swapchain_->depthImageView(imageIndex);
        swDepthAttachment2.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        swDepthAttachment2.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        // Depth jest tu tylko do odczytu — STORE_OP_NONE (Vulkan 1.3) nie generuje zapisu na końcu przebiegu.
        swDepthAttachment2.storeOp = VK_ATTACHMENT_STORE_OP_NONE;

        VkRenderingInfo renderingInfo2{};
        renderingInfo2.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo2.renderArea = {{0, 0}, swapchain_->extent()};
        renderingInfo2.layerCount = 1;
        renderingInfo2.colorAttachmentCount = 1;
        renderingInfo2.pColorAttachments = &swColorAttachment2;
        renderingInfo2.pDepthAttachment = &swDepthAttachment2;

        vkCmdBeginRendering(cmd, &renderingInfo2);

        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        executeFeatures(cmd, view, IRenderFeature::Stage::AfterTransparent);

        for (auto& pass : passes_) {
            if (pass->getName() == "TransparentPass") {
                pass->execute(cmd, currentFrame_, scene, descriptorSets_[currentFrame_]);
            }
        }

        vkCmdEndRendering(cmd);

        // PART 3: Overlay (GUI, tekst) — własny przebieg z wyczyszczonym, zapisywalnym depth.
        // Część 2 ma depth w layoucie READ_ONLY (próbkowany przez ocean), więc nie wolno w niej
        // ani czyścić depth, ani do niego pisać.
        if (hasActiveFeature(IRenderFeature::Stage::Overlay)) {
            VkImageMemoryBarrier overlayDepthBarrier{};
            overlayDepthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            overlayDepthBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            overlayDepthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            overlayDepthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            overlayDepthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            overlayDepthBarrier.image = swapchain_->depthImage(imageIndex);
            overlayDepthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            overlayDepthBarrier.subresourceRange.levelCount = 1;
            overlayDepthBarrier.subresourceRange.layerCount = 1;
            // Kolor: zapisy części 2 muszą być widoczne dla loadOp = LOAD części 3.
            VkImageMemoryBarrier overlayColorBarrier{};
            overlayColorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            overlayColorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            overlayColorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            overlayColorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            overlayColorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            overlayColorBarrier.image = swapchain_->images()[imageIndex];
            overlayColorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            overlayColorBarrier.subresourceRange.levelCount = 1;
            overlayColorBarrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &overlayColorBarrier);

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &overlayDepthBarrier);

            VkRenderingAttachmentInfo overlayDepthAttachment{};
            overlayDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            overlayDepthAttachment.imageView = swapchain_->depthImageView(imageIndex);
            overlayDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            overlayDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            overlayDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            overlayDepthAttachment.clearValue.depthStencil = {1.0f, 0};

            VkRenderingInfo overlayInfo = renderingInfo2;
            overlayInfo.pDepthAttachment = &overlayDepthAttachment;
            vkCmdBeginRendering(cmd, &overlayInfo);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            executeFeatures(cmd, view, IRenderFeature::Stage::Overlay);
            vkCmdEndRendering(cmd);
        }

        VkImageMemoryBarrier presentBarrier{};
        presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        presentBarrier.dstAccessMask = 0;
        presentBarrier.image = swapchain_->images()[imageIndex];
        presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        presentBarrier.subresourceRange.levelCount = 1;
        presentBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &presentBarrier);

        vkEndCommandBuffer(cmd);
        Profiler::get().lap("render.deferred+przezr.+GUI (nagrywanie)");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        if (synchronizationMode_ == SynchronizationMode::Timeline) {
            uint64_t timelineValue = ++globalTimelineCounter_;
            timelineCounters_[currentFrame_] = timelineValue;

            VkSemaphore signalSemaphores[] = {presentReadySemaphores_[imageIndex], renderTimelineSemaphores_[currentFrame_]};
            submitInfo.signalSemaphoreCount = 2;
            submitInfo.pSignalSemaphores = signalSemaphores;

            VkTimelineSemaphoreSubmitInfo timelineInfo{};
            timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
            uint64_t waitValues[] = {0};
            uint64_t signalValues[] = {0, timelineValue};
            timelineInfo.waitSemaphoreValueCount = 1;
            timelineInfo.pWaitSemaphoreValues = waitValues;
            timelineInfo.signalSemaphoreValueCount = 2;
            timelineInfo.pSignalSemaphoreValues = signalValues;
            submitInfo.pNext = &timelineInfo;

            vkQueueSubmit(vulkanContext_->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        } else {
            VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[imageIndex]};
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;
            vkQueueSubmit(vulkanContext_->graphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]);
        }
        VkSemaphore presentWaitSemaphore = (synchronizationMode_ == SynchronizationMode::Timeline)
            ? presentReadySemaphores_[imageIndex] : renderFinishedSemaphores_[imageIndex];
        Profiler::get().lap("render.submit");
        if (!swapchain_->present(imageIndex, presentWaitSemaphore))
            recreateSwapChain();
        Profiler::get().lap("render.present");
        currentFrame_ = (currentFrame_ + 1) % kMaxFrames;
    }

    TextureHandle loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat, bool disableAlpha = false) {
        VkDevice device = vulkanContext_->device();
        
        // Clean up existing texture with the same name to prevent memory leak on reload
        auto oldIt = textures_.find(name);
        if (oldIt != textures_.end()) {
            // Stara tekstura może być używana przez klatki w locie. Przeładowanie jest rzadkie,
            // więc zamiast kolejki opóźnionego niszczenia czekamy na GPU.
            vkDeviceWaitIdle(device);
            vkDestroyImageView(device, oldIt->second.imageView, nullptr);
            vkDestroyImage(device, oldIt->second.image, nullptr);
            vkFreeMemory(device, oldIt->second.imageMemory, nullptr);
            vkDestroySampler(device, oldIt->second.sampler, nullptr);
            textures_.erase(oldIt);
        }

        VkPhysicalDevice physicalDevice = vulkanContext_->physicalDevice();
        TextureUploadData uploadData = TextureAssetLoader::load(path);

        bool needsMipmapGen = (uploadData.mipLevels <= 1);
        if (needsMipmapGen) {
            uint32_t maxDim = std::max(uploadData.width, uploadData.height);
            uploadData.mipLevels = 1;
            while (maxDim > 1) { maxDim /= 2; uploadData.mipLevels++; }
        }

        TextureResources tex;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (needsMipmapGen) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        vk::createImage(device, physicalDevice,
                        uploadData.width, uploadData.height, uploadData.mipLevels,
                        uploadData.format, usage,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        tex.image, tex.imageMemory);

        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                  tex.image, uploadData.format,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  uploadData.mipLevels);

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        VkDeviceSize dataSize = uploadData.pixelBytes.size();
        vk::createBuffer(device, physicalDevice, dataSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingMemory);

        void* mappedMemory;
        vkMapMemory(device, stagingMemory, 0, dataSize, 0, &mappedMemory);
        std::memcpy(mappedMemory, uploadData.pixelBytes.data(), dataSize);
        vkUnmapMemory(device, stagingMemory);

        VkCommandBuffer cmd = beginSingleTimeCommands();
        vkCmdCopyBufferToImage(cmd, stagingBuffer, tex.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(uploadData.copyRegions.size()),
                               uploadData.copyRegions.data());

        if (needsMipmapGen) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = tex.image;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;

            int32_t mipWidth = static_cast<int32_t>(uploadData.width);
            int32_t mipHeight = static_cast<int32_t>(uploadData.height);

            for (uint32_t i = 1; i < uploadData.mipLevels; i++) {
                barrier.subresourceRange.baseMipLevel = i - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &barrier);

                VkImageBlit blit{};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i-1;
                blit.srcSubresource.layerCount = 1;
                blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.layerCount = 1;
                blit.dstOffsets[1] = {mipWidth>1?mipWidth/2:1,
                                      mipHeight>1?mipHeight/2:1, 1};
                vkCmdBlitImage(cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &blit, VK_FILTER_LINEAR);

                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &barrier);

                if (mipWidth > 1) mipWidth /= 2;
                if (mipHeight > 1) mipHeight /= 2;
            }

            barrier.subresourceRange.baseMipLevel = uploadData.mipLevels - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &barrier);
        }

        endSingleTimeCommands(cmd);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        if (!needsMipmapGen)
            vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                      tex.image, uploadData.format,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      uploadData.mipLevels);

        if (disableAlpha) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = tex.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = uploadData.format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = uploadData.mipLevels;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_ONE;

            if (vkCreateImageView(device, &viewInfo, nullptr, &tex.imageView) != VK_SUCCESS) {
                throw std::runtime_error("failed to create image view with component swizzle");
            }
        } else {
            tex.imageView = vk::createImageView(device, tex.image, uploadData.format,
                                                VK_IMAGE_ASPECT_COLOR_BIT, uploadData.mipLevels);
        }
        tex.sampler = vk::createSampler(device, vulkanContext_->physicalDevice(), uploadData.mipLevels,
                        textureFilter_, mirroredRepeat ? AddressMode::MirroredRepeat : AddressMode::Repeat);
        tex.mipLevels = uploadData.mipLevels;
        tex.addressMode = mirroredRepeat ? AddressMode::MirroredRepeat : AddressMode::Repeat;
        textures_[name] = tex;
        
        registerTextureInBindless(name, tex.imageView, tex.sampler);
        LOG_INFO("Loaded texture: {} at index {}", name, textureNameToIndex_[name]);

        if (assetRegistry_) {
            return assetRegistry_->registerTexture(name);
        }
        return {};
    }

    TextureHandle loadTextureFromMemory(const std::string& name, const std::vector<float>& data, uint32_t width, uint32_t height, bool mirroredRepeat) {
        VkDevice device = vulkanContext_->device();
        
        // Clean up existing texture with the same name to prevent memory leak on reload
        auto oldIt = textures_.find(name);
        if (oldIt != textures_.end()) {
            // Stara tekstura może być używana przez klatki w locie. Przeładowanie jest rzadkie,
            // więc zamiast kolejki opóźnionego niszczenia czekamy na GPU.
            vkDeviceWaitIdle(device);
            vkDestroyImageView(device, oldIt->second.imageView, nullptr);
            vkDestroyImage(device, oldIt->second.image, nullptr);
            vkFreeMemory(device, oldIt->second.imageMemory, nullptr);
            vkDestroySampler(device, oldIt->second.sampler, nullptr);
            textures_.erase(oldIt);
        }

        VkPhysicalDevice physicalDevice = vulkanContext_->physicalDevice();

        // Dane 0..255 (mapa wysokości, maski) -> 16-bit UNORM z zaokrągleniem. Wcześniej było 8 bit z obcięciem:
        // teren przesuwany na GPU leżał w "schodkach" do 1 jednostki mapy niżej niż siatka CPU, która rzuca cienie
        // - siatka cieni wystawała ponad widoczny teren i rzucała pasy cieni wzdłuż schodków.
        TextureUploadData uploadData{};
        uploadData.width = width;
        uploadData.height = height;
        uploadData.mipLevels = 1;
        uploadData.format = VK_FORMAT_R16_UNORM;
        uploadData.pixelBytes.resize(static_cast<size_t>(width) * height * 2);
        for (uint32_t i = 0; i < width * height; ++i) {
            const float val = std::clamp(data[i], 0.0f, 255.0f);
            const auto v = static_cast<uint16_t>(std::lround(val / 255.0f * 65535.0f));
            uploadData.pixelBytes[i * 2] = static_cast<uint8_t>(v & 0xFF);
            uploadData.pixelBytes[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
        }

        // Compute full mip chain
        uint32_t maxDim = std::max(width, height);
        uploadData.mipLevels = 1;
        while (maxDim > 1) { maxDim /= 2; uploadData.mipLevels++; }

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};
        uploadData.copyRegions.push_back(region);

        TextureResources tex;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        vk::createImage(device, physicalDevice,
                        uploadData.width, uploadData.height, uploadData.mipLevels,
                        uploadData.format, usage,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        tex.image, tex.imageMemory);

        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                  tex.image, uploadData.format,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  uploadData.mipLevels);

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        VkDeviceSize dataSize = uploadData.pixelBytes.size();
        vk::createBuffer(device, physicalDevice, dataSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingMemory);

        void* mappedMemory;
        vkMapMemory(device, stagingMemory, 0, dataSize, 0, &mappedMemory);
        std::memcpy(mappedMemory, uploadData.pixelBytes.data(), dataSize);
        vkUnmapMemory(device, stagingMemory);

        VkCommandBuffer cmd = beginSingleTimeCommands();
        vkCmdCopyBufferToImage(cmd, stagingBuffer, tex.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(uploadData.copyRegions.size()),
                               uploadData.copyRegions.data());

        // Generate mip chain via blit
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = tex.image;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;

            int32_t mipW = static_cast<int32_t>(width);
            int32_t mipH = static_cast<int32_t>(height);

            for (uint32_t i = 1; i < uploadData.mipLevels; i++) {
                barrier.subresourceRange.baseMipLevel = i - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &barrier);

                VkImageBlit blit{};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i-1;
                blit.srcSubresource.layerCount = 1;
                blit.srcOffsets[1] = {mipW, mipH, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.layerCount = 1;
                blit.dstOffsets[1] = {mipW>1?mipW/2:1, mipH>1?mipH/2:1, 1};
                vkCmdBlitImage(cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &blit, VK_FILTER_LINEAR);

                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &barrier);

                if (mipW > 1) mipW /= 2;
                if (mipH > 1) mipH /= 2;
            }

            barrier.subresourceRange.baseMipLevel = uploadData.mipLevels - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &barrier);
        }

        endSingleTimeCommands(cmd);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        tex.imageView = vk::createImageView(device, tex.image, uploadData.format,
                                            VK_IMAGE_ASPECT_COLOR_BIT, uploadData.mipLevels);
        tex.sampler = vk::createSampler(device, vulkanContext_->physicalDevice(), uploadData.mipLevels,
                        textureFilter_, mirroredRepeat ? AddressMode::MirroredRepeat : AddressMode::Repeat);
        tex.mipLevels = uploadData.mipLevels;
        tex.addressMode = mirroredRepeat ? AddressMode::MirroredRepeat : AddressMode::Repeat;
        textures_[name] = tex;
        
        registerTextureInBindless(name, tex.imageView, tex.sampler);
        LOG_INFO("Loaded memory texture: {} at index {} (mipLevels={})", name, textureNameToIndex_[name], uploadData.mipLevels);

        if (assetRegistry_) {
            return assetRegistry_->registerTexture(name);
        }
        return {};
    }

    void registerTextureInBindless(const std::string& name, VkImageView imageView, VkSampler sampler) {
        VkDevice device = vulkanContext_->device();
        uint32_t index = 0;
        auto it = textureNameToIndex_.find(name);
        if (it == textureNameToIndex_.end()) {
            index = nextTextureIndex_++;
            textureNameToIndex_[name] = index;
        } else {
            index = it->second;
        }

        if (assetRegistry_) {
            const auto handle = assetRegistry_->registerTexture(name);
            if (handle.isValid()) {
                if (handle.value >= textureHandleToIndex_.size()) {
                    textureHandleToIndex_.resize(handle.value + 1, 0);
                }
                textureHandleToIndex_[handle.value] = index;
            }
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        for (int i = 0; i < kMaxFrames; i++) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSets_[i];
            write.dstBinding = 1;
            write.dstArrayElement = index;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    void allocateDescriptorSet(const std::string& name) {
        // No longer needed for per-texture sets
    }

    // Getters for sub-renderers
    VkDevice device() const { return vulkanContext_->device(); }
    VkPhysicalDevice physicalDevice() const { return vulkanContext_->physicalDevice(); }
    VkFormat colorFormat() const { return swapchain_ ? swapchain_->imageFormat() : VK_FORMAT_UNDEFINED; }
    VkFormat depthFormat() const { return swapchain_ ? swapchain_->depthFormat() : VK_FORMAT_UNDEFINED; }

    VkDescriptorSetLayout descriptorSetLayout() const { return descriptorSetLayout_; }
    VkCommandPool commandPool() const { return vulkanContext_->commandPool(); }
    VkQueue graphicsQueue() const { return vulkanContext_->graphicsQueue(); }
    VkExtent2D extent() const { return swapchain_ ? swapchain_->extent() : VkExtent2D{0,0}; }
    void setTessellationEnabled(bool enabled) { tessellationEnabled_ = enabled; }
    bool isTessellationEnabled() const { return tessellationEnabled_; }
    bool isTessellationSupported() const { return vulkanContext_ && vulkanContext_->isTessellationSupported(); }
    void setTessellationLevel(float level) { tessellationLevel_ = level; }
    float getTessellationLevel() const { return tessellationLevel_; }
    void setTextureFilter(TextureFilter filter) {
        if (textureFilter_ == filter) return;
        textureFilter_ = filter;
        VkDevice device = vulkanContext_->device();
        // Niszczymy samplery i nadpisujemy deskryptory, których mogą używać klatki w locie.
        vkDeviceWaitIdle(device);
        for (auto& [name, tex] : textures_) {
            vkDestroySampler(device, tex.sampler, nullptr);
            tex.sampler = vk::createSampler(device, vulkanContext_->physicalDevice(), tex.mipLevels, filter, tex.addressMode);
            uint32_t index = getTextureIndex(name);
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = tex.imageView;
            imageInfo.sampler = tex.sampler;
            for (int i = 0; i < kMaxFrames; i++) {
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptorSets_[i];
                write.dstBinding = 1;
                write.dstArrayElement = index;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfo;
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            }
        }
    }
    TextureFilter getTextureFilter() const { return textureFilter_; }
    
    uint32_t getTextureIndex(const std::string& name) const {
        auto it = textureNameToIndex_.find(name);
        if (it != textureNameToIndex_.end()) return it->second;
        return 0;
    }

    uint32_t getTextureIndex(TextureHandle handle) const {
        if (!handle.isValid()) {
            return 0;
        }
        if (handle.value < textureHandleToIndex_.size()) {
            return textureHandleToIndex_[handle.value];
        }
        return 0;
    }

    void prepareRenderPacket(RenderPacket& packet) {
        packet.forwardOpaquePass = false;
        const bool wantsLayeredForward = packet.layerBlendEnabled;
        if (materialManager_) {
            if (const Material* materialData = materialManager_->get(packet.materialHandle)) {
                const std::string& shaderProgram = materialData->shaderProgram;
                const bool usesCustomOpaqueShader =
                    !shaderProgram.empty() &&
                    shaderProgram != "default" &&
                    shaderProgram != "gbuffer" &&
                    shaderProgram != "gbuffer_skinned";
                if ((usesCustomOpaqueShader || wantsLayeredForward) && materialData->blendMode == BlendMode::Disabled) {
                    packet.forwardOpaquePass = true;
                }
            }
        }

        packet.diffuseTextureIndex = packet.diffuseTextureHandle.isValid()
            ? getTextureIndex(packet.diffuseTextureHandle)
            : defaultDiffuseIndex_;

        if (packet.normalTextureHandle.isValid()) {
            packet.normalTextureIndex = getTextureIndex(packet.normalTextureHandle);
        } else {
            packet.normalTextureIndex = defaultNormalIndex_;
            packet.bumpIntensity = 0.0f;
        }

        packet.metallicRoughnessTextureIndex = packet.metallicRoughnessTextureHandle.isValid()
            ? getTextureIndex(packet.metallicRoughnessTextureHandle)
            : defaultOrmIndex_;

        packet.occlusionTextureIndex = packet.occlusionTextureHandle.isValid()
            ? getTextureIndex(packet.occlusionTextureHandle)
            : defaultDiffuseIndex_;

        packet.emissiveTextureIndex = packet.emissiveTextureHandle.isValid()
            ? getTextureIndex(packet.emissiveTextureHandle)
            : defaultEmissiveIndex_;

        packet.windTime = windTime_;
        packet.windStrength = windStrength_;
        packet.layerCount = std::min(packet.layerCount, kMaxTerrainLayers);
        for (uint32_t i = 0; i < kMaxTerrainLayers; ++i) {
            packet.layerTextureIndices[i] = defaultDiffuseIndex_;
            packet.layerMaskIndices[i] = defaultMaskIndex_;
        }

        if (packet.layerBlendEnabled && packet.layerCount > 0) {
            for (uint32_t i = 0; i < packet.layerCount; ++i) {
                packet.layerTextureIndices[i] = packet.layerTextureHandles[i].isValid()
                    ? getTextureIndex(packet.layerTextureHandles[i])
                    : defaultDiffuseIndex_;
                if (i > 0 && packet.layerMaskHandles[i].isValid()) {
                    packet.layerMaskIndices[i] = getTextureIndex(packet.layerMaskHandles[i]);
                }
            }
        }
        packet.heightmapTextureIndex = packet.heightmapTextureHandle.isValid()
            ? getTextureIndex(packet.heightmapTextureHandle)
            : 0;
    }

    VkDescriptorSet getDescriptorSet(uint32_t frame) const {
        return frame < descriptorSets_.size() ? descriptorSets_[frame] : VK_NULL_HANDLE;
    }

    bool getTextureInfo(const std::string& name, VkImageView& v, VkSampler& s) const {
        auto it = textures_.find(name);
        if (it == textures_.end()) return false;
        v = it->second.imageView;
        s = it->second.sampler;
        return v != VK_NULL_HANDLE && s != VK_NULL_HANDLE;
    }

    bool getTextureInfo(TextureHandle handle, VkImageView& v, VkSampler& s) const {
        if (!handle.isValid() || !assetRegistry_) {
            return false;
        }

        const auto name = assetRegistry_->textureName(handle);
        if (!name.has_value()) {
            return false;
        }

        return getTextureInfo(*name, v, s);
    }

    VkBuffer getUniformBuffer(uint32_t frame) const {
        return frame < uniformBuffers_.size() ? uniformBuffers_[frame] : VK_NULL_HANDLE;
    }

    void waitIdle() {
        if (vulkanContext_ && vulkanContext_->device()) {
            vkDeviceWaitIdle(vulkanContext_->device());
        }
    }

    void cleanupGBuffer() {
        VkDevice device = vulkanContext_->device();
        for (int f = 0; f < kMaxFrames; f++) {
            for (int a = 0; a < kGBufferAttachments; a++) {
                if (gbufferViews_[a][f]) { vkDestroyImageView(device, gbufferViews_[a][f], nullptr); gbufferViews_[a][f] = VK_NULL_HANDLE; }
                if (gbufferImages_[a][f]) { vkDestroyImage(device, gbufferImages_[a][f], nullptr); gbufferImages_[a][f] = VK_NULL_HANDLE; }
                if (gbufferMemories_[a][f]) { vkFreeMemory(device, gbufferMemories_[a][f], nullptr); gbufferMemories_[a][f] = VK_NULL_HANDLE; }
            }
        }
        if (gbufferSampler_) { vkDestroySampler(device, gbufferSampler_, nullptr); gbufferSampler_ = VK_NULL_HANDLE; }
        if (gbufferPipeline_) { vkDestroyPipeline(device, gbufferPipeline_, nullptr); gbufferPipeline_ = VK_NULL_HANDLE; }
        if (gbufferPipelineLayout_) { vkDestroyPipelineLayout(device, gbufferPipelineLayout_, nullptr); gbufferPipelineLayout_ = VK_NULL_HANDLE; }
        if (gbufferSkinnedPipeline_) { vkDestroyPipeline(device, gbufferSkinnedPipeline_, nullptr); gbufferSkinnedPipeline_ = VK_NULL_HANDLE; }
        if (gbufferSkinnedPipelineLayout_) { vkDestroyPipelineLayout(device, gbufferSkinnedPipelineLayout_, nullptr); gbufferSkinnedPipelineLayout_ = VK_NULL_HANDLE; }
        if (deferredPipeline_) { vkDestroyPipeline(device, deferredPipeline_, nullptr); deferredPipeline_ = VK_NULL_HANDLE; }
        if (deferredPipelineLayout_) { vkDestroyPipelineLayout(device, deferredPipelineLayout_, nullptr); deferredPipelineLayout_ = VK_NULL_HANDLE; }
        if (deferredDescriptorSetLayout_) { vkDestroyDescriptorSetLayout(device, deferredDescriptorSetLayout_, nullptr); deferredDescriptorSetLayout_ = VK_NULL_HANDLE; }
        if (deferredDescriptorPool_) { vkDestroyDescriptorPool(device, deferredDescriptorPool_, nullptr); deferredDescriptorPool_ = VK_NULL_HANDLE; }
    }

    // ---------------------------------------------------------------------------------
    // Mgła wojny: tekstura widoczności R8 kFogOfWarSize^2, osobna na każdą klatkę w locie (CPU nadpisuje bufor
    // przejściowy swojej klatki i kopiuje go do jej obrazu na początku klatki - bez czekania na GPU i bez
    // wyścigu z klatką, która jeszcze czyta poprzednią wersję). Shader deferred przyciemnia piksele wg pozycji świata.
    // ---------------------------------------------------------------------------------
    static constexpr uint32_t kFowSize = Renderer::kFogOfWarSize;
    VkImage fowImage_[kMaxFrames]{};
    VkDeviceMemory fowImageMem_[kMaxFrames]{};
    VkImageView fowView_[kMaxFrames]{};
    VkBuffer fowStaging_[kMaxFrames]{};
    VkDeviceMemory fowStagingMem_[kMaxFrames]{};
    void* fowStagingPtr_[kMaxFrames]{};
    VkSampler fowSampler_ = VK_NULL_HANDLE;
    std::vector<uint8_t> fowData_ = std::vector<uint8_t>(kFowSize * kFowSize, 255);
    uint64_t fowVersion_ = 1;
    uint64_t fowUploaded_[kMaxFrames]{};
    uint32_t fowBindless_[kMaxFrames]{};   // indeksy w tablicy bindless (teren czyta mgłę stamtąd)
    bool fowEnabled_ = false;
    float fowOriginX_ = 0.0f, fowOriginZ_ = 0.0f, fowSize_ = 1.0f, fowExplored_ = 0.45f;

    void createFogOfWarResources() {
        VkDevice device = vulkanContext_->device();
        VkPhysicalDevice phys = vulkanContext_->physicalDevice();
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(kFowSize) * kFowSize;
        fowSampler_ = vk::createSampler(device, phys, 1, TextureFilter::Linear, AddressMode::ClampToEdge);
        for (int f = 0; f < kMaxFrames; ++f) {
            vk::createImage(device, phys, kFowSize, kFowSize, 1, VK_FORMAT_R8_UNORM,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            fowImage_[f], fowImageMem_[f]);
            fowView_[f] = vk::createImageView(device, fowImage_[f], VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
            vk::createBuffer(device, phys, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, fowStaging_[f], fowStagingMem_[f]);
            vkMapMemory(device, fowStagingMem_[f], 0, bytes, 0, &fowStagingPtr_[f]);   // mapowanie na stałe
            // Start: wszystko widoczne (gra bez mgły wojny wygląda jak dotąd).
            vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(), fowImage_[f],
                                      VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            std::memcpy(fowStagingPtr_[f], fowData_.data(), bytes);
            VkCommandBuffer cmd = beginSingleTimeCommands();
            recordFogOfWarCopy(cmd, f, false);
            endSingleTimeCommands(cmd);
            fowUploaded_[f] = fowVersion_;
            const std::string name = "fog_of_war_" + std::to_string(f);
            registerTextureInBindless(name, fowView_[f], fowSampler_);
            fowBindless_[f] = getTextureIndex(name);
        }
    }

    // Kopia bufora przejściowego klatki f do jej obrazu (z barierami: odczyt w shaderze <-> zapis transferu).
    void recordFogOfWarCopy(VkCommandBuffer cmd, int f, bool fromShaderRead) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = fowImage_[f];
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (fromShaderRead) {
            b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &b);
        }
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kFowSize, kFowSize, 1};
        vkCmdCopyBufferToImage(cmd, fowStaging_[f], fowImage_[f], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &b);
    }

    // Na początku klatki: gdy dane widoczności się zmieniły, przesyłamy je do obrazu tej klatki.
    void uploadFogOfWarIfChanged(VkCommandBuffer cmd) {
        const int f = currentFrame_;
        if (fowUploaded_[f] == fowVersion_) return;
        std::memcpy(fowStagingPtr_[f], fowData_.data(), fowData_.size());
        recordFogOfWarCopy(cmd, f, true);
        fowUploaded_[f] = fowVersion_;
    }

    void setFogOfWar(bool enabled, float originX, float originZ, float worldSize, float exploredBrightness) {
        fowEnabled_ = enabled;
        fowOriginX_ = originX;
        fowOriginZ_ = originZ;
        fowSize_ = std::max(1.0f, worldSize);
        fowExplored_ = exploredBrightness;
    }
    void updateFogOfWar(const std::vector<uint8_t>& visibility) {
        if (visibility.size() != fowData_.size()) return;
        fowData_ = visibility;
        ++fowVersion_;
    }

    void destroyFogOfWarResources() {
        VkDevice device = vulkanContext_->device();
        for (int f = 0; f < kMaxFrames; ++f) {
            if (fowView_[f]) vkDestroyImageView(device, fowView_[f], nullptr);
            if (fowImage_[f]) vkDestroyImage(device, fowImage_[f], nullptr);
            if (fowImageMem_[f]) vkFreeMemory(device, fowImageMem_[f], nullptr);
            if (fowStaging_[f]) vkDestroyBuffer(device, fowStaging_[f], nullptr);
            if (fowStagingMem_[f]) vkFreeMemory(device, fowStagingMem_[f], nullptr);   // zwalnia też mapowanie
            fowView_[f] = VK_NULL_HANDLE;
            fowImage_[f] = VK_NULL_HANDLE;
            fowImageMem_[f] = VK_NULL_HANDLE;
            fowStaging_[f] = VK_NULL_HANDLE;
            fowStagingMem_[f] = VK_NULL_HANDLE;
        }
        if (fowSampler_) vkDestroySampler(device, fowSampler_, nullptr);
        fowSampler_ = VK_NULL_HANDLE;
    }

    void cleanup() {
        if (cleanedUp_ || !vulkanContext_ || !vulkanContext_->device()) return;
        VkDevice device = vulkanContext_->device();
        vkDeviceWaitIdle(device);

        features_.clear();
        passes_.clear();

        destroySceneColorResources();
        cleanupGBuffer();
        destroyFogOfWarResources();
        if (shadowMapView_) { vkDestroyImageView(device, shadowMapView_, nullptr); shadowMapView_ = VK_NULL_HANDLE; }
        for (auto view : shadowMapLayerViews_) {
            if (view) vkDestroyImageView(device, view, nullptr);
        }
        shadowMapLayerViews_.clear();
        if (shadowMapImage_) { vkDestroyImage(device, shadowMapImage_, nullptr); shadowMapImage_ = VK_NULL_HANDLE; }
        if (shadowMapMemory_) { vkFreeMemory(device, shadowMapMemory_, nullptr); shadowMapMemory_ = VK_NULL_HANDLE; }
        if (shadowSampler_) { vkDestroySampler(device, shadowSampler_, nullptr); shadowSampler_ = VK_NULL_HANDLE; }
        if (shadowPipeline_) { vkDestroyPipeline(device, shadowPipeline_, nullptr); shadowPipeline_ = VK_NULL_HANDLE; }
        if (shadowSunPipeline_) { vkDestroyPipeline(device, shadowSunPipeline_, nullptr); shadowSunPipeline_ = VK_NULL_HANDLE; }
        if (terrainShadowPipeline_) { vkDestroyPipeline(device, terrainShadowPipeline_, nullptr); terrainShadowPipeline_ = VK_NULL_HANDLE; }
        if (terrainShadowSunPipeline_) { vkDestroyPipeline(device, terrainShadowSunPipeline_, nullptr); terrainShadowSunPipeline_ = VK_NULL_HANDLE; }
        if (terrainShadowPipelineLayout_) { vkDestroyPipelineLayout(device, terrainShadowPipelineLayout_, nullptr); terrainShadowPipelineLayout_ = VK_NULL_HANDLE; }
        if (shadowSkinnedSunPipeline_) { vkDestroyPipeline(device, shadowSkinnedSunPipeline_, nullptr); shadowSkinnedSunPipeline_ = VK_NULL_HANDLE; }
        if (shadowPipelineLayout_) { vkDestroyPipelineLayout(device, shadowPipelineLayout_, nullptr); shadowPipelineLayout_ = VK_NULL_HANDLE; }
        if (shadowSkinnedPipeline_) { vkDestroyPipeline(device, shadowSkinnedPipeline_, nullptr); shadowSkinnedPipeline_ = VK_NULL_HANDLE; }
        if (shadowSkinnedPipelineLayout_) { vkDestroyPipelineLayout(device, shadowSkinnedPipelineLayout_, nullptr); shadowSkinnedPipelineLayout_ = VK_NULL_HANDLE; }

        for (auto& [name, tex] : textures_) {
            if (tex.sampler) { vkDestroySampler(device, tex.sampler, nullptr); tex.sampler = VK_NULL_HANDLE; }
            if (tex.imageView) { vkDestroyImageView(device, tex.imageView, nullptr); tex.imageView = VK_NULL_HANDLE; }
            if (tex.image) {
                vkDestroyImage(device, tex.image, nullptr); tex.image = VK_NULL_HANDLE;
                vkFreeMemory(device, tex.imageMemory, nullptr); tex.imageMemory = VK_NULL_HANDLE;
            }
        }
        textures_.clear();
        destroyPresentSemaphores();
        for (int i = 0; i < kMaxFrames; i++) {
            if (imageAvailableSemaphores_[i]) { vkDestroySemaphore(device, imageAvailableSemaphores_[i], nullptr); imageAvailableSemaphores_[i] = VK_NULL_HANDLE; }
            if (renderTimelineSemaphores_[i]) { vkDestroySemaphore(device, renderTimelineSemaphores_[i], nullptr); renderTimelineSemaphores_[i] = VK_NULL_HANDLE; }
            if (inFlightFences_[i]) { vkDestroyFence(device, inFlightFences_[i], nullptr); inFlightFences_[i] = VK_NULL_HANDLE; }
            if (uniformBuffersMapped_[i]) { vkUnmapMemory(device, uniformBuffersMemory_[i]); uniformBuffersMapped_[i] = nullptr; }
            if (uniformBuffers_[i]) { vkDestroyBuffer(device, uniformBuffers_[i], nullptr); uniformBuffers_[i] = VK_NULL_HANDLE;
                vkFreeMemory(device, uniformBuffersMemory_[i], nullptr); uniformBuffersMemory_[i] = VK_NULL_HANDLE; }
        }
        if (descriptorPool_) { vkDestroyDescriptorPool(device, descriptorPool_, nullptr); descriptorPool_ = VK_NULL_HANDLE; }
        if (descriptorSetLayout_) { vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr); descriptorSetLayout_ = VK_NULL_HANDLE; }
        if (boneDescriptorSetLayout_) { vkDestroyDescriptorSetLayout(device, boneDescriptorSetLayout_, nullptr); boneDescriptorSetLayout_ = VK_NULL_HANDLE; }
        if (pipelineLayout_) { vkDestroyPipelineLayout(device, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
        
        if (defaultSampler_) { vkDestroySampler(device, defaultSampler_, nullptr); defaultSampler_ = VK_NULL_HANDLE; }
        if (defaultImageView_) { vkDestroyImageView(device, defaultImageView_, nullptr); defaultImageView_ = VK_NULL_HANDLE; }
        if (defaultImage_) { vkDestroyImage(device, defaultImage_, nullptr); defaultImage_ = VK_NULL_HANDLE; vkFreeMemory(device, defaultImageMemory_, nullptr); defaultImageMemory_ = VK_NULL_HANDLE; }
        if (defaultNormalSampler_) { vkDestroySampler(device, defaultNormalSampler_, nullptr); defaultNormalSampler_ = VK_NULL_HANDLE; }
        if (defaultNormalImageView_) { vkDestroyImageView(device, defaultNormalImageView_, nullptr); defaultNormalImageView_ = VK_NULL_HANDLE; }
        if (defaultNormalImage_) { vkDestroyImage(device, defaultNormalImage_, nullptr); defaultNormalImage_ = VK_NULL_HANDLE; vkFreeMemory(device, defaultNormalImageMemory_, nullptr); defaultNormalImageMemory_ = VK_NULL_HANDLE; }
        if (defaultOrmSampler_) { vkDestroySampler(device, defaultOrmSampler_, nullptr); defaultOrmSampler_ = VK_NULL_HANDLE; }
        if (defaultOrmImageView_) { vkDestroyImageView(device, defaultOrmImageView_, nullptr); defaultOrmImageView_ = VK_NULL_HANDLE; }
        if (defaultOrmImage_) { vkDestroyImage(device, defaultOrmImage_, nullptr); defaultOrmImage_ = VK_NULL_HANDLE; vkFreeMemory(device, defaultOrmImageMemory_, nullptr); defaultOrmImageMemory_ = VK_NULL_HANDLE; }
        if (defaultBlackSampler_) { vkDestroySampler(device, defaultBlackSampler_, nullptr); defaultBlackSampler_ = VK_NULL_HANDLE; }
        if (defaultBlackImageView_) { vkDestroyImageView(device, defaultBlackImageView_, nullptr); defaultBlackImageView_ = VK_NULL_HANDLE; }
        if (defaultBlackImage_) { vkDestroyImage(device, defaultBlackImage_, nullptr); defaultBlackImage_ = VK_NULL_HANDLE; vkFreeMemory(device, defaultBlackImageMemory_, nullptr); defaultBlackImageMemory_ = VK_NULL_HANDLE; }

        if (shadowPipeline_) { vkDestroyPipeline(device, shadowPipeline_, nullptr); shadowPipeline_ = VK_NULL_HANDLE; }
        if (shadowSunPipeline_) { vkDestroyPipeline(device, shadowSunPipeline_, nullptr); shadowSunPipeline_ = VK_NULL_HANDLE; }
        if (terrainShadowPipeline_) { vkDestroyPipeline(device, terrainShadowPipeline_, nullptr); terrainShadowPipeline_ = VK_NULL_HANDLE; }
        if (terrainShadowSunPipeline_) { vkDestroyPipeline(device, terrainShadowSunPipeline_, nullptr); terrainShadowSunPipeline_ = VK_NULL_HANDLE; }
        if (terrainShadowPipelineLayout_) { vkDestroyPipelineLayout(device, terrainShadowPipelineLayout_, nullptr); terrainShadowPipelineLayout_ = VK_NULL_HANDLE; }
        if (shadowSkinnedSunPipeline_) { vkDestroyPipeline(device, shadowSkinnedSunPipeline_, nullptr); shadowSkinnedSunPipeline_ = VK_NULL_HANDLE; }
        if (shadowPipelineLayout_) { vkDestroyPipelineLayout(device, shadowPipelineLayout_, nullptr); shadowPipelineLayout_ = VK_NULL_HANDLE; }
        if (shadowSampler_) { vkDestroySampler(device, shadowSampler_, nullptr); shadowSampler_ = VK_NULL_HANDLE; }
        if (shadowMapView_) { vkDestroyImageView(device, shadowMapView_, nullptr); shadowMapView_ = VK_NULL_HANDLE; }
        if (shadowMapImage_) { vkDestroyImage(device, shadowMapImage_, nullptr); shadowMapImage_ = VK_NULL_HANDLE; vkFreeMemory(device, shadowMapMemory_, nullptr); shadowMapMemory_ = VK_NULL_HANDLE; }

        cleanedUp_ = true;
    }

    uint32_t width() const { return platform_->width(); }
    uint32_t height() const { return platform_->height(); }
    std::array<float, 4> clearColor_ = {{0.1f, 0.1f, 0.1f, 1.0f}};

    void addFeature(std::shared_ptr<IRenderFeature> feature, Renderer* renderer) {
        feature->init(renderer);
        features_.push_back(feature);
    }

    void setSynchronizationMode(SynchronizationMode mode) {
        if (synchronizationMode_ == mode) return;
        // Klatki w locie zostały zgłoszone ze starym mechanizmem (fence albo timeline);
        // nowy tryb by na nie nie czekał i nadpisałby bufory komend używane przez GPU.
        waitIdle();
        synchronizationMode_ = mode;
    }

    SynchronizationMode synchronizationMode() const { return synchronizationMode_; }

    void setPresentMode(PresentMode mode) {
        if (presentMode_ == mode) return;
        presentMode_ = mode;
        if (swapchain_ && vulkanContext_) {
            VkDevice device = vulkanContext_->device();
            vkDeviceWaitIdle(device);
            swapchain_->setPresentMode(mode == PresentMode::Immediate ? VK_PRESENT_MODE_IMMEDIATE_KHR :
                                       mode == PresentMode::Mailbox ? VK_PRESENT_MODE_MAILBOX_KHR :
                                       VK_PRESENT_MODE_FIFO_KHR);
            recreateSwapChain();
        }
    }

    PresentMode presentMode() const { return presentMode_; }

    void setShadowsEnabled(bool enabled) { shadowsEnabled_ = enabled; }
    bool shadowsEnabled() const { return shadowsEnabled_; }
    void setWireframeEnabled(bool enabled) {
        if (wireframeEnabled_ != enabled) {
            wireframeEnabled_ = enabled;
            waitIdle();
            VkDevice device = vulkanContext_->device();
            if (gbufferPipeline_) {
                vkDestroyPipeline(device, gbufferPipeline_, nullptr);
                gbufferPipeline_ = VK_NULL_HANDLE;
            }
            if (gbufferSkinnedPipeline_) {
                vkDestroyPipeline(device, gbufferSkinnedPipeline_, nullptr);
                gbufferSkinnedPipeline_ = VK_NULL_HANDLE;
            }
            // create*Pipeline() tworzą też nowe layouty — stare trzeba zniszczyć, inaczej wyciekają.
            if (gbufferPipelineLayout_) {
                vkDestroyPipelineLayout(device, gbufferPipelineLayout_, nullptr);
                gbufferPipelineLayout_ = VK_NULL_HANDLE;
            }
            if (gbufferSkinnedPipelineLayout_) {
                vkDestroyPipelineLayout(device, gbufferSkinnedPipelineLayout_, nullptr);
                gbufferSkinnedPipelineLayout_ = VK_NULL_HANDLE;
            }
            createGBufferPipeline();
            createGBufferSkinnedPipeline();
        }
    }
    bool isWireframeEnabled() const { return wireframeEnabled_; }

    void setLightingEnabled(bool enabled) { lightingEnabled_ = enabled; }
    bool isLightingEnabled() const { return lightingEnabled_; }

    void setFogEnabled(bool enabled) { fogEnabled_ = enabled; }
    bool isFogEnabled() const { return fogEnabled_; }
    void setFogColor(float r, float g, float b) { fogColor_[0] = r; fogColor_[1] = g; fogColor_[2] = b; }
    void setFogStart(float start) { fogStart_ = start; }
    void setFogEnd(float end) { fogEnd_ = end; }
    void setFogDensity(float density) { fogDensity_ = density; }

    void setWindTime(float time) { windTime_ = time; }
    float getWindTime() const { return windTime_; }
    void setWindStrength(float strength) { windStrength_ = strength; }
    float getWindStrength() const { return windStrength_; }

    void setAmbientStrength(float s) { ambientStrength_ = s; }

    void setDirectionalShadowConfig(float halfSize, float startDistance, float splitLambda) {
        dirShadowHalfSize_ = std::max(1.0f, halfSize);
        dirShadowStart_ = std::max(0.0f, startDistance);
        dirShadowLambda_ = std::clamp(splitLambda, 0.0f, 1.0f);
    }

    /**
     * Podział kaskad (Practical Split Scheme): mieszanka podziału logarytmicznego (gęstsze kaskady
     * blisko kamery) i równomiernego, sterowana lambda. Kaskady obejmują głębokości widoku
     * [start, koniec], gdzie start = max(near kamery, dirShadowStart_) - kamera RTS widzi teren
     * dopiero z pewnej odległości, więc nie marnujemy rozdzielczości na pustą przestrzeń.
     * Wynik: dirShadowSplitDepths_[i] = dalsza granica kaskady i (głębokość w przestrzeni widoku).
     */
    void computeCascadeSplits(float nearDepth, float startDepth, float farDepth) {
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
    void updateVisibleDepthRange(const RenderScene& scene, const RenderView& v) {
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

    void setAmbientColor(float r, float g, float b) {
        ambientColor_[0] = r; ambientColor_[1] = g; ambientColor_[2] = b;
    }

    void setLights(const std::vector<SceneLight>& lights) {
        // Renderer otrzymuje gotowy batch swiatel z ECS i konwertuje go
        // do formatu UBO wykorzystywanego przez deferred/PBR pass.
        storedLightCount_ = 0;
        memset(storedLights_, 0, sizeof(storedLights_));

        for (const auto& src : lights) {
            if (storedLightCount_ >= kMaxLights) {
                break;
            }

            auto& dst = storedLights_[storedLightCount_];
            dst.position[3] = -1.0f;   // warstwę mapy cieni przydziela assignShadowSlots (co klatkę)
            dst.params[3] = 1.0f;
            dst.position[0] = src.position.x;
            dst.position[1] = src.position.y;
            dst.position[2] = src.position.z;

            Vec3 dir = normalize(src.direction);
            dst.direction[0] = dir.x;
            dst.direction[1] = dir.y;
            dst.direction[2] = dir.z;

            dst.color[0] = src.color.x;
            dst.color[1] = src.color.y;
            dst.color[2] = src.color.z;
            dst.color[3] = src.intensity;

            switch (src.type) {
            case SceneLightType::Directional:
                dst.direction[3] = 1.0f;
                dst.params[3] = static_cast<float>(kShadowCascades);
                break;
            case SceneLightType::Point:
                dst.direction[3] = 2.0f;
                dst.params[2] = src.range;
                break;
            case SceneLightType::Spot:
                dst.direction[3] = 3.0f;
                dst.params[0] = std::cos(src.innerAngleDeg * PI / 180.0f);
                dst.params[1] = std::cos(src.outerAngleDeg * PI / 180.0f);
                dst.params[2] = src.range;
                break;
            }

            storedLightCount_++;
        }
    }

    uint32_t getLightCount() const { return storedLightCount_; }

    /**
     * Wybiera światła rzucające cień w tej klatce i przydziela im miejsce w tablicy map cieni:
     *  - pierwsze światło kierunkowe (słońce) -> kaskady w warstwach 0..kShadowCascades-1,
     *  - światła lokalne (reflektor, punktowe): do kLocalShadowSlots najbliższych kamerze, których zasięg
     *    sięga w obszar cieni (odległość - zasięg < zasięg cieni) -> ćwiartki atlasu w warstwie kLocalShadowLayer.
     * Pozostałe światła oświetlają scenę normalnie, ale bez cienia (position.w = -1).
     */
    void assignShadowSlots(const Vec3& cameraPos) {
        shadowCasters_.clear();
        struct Candidate { uint32_t index; float score; };
        std::vector<Candidate> local;
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
            local.push_back({i, distanceToRange});
        }
        std::sort(local.begin(), local.end(), [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
        for (uint32_t slot = 0; slot < local.size() && slot < kLocalShadowSlots; ++slot) {
            LightData& l = storedLights_[local[slot].index];
            l.position[3] = static_cast<float>(kLocalShadowLayer);
            l.params[3] = static_cast<float>(slot);
            shadowCasters_.push_back({local[slot].index, kLocalShadowLayer, 1u, static_cast<int>(slot)});
        }
    }
    IRenderFeature* getFeature(const std::string& name) {
        for (auto& f : features_) {
            if (f->getName() == name) return f.get();
        }
        return nullptr;
    }
    void removeFeature(const std::string& name) {
        auto it = std::remove_if(features_.begin(), features_.end(),
            [&name](const std::shared_ptr<IRenderFeature>& f) {
                return f->getName() == name;
            });
        features_.erase(it, features_.end());
    }

private:
    void createUniformBuffers() {
        VkDevice device = vulkanContext_->device();
        VkPhysicalDevice physicalDevice = vulkanContext_->physicalDevice();
        VkDeviceSize bufferSize = sizeof(GlobalUBO);
        uniformBuffers_.resize(kMaxFrames);
        uniformBuffersMemory_.resize(kMaxFrames);
        uniformBuffersMapped_.resize(kMaxFrames);
        for (int i = 0; i < kMaxFrames; i++) {
            vk::createBuffer(device, physicalDevice, bufferSize,
                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             uniformBuffers_[i], uniformBuffersMemory_[i]);
            vkMapMemory(device, uniformBuffersMemory_[i], 0, bufferSize, 0, &uniformBuffersMapped_[i]);
        }
    }

    std::array<Vec3, 8> buildCameraFrustumCorners(const RenderView& view) const {
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

    Matrix4 buildDirectionalCascadeMatrix(const std::array<Vec3, 8>& frustumCorners,
                                          const Vec3& lightDirection,
                                          float splitNear,
                                          float splitFar) const {
        std::array<Vec3, 8> cascadeCorners{};
        for (int i = 0; i < 4; ++i) {
            const Vec3 nearCorner = frustumCorners[i];
            const Vec3 farCorner = frustumCorners[i + 4];
            cascadeCorners[i] = lerp(nearCorner, farCorner, splitNear);
            cascadeCorners[i + 4] = lerp(nearCorner, farCorner, splitFar);
        }

        Vec3 center{0.0f, 0.0f, 0.0f};
        for (const Vec3& corner : cascadeCorners) {
            center = add(center, corner);
        }
        center = multiply(center, 1.0f / static_cast<float>(cascadeCorners.size()));

        float radius = 0.0f;
        for (const Vec3& corner : cascadeCorners) {
            const Vec3 delta = subtract(corner, center);
            radius = std::max(radius, std::sqrt(dot(delta, delta)));
        }
        radius = std::max(radius, 8.0f);
        radius = std::ceil(radius * 16.0f) / 16.0f;

        Vec3 up = std::abs(lightDirection.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        const Vec3 lightForward = normalize(multiply(lightDirection, -1.0f));
        const Vec3 lightRight = normalize(cross(lightForward, up));
        const Vec3 lightUp = cross(lightRight, lightForward);
        const float texelSize = (radius * 2.0f) / static_cast<float>(kShadowMapSize);
        const float snappedX = std::round(dot(center, lightRight) / texelSize) * texelSize;
        const float snappedY = std::round(dot(center, lightUp) / texelSize) * texelSize;
        center = add(center,
                     add(multiply(lightRight, snappedX - dot(center, lightRight)),
                         multiply(lightUp, snappedY - dot(center, lightUp))));

        const Vec3 lightPosition = add(center, multiply(lightDirection, radius * 2.0f));
        const Matrix4 lightView = lookAt(lightPosition, center, up);

        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();

        for (const Vec3& corner : cascadeCorners) {
            const Vec3 lightSpace = transformPoint(lightView, corner);
            minZ = std::min(minZ, lightSpace.z);
            maxZ = std::max(maxZ, lightSpace.z);
        }

        // Stały, symetryczny obszar XY zachowuje stabilizację do siatki tekseli.
        // Ciasne min/max zmieniało skalę projekcji przy każdym obrocie kamery,
        // przez co krawędzie cieni migotały mimo zaokrąglania środka.
        const float zPadding = std::max(80.0f, radius);
        const float nearPlane = std::max(0.5f, -maxZ - zPadding);
        const float farPlane = std::max(nearPlane + 1.0f, -minZ + zPadding);
        return multiply(ortho(-radius, radius, -radius, radius, nearPlane, farPlane), lightView);
    }

    void updateUniformBuffer(const RenderView& view) {
        GlobalUBO ubo{};
        ubo.view = view.view;
        ubo.proj = view.projection;
        ubo.invViewProj = inverse(multiply(ubo.proj, ubo.view));
        std::memcpy(ubo.camRight, &view.cameraRight, sizeof(Vec3));
        std::memcpy(ubo.camUp, &view.cameraUp, sizeof(Vec3));
        std::memcpy(ubo.cameraPos, &view.cameraPosition, sizeof(Vec3));
        
        ubo.lightingEnabled = lightingEnabled_ ? 1u : 0u;
        ubo.fowEnabled = fowEnabled_ ? 1u : 0u;
        ubo.fowOriginX = fowOriginX_;
        ubo.fowOriginZ = fowOriginZ_;
        ubo.fowSize = fowSize_;
        ubo.fowExplored = fowExplored_;
        ubo.fowTextureIndex = fowBindless_[currentFrame_];
        ubo.shadowsEnabled = shadowsEnabled_ ? 1u : 0u;
        ubo.ambientStrength = ambientStrength_;
        ubo.lightCount = std::min(storedLightCount_, (uint32_t)kMaxLights);
        assignShadowSlots(view.cameraPosition);
        ubo.directionalShadowCascadeCount = kShadowCascades;
        {
            // Głębokości bliskiej i dalekiej płaszczyzny kamery (do podziału kaskad).
            const auto corners = buildCameraFrustumCorners(view);
            const float nearDepth = std::abs(transformPoint(view.view, corners[0]).z);
            const float cameraFar = std::abs(transformPoint(view.view, corners[4]).z);
            float start = dirShadowStart_;
            float far = std::min(cameraFar, dirShadowHalfSize_);
            if (dirShadowStart_ <= 0.0f && visDepthValid_) {
                // Gra nie ustawiła początku kaskad - dopasowanie automatyczne: kaskady od najbliższej widocznej
                // geometrii, a zasięg (halfSize) liczony od niej, a nie od kamery.
                start = std::max(nearDepth, visMinDepth_);
                far = std::min({cameraFar, visMaxDepth_, start + dirShadowHalfSize_});
                far = std::max(far, start + 1.0f);
            }
            shadowFarDepth_ = far;
            computeCascadeSplits(nearDepth, start, far);
        }
        for (uint32_t i = 0; i < kShadowCascades; ++i) {
            ubo.directionalShadowSplits[i] = dirShadowSplitDepths_[i];
        }
        std::memcpy(ubo.ambientColor, ambientColor_, sizeof(ambientColor_));
        ubo.ambientColor[3] = 1.0f;

        ubo.fogColor[0] = fogColor_[0];
        ubo.fogColor[1] = fogColor_[1];
        ubo.fogColor[2] = fogColor_[2];
        ubo.fogColor[3] = 1.0f;
        ubo.fogStart = fogStart_;
        ubo.fogEnd = fogEnd_;
        ubo.fogDensity = fogDensity_;
        ubo.fogEnabled = fogEnabled_ ? 1u : 0u;
        ubo.windTime = windTime_;
        ubo.windStrength = windStrength_;

        for (uint32_t i = 0; i < ubo.lightCount; i++) {
            LightData& l = storedLights_[i];
            uint32_t type = (uint32_t)l.direction[3];
            Vec3 lPos = {l.position[0], l.position[1], l.position[2]};
            Vec3 lDir = {l.direction[0], l.direction[1], l.direction[2]};
            
            if (type == 1) { // Directional
                auto frustumCorners = buildCameraFrustumCorners(view);
                const float nearDepth = std::abs(transformPoint(view.view, frustumCorners[0]).z);
                for (int corner = 4; corner < 8; ++corner) {
                    const Vec3 ray = subtract(frustumCorners[corner], view.cameraPosition);
                    const Vec3 farViewPosition = transformPoint(view.view, frustumCorners[corner]);
                    const float farViewDepth = std::abs(farViewPosition.z);
                    if (farViewDepth > shadowFarDepth_) {
                        // Kaskady i shader wybierają zakres według głębokości widoku,
                        // więc również narożniki frustum muszą być przycinane tą miarą.
                        frustumCorners[corner] = add(
                            view.cameraPosition,
                            multiply(ray, shadowFarDepth_ / farViewDepth));
                    }
                }
                // Narożniki dalekiej płaszczyzny leżą teraz na głębokości farUsed; punkt na promieniu
                // z parametrem t ma głębokość nearDepth + t * (farUsed - nearDepth).
                const float farUsed = std::abs(transformPoint(view.view, frustumCorners[4]).z);
                auto depthToT = [&](float depth) {
                    return std::clamp((depth - nearDepth) / std::max(farUsed - nearDepth, 0.001f), 0.0f, 1.0f);
                };
                // Kierunek mapy cieni = bieżący kierunek światła (płynny ruch cieni razem ze słońcem).
                const Vec3 shadowDir = normalize(lDir);
                float previousSplit = depthToT(dirShadowSplitStart_);
                for (uint32_t cascade = 0; cascade < kShadowCascades; ++cascade) {
                    const float splitFar = depthToT(dirShadowSplitDepths_[cascade]);
                    l.lightViewProj[cascade] = buildDirectionalCascadeMatrix(frustumCorners, shadowDir, previousSplit, splitFar);
                    previousSplit = splitFar;
                }
            } else if (type == 3) { // Spot
                Vec3 target = add(lPos, lDir);
                Vec3 up = std::abs(lDir.y) > 0.99f ? Vec3{1,0,0} : Vec3{0,1,0};
                Matrix4 lView = lookAt(lPos, target, up);
                float outerAngle = std::acos(l.params[1]) * 2.0f;
                Matrix4 lProj = perspective(outerAngle, 1.0f, 0.1f, l.params[2]);
                l.lightViewProj[0] = multiply(lProj, lView);
            } else if (type == 2) { // Point
                // Point lights look straight down at the scene center for better floor coverage
                Vec3 target = {lPos.x, 0.0f, lPos.z};
                Matrix4 lView = lookAt(lPos, target, Vec3{0, 0, 1});
                Matrix4 lProj = perspective(radians(120.0f), 1.0f, 0.1f, l.params[2]);
                l.lightViewProj[0] = multiply(lProj, lView);
            } else {
                l.lightViewProj[0] = Matrix4::identity();
            }
        }

        std::memcpy(ubo.lights, storedLights_, sizeof(storedLights_));
        std::memcpy(uniformBuffersMapped_[currentFrame_], &ubo, sizeof(ubo));
    }

    void createDefaultTexture() {
        VkDevice device = vulkanContext_->device();
        VkPhysicalDevice physicalDevice = vulkanContext_->physicalDevice();
        uint32_t whitePixel = 0xFFFFFFFF;
        VkDeviceSize imageSize = 4;
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        vk::createBuffer(device, physicalDevice, imageSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingBufferMemory);
        void* mappedMemory;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &mappedMemory);
        std::memcpy(mappedMemory, &whitePixel, imageSize);
        vkUnmapMemory(device, stagingBufferMemory);

        vk::createImage(device, physicalDevice, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        defaultImage_, defaultImageMemory_);

        vk::transitionImageLayout(device, vulkanContext_->commandPool(),
                                  vulkanContext_->graphicsQueue(),
                                  defaultImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);

        VkCommandBuffer cmdBuffer = beginSingleTimeCommands();
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {1,1,1};
        vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, defaultImage_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endSingleTimeCommands(cmdBuffer);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                  defaultImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        defaultImageView_ = vk::createImageView(device, defaultImage_, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_ASPECT_COLOR_BIT, 1);
        defaultSampler_ = vk::createSampler(device, vulkanContext_->physicalDevice(), 1, textureFilter_, AddressMode::Repeat);
    }

    void createDefaultNormalTexture() {
        VkDevice device = vulkanContext_->device();
        VkPhysicalDevice physicalDevice = vulkanContext_->physicalDevice();
        unsigned char flatNormal[4] = {128, 128, 255, 255};
        VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
        vk::createBuffer(device, physicalDevice, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingBufferMemory);
        void* m; vkMapMemory(device, stagingBufferMemory, 0, 4, 0, &m);
        std::memcpy(m, flatNormal, 4); vkUnmapMemory(device, stagingBufferMemory);
        vk::createImage(device, physicalDevice, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, defaultNormalImage_, defaultNormalImageMemory_);
        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                  defaultNormalImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        VkCommandBuffer cmd = beginSingleTimeCommands();
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {1, 1, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, defaultNormalImage_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endSingleTimeCommands(cmd);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                  defaultNormalImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        defaultNormalImageView_ = vk::createImageView(device, defaultNormalImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                                       VK_IMAGE_ASPECT_COLOR_BIT, 1);
        defaultNormalSampler_ = vk::createClampSampler(device);
    }

    void createDefaultOrmTexture() {
        VkDevice device = vulkanContext_->device();
        VkPhysicalDevice physicalDevice = vulkanContext_->physicalDevice();
        // R=occlusion(1.0), G=roughness(1.0), B=metallic(0.0), A=1.0
        unsigned char ormPixel[4] = {255, 255, 0, 255};
        VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
        vk::createBuffer(device, physicalDevice, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingBufferMemory);
        void* m; vkMapMemory(device, stagingBufferMemory, 0, 4, 0, &m);
        std::memcpy(m, ormPixel, 4); vkUnmapMemory(device, stagingBufferMemory);
        vk::createImage(device, physicalDevice, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, defaultOrmImage_, defaultOrmImageMemory_);
        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                  defaultOrmImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        VkCommandBuffer cmd = beginSingleTimeCommands();
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {1, 1, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, defaultOrmImage_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endSingleTimeCommands(cmd);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                  defaultOrmImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        defaultOrmImageView_ = vk::createImageView(device, defaultOrmImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                                    VK_IMAGE_ASPECT_COLOR_BIT, 1);
        defaultOrmSampler_ = vk::createClampSampler(device);
    }

    void createDefaultBlackTexture() {
        VkDevice device = vulkanContext_->device();
        VkPhysicalDevice physicalDevice = vulkanContext_->physicalDevice();
        unsigned char blackPixel[4] = {0, 0, 0, 0};
        VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
        vk::createBuffer(device, physicalDevice, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingBufferMemory);
        void* m; vkMapMemory(device, stagingBufferMemory, 0, 4, 0, &m);
        std::memcpy(m, blackPixel, 4); vkUnmapMemory(device, stagingBufferMemory);
        vk::createImage(device, physicalDevice, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, defaultBlackImage_, defaultBlackImageMemory_);
        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                  defaultBlackImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        VkCommandBuffer cmd = beginSingleTimeCommands();
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {1, 1, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, defaultBlackImage_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endSingleTimeCommands(cmd);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                  defaultBlackImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        defaultBlackImageView_ = vk::createImageView(device, defaultBlackImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                                      VK_IMAGE_ASPECT_COLOR_BIT, 1);
        defaultBlackSampler_ = vk::createClampSampler(device);
    }

    void createDescriptorResources() {
        VkDevice device = vulkanContext_->device();
        
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFrames},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(kMaxBindlessTextures * kMaxFrames + kMaxFrames)}
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = kMaxFrames;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }

        VkDescriptorSetLayoutBinding bindings[3] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                  VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = kMaxBindlessTextures;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // UWAGA: MaterialCompiler tworzy własną kopię tego layoutu — flagi muszą być identyczne,
        // inaczej set 0 nie jest zgodny z potokami materiałów.
        // UPDATE_UNUSED_WHILE_PENDING: nowe tekstury są dopisywane do setów, których używają klatki
        // w locie — wolno to robić tylko dla elementów tablicy, których te klatki nie czytają.
        VkDescriptorBindingFlags bindingFlags[3] = {0,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
            0};
        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flagsInfo.bindingCount = 3;
        flagsInfo.pBindingFlags = bindingFlags;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = &flagsInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout");
        }

        // Create bone descriptor set layout for compatibility with MaterialSystem
        VkDescriptorSetLayoutBinding boneBinding{};
        boneBinding.binding = 0;
        boneBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        boneBinding.descriptorCount = 1;
        boneBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo boneLayoutInfo{};
        boneLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        boneLayoutInfo.bindingCount = 1;
        boneLayoutInfo.pBindings = &boneBinding;
        if (vkCreateDescriptorSetLayout(device, &boneLayoutInfo, nullptr, &boneDescriptorSetLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create bone descriptor set layout");
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = 128; // Standard size for push constants

        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, boneDescriptorSetLayout_};
        plInfo.setLayoutCount = 2;
        plInfo.pSetLayouts = setLayouts;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout");
        }

        descriptorSets_.resize(kMaxFrames);
        std::vector<VkDescriptorSetLayout> layouts(kMaxFrames, descriptorSetLayout_);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = kMaxFrames;
        allocInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor sets");
        }

        for (int i = 0; i < kMaxFrames; i++) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers_[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(GlobalUBO);

            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = descriptorSets_[i];
            w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.descriptorCount = 1;
            w.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }
    }

    void recreateSwapChain() {
        VkDevice device = vulkanContext_->device();
        vkDeviceWaitIdle(device);
        destroySceneColorResources();
        cleanupGBuffer();
        swapchain_->recreate(platform_->width(), platform_->height());
        destroyPresentSemaphores();
        createPresentSemaphores();
        createGBufferResources();
        createSceneColorResources();
    }

    VkCommandBuffer beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = vulkanContext_->commandPool();
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(vulkanContext_->device(), &allocInfo, &cmd);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);
        return cmd;
    }

    void endSingleTimeCommands(VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(vulkanContext_->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(vulkanContext_->graphicsQueue());
        vkFreeCommandBuffers(vulkanContext_->device(), vulkanContext_->commandPool(), 1, &cmd);
    }

    void createShadowResources() {
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

    void transitionShadowMapLayout(VkImageLayout newLayout) {
        VkDevice device = vulkanContext_->device();
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
    void createTerrainShadowPipeline() {
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
        pushRange.size = sizeof(TerrainShadowPush);
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

    void createShadowPipeline() {
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

        constexpr uint32_t kShadowPushSize = sizeof(Matrix4) * 2 + sizeof(float) + sizeof(uint32_t) + sizeof(float) * 2;
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

    void createShadowSkinnedPipeline() {
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

        constexpr uint32_t kShadowPushSize = sizeof(Matrix4) * 2 + sizeof(float) + sizeof(uint32_t) + sizeof(float) * 2;
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

    void createGBufferResources() {
        VkDevice device = vulkanContext_->device();
        VkPhysicalDevice pd = vulkanContext_->physicalDevice();
        VkExtent2D ext = swapchain_->extent();

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;
        vkCreateSampler(device, &samplerInfo, nullptr, &gbufferSampler_);

        for (int f = 0; f < kMaxFrames; f++) {
            for (int a = 0; a < kGBufferAttachments; a++) {
                VkImageCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                ci.imageType = VK_IMAGE_TYPE_2D;
                ci.extent = {ext.width, ext.height, 1};
                ci.mipLevels = 1; ci.arrayLayers = 1;
                ci.format = gbufferAttachmentFormat(a);
                ci.tiling = VK_IMAGE_TILING_OPTIMAL;
                ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                ci.samples = VK_SAMPLE_COUNT_1_BIT;
                ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                vkCreateImage(device, &ci, nullptr, &gbufferImages_[a][f]);

                VkMemoryRequirements mr;
                vkGetImageMemoryRequirements(device, gbufferImages_[a][f], &mr);
                VkMemoryAllocateInfo ai{};
                ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                ai.allocationSize = mr.size;
                ai.memoryTypeIndex = vk::findMemoryType(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                vkAllocateMemory(device, &ai, nullptr, &gbufferMemories_[a][f]);
                vkBindImageMemory(device, gbufferImages_[a][f], gbufferMemories_[a][f], 0);

                VkImageViewCreateInfo vi{};
                vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                vi.image = gbufferImages_[a][f];
                vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
                vi.format = gbufferAttachmentFormat(a);
                vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                vi.subresourceRange.levelCount = 1;
                vi.subresourceRange.layerCount = 1;
                vkCreateImageView(device, &vi, nullptr, &gbufferViews_[a][f]);

                vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                          gbufferImages_[a][f], gbufferAttachmentFormat(a),
                                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
            }
        }

        createGBufferPipeline();
        createGBufferSkinnedPipeline();
        createDeferredPipeline();
    }

    void createGBufferPipeline() {
        VkDevice device = vulkanContext_->device();

        std::string shaderDir = "bin/assets/shaders/gbuffer/";
        auto vertCode = vk::readFile(shaderDir + "gbuffer.vert.spv");
        auto fragCode = vk::readFile(shaderDir + "gbuffer.frag.spv");
        VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
        VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "vertexMain";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "fragmentMain";

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
        rasterizer.polygonMode = wireframeEnabled_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blendAttachments[4] = {};
        for (int i = 0; i < 4; i++) {
            blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 4;
        colorBlending.pAttachments = blendAttachments;

        std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = 256;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, boneDescriptorSetLayout_};
        layoutInfo.setLayoutCount = 2;
        layoutInfo.pSetLayouts = setLayouts;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &gbufferPipelineLayout_);

        VkFormat depthFmt = swapchain_->depthFormat();
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 4;
        VkFormat formats[4] = {gbufferAttachmentFormat(0), gbufferAttachmentFormat(1),
                               gbufferAttachmentFormat(2), gbufferAttachmentFormat(3)};
        renderingInfo.pColorAttachmentFormats = formats;
        renderingInfo.depthAttachmentFormat = depthFmt;

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
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = gbufferPipelineLayout_;
        pipelineInfo.pNext = &renderingInfo;

        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gbufferPipeline_);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
    }

    void createGBufferSkinnedPipeline() {
        VkDevice device = vulkanContext_->device();
        std::string shaderDir = "bin/assets/shaders/gbuffer_skinned/";
        auto vertCode = vk::readFile(shaderDir + "gbuffer_skinned.vert.spv");
        auto fragCode = vk::readFile(shaderDir + "gbuffer_skinned.frag.spv");
        VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
        VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "vertexMain";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "fragmentMain";

        auto bindingDesc = Vertex::bindingDescription();
        auto attrDescs = Vertex::attributeDescriptions();

        VkVertexInputBindingDescription binding2{};
        binding2.binding = 1;
        binding2.stride = sizeof(uint32_t) * 4 + sizeof(float) * 4;
        binding2.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription skinAttrs[2]{};
        skinAttrs[0].binding = 1;
        skinAttrs[0].location = 4;
        skinAttrs[0].format = VK_FORMAT_R32G32B32A32_UINT;
        skinAttrs[0].offset = 0;
        skinAttrs[1].binding = 1;
        skinAttrs[1].location = 5;
        skinAttrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        skinAttrs[1].offset = sizeof(uint32_t) * 4;

        std::vector<VkVertexInputBindingDescription> bindings = {bindingDesc, binding2};
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
        rasterizer.polygonMode = wireframeEnabled_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 1.0f;
        rasterizer.depthBiasSlopeFactor = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blendAttachments[4] = {};
        for (int i = 0; i < 4; i++)
            blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 4;
        colorBlending.pAttachments = blendAttachments;

        std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = 256;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, boneDescriptorSetLayout_};
        layoutInfo.setLayoutCount = 2;
        layoutInfo.pSetLayouts = setLayouts;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &gbufferSkinnedPipelineLayout_);

        VkFormat depthFmt = swapchain_->depthFormat();
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 4;
        VkFormat formats[4] = {gbufferAttachmentFormat(0), gbufferAttachmentFormat(1),
                               gbufferAttachmentFormat(2), gbufferAttachmentFormat(3)};
        renderingInfo.pColorAttachmentFormats = formats;
        renderingInfo.depthAttachmentFormat = depthFmt;

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
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = gbufferSkinnedPipelineLayout_;
        pipelineInfo.pNext = &renderingInfo;

        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gbufferSkinnedPipeline_);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
    }

    void createSceneColorResources() {
        VkDevice device = vulkanContext_->device();
        VkPhysicalDevice pd = vulkanContext_->physicalDevice();
        VkExtent2D ext = swapchain_->extent();
        VkFormat colorFmt = swapchain_->imageFormat();

        sceneColorSampler_ = vk::createSampler(device, pd, 1, TextureFilter::Linear, AddressMode::ClampToEdge);

        for (int f = 0; f < kMaxFrames; f++) {
            VkImageCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.extent = {ext.width, ext.height, 1};
            ci.mipLevels = 1; ci.arrayLayers = 1;
            ci.format = colorFmt;
            ci.tiling = VK_IMAGE_TILING_OPTIMAL;
            ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            vkCreateImage(device, &ci, nullptr, &sceneColorImages_[f]);

            VkMemoryRequirements mr;
            vkGetImageMemoryRequirements(device, sceneColorImages_[f], &mr);
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = mr.size;
            ai.memoryTypeIndex = vk::findMemoryType(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &ai, nullptr, &sceneColorMemories_[f]);
            vkBindImageMemory(device, sceneColorImages_[f], sceneColorMemories_[f], 0);

            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = sceneColorImages_[f];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = colorFmt;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &vi, nullptr, &sceneColorViews_[f]);

            vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                      sceneColorImages_[f], colorFmt,
                                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
    }

    void registerSceneColorForFrame(uint32_t frameIndex) {
        VkDevice device = vulkanContext_->device();
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = sceneColorViews_[frameIndex];
        imageInfo.sampler = sceneColorSampler_;

        uint32_t index = 0;
        auto it = textureNameToIndex_.find("ocean_scene_color");
        if (it == textureNameToIndex_.end()) {
            index = nextTextureIndex_++;
            textureNameToIndex_["ocean_scene_color"] = index;
        } else {
            index = it->second;
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[frameIndex];
        write.dstBinding = 1;
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void destroySceneColorResources() {
        VkDevice device = vulkanContext_->device();
        for (int f = 0; f < kMaxFrames; f++) {
            if (sceneColorViews_[f]) { vkDestroyImageView(device, sceneColorViews_[f], nullptr); sceneColorViews_[f] = VK_NULL_HANDLE; }
            if (sceneColorImages_[f]) { vkDestroyImage(device, sceneColorImages_[f], nullptr); sceneColorImages_[f] = VK_NULL_HANDLE; }
            if (sceneColorMemories_[f]) { vkFreeMemory(device, sceneColorMemories_[f], nullptr); sceneColorMemories_[f] = VK_NULL_HANDLE; }
        }
        if (sceneColorSampler_) { vkDestroySampler(device, sceneColorSampler_, nullptr); sceneColorSampler_ = VK_NULL_HANDLE; }
    }

    void registerSceneDepthForFrame(uint32_t frameIndex, uint32_t imageIndex) {
        VkDevice device = vulkanContext_->device();
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo.imageView = swapchain_->depthImageView(imageIndex);
        imageInfo.sampler = sceneColorSampler_;

        uint32_t index = 0;
        auto it = textureNameToIndex_.find("ocean_scene_depth");
        if (it == textureNameToIndex_.end()) {
            index = nextTextureIndex_++;
            textureNameToIndex_["ocean_scene_depth"] = index;
        } else {
            index = it->second;
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[frameIndex];
        write.dstBinding = 1;
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void registerGBufferWorldPosForFrame(uint32_t frameIndex) {
        VkDevice device = vulkanContext_->device();
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = gbufferViews_[3][frameIndex];
        imageInfo.sampler = gbufferSampler_;

        uint32_t index = 0;
        auto it = textureNameToIndex_.find("gbuffer_world_pos");
        if (it == textureNameToIndex_.end()) {
            index = nextTextureIndex_++;
            textureNameToIndex_["gbuffer_world_pos"] = index;
        } else {
            index = it->second;
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[frameIndex];
        write.dstBinding = 1;
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void createDeferredPipeline() {
        VkDevice device = vulkanContext_->device();

        std::string shaderDir = "bin/assets/shaders/deferred/";
        auto vertCode = vk::readFile(shaderDir + "deferred.vert.spv");
        auto fragCode = vk::readFile(shaderDir + "deferred.frag.spv");
        VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
        VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "vertexMain";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "fragmentMain";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 0;
        vertexInput.vertexAttributeDescriptionCount = 0;

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
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // Set 1: GBuffer textures + Shadow Map
        VkDescriptorSetLayoutBinding gbufferBindings[6] = {};
        for (int i = 0; i < 4; i++) {
            gbufferBindings[i].binding = i;
            gbufferBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbufferBindings[i].descriptorCount = 1;
            gbufferBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        gbufferBindings[4].binding = 4;
        gbufferBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbufferBindings[4].descriptorCount = 1;
        gbufferBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        gbufferBindings[5].binding = 5;   // mgła wojny (tekstura widoczności)
        gbufferBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbufferBindings[5].descriptorCount = 1;
        gbufferBindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dslInfo{};
        dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslInfo.bindingCount = 6;
        dslInfo.pBindings = gbufferBindings;
        vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &deferredDescriptorSetLayout_);

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 * static_cast<uint32_t>(kMaxFrames)};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = kMaxFrames;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &deferredDescriptorPool_);

        for (int f = 0; f < kMaxFrames; f++) {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = deferredDescriptorPool_;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &deferredDescriptorSetLayout_;
            vkAllocateDescriptorSets(device, &allocInfo, &deferredDescriptorSets_[f]);

            VkWriteDescriptorSet writes[6] = {};
            VkDescriptorImageInfo imageInfos[6] = {};
            for (int a = 0; a < 4; a++) {
                imageInfos[a].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos[a].imageView = gbufferViews_[a][f];
                imageInfos[a].sampler = gbufferSampler_;
                writes[a].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[a].dstSet = deferredDescriptorSets_[f];
                writes[a].dstBinding = a;
                writes[a].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[a].descriptorCount = 1;
                writes[a].pImageInfo = &imageInfos[a];
            }
            imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[4].imageView = shadowMapView_;
            imageInfos[4].sampler = shadowSampler_;
            writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet = deferredDescriptorSets_[f];
            writes[4].dstBinding = 4;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[4].descriptorCount = 1;
            writes[4].pImageInfo = &imageInfos[4];
            imageInfos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[5].imageView = fowView_[f];
            imageInfos[5].sampler = fowSampler_;
            writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[5].dstSet = deferredDescriptorSets_[f];
            writes[5].dstBinding = 5;
            writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[5].descriptorCount = 1;
            writes[5].pImageInfo = &imageInfos[5];
            vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = 128;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, deferredDescriptorSetLayout_};
        layoutInfo.setLayoutCount = 2;
        layoutInfo.pSetLayouts = setLayouts;
        layoutInfo.pushConstantRangeCount = 0;
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &deferredPipelineLayout_);

        VkFormat colorFormat = swapchain_->imageFormat();
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        // Przebieg, w którym rysujemy oświetlenie, ma podpięty depth — format musi się zgadzać.
        renderingInfo.depthAttachmentFormat = swapchain_->depthFormat();

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
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = deferredPipelineLayout_;
        pipelineInfo.pNext = &renderingInfo;

        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &deferredPipeline_);
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
    }

    void createSyncObjects() {
        VkDevice device = vulkanContext_->device();

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkSemaphoreTypeCreateInfo timelineTypeInfo{};
        timelineTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timelineTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineTypeInfo.initialValue = 0;

        VkSemaphoreCreateInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        timelineInfo.pNext = &timelineTypeInfo;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < kMaxFrames; i++) {
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]);
            vkCreateSemaphore(device, &timelineInfo, nullptr, &renderTimelineSemaphores_[i]);
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences_[i]);
            timelineCounters_[i] = 0;
        }
        globalTimelineCounter_ = 0;
        createPresentSemaphores();
    }

    void createPresentSemaphores() {
        VkDevice device = vulkanContext_->device();
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        const size_t imageCount = swapchain_->images().size();
        renderFinishedSemaphores_.assign(imageCount, VK_NULL_HANDLE);
        presentReadySemaphores_.assign(imageCount, VK_NULL_HANDLE);
        for (size_t i = 0; i < imageCount; i++) {
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]);
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &presentReadySemaphores_[i]);
        }
    }

    void destroyPresentSemaphores() {
        VkDevice device = vulkanContext_->device();
        for (VkSemaphore sem : renderFinishedSemaphores_) if (sem) vkDestroySemaphore(device, sem, nullptr);
        for (VkSemaphore sem : presentReadySemaphores_) if (sem) vkDestroySemaphore(device, sem, nullptr);
        renderFinishedSemaphores_.clear();
        presentReadySemaphores_.clear();
    }

    void createCommandBuffers() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = vulkanContext_->commandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = kMaxFrames;
        commandBuffers_.resize(kMaxFrames);
        vkAllocateCommandBuffers(vulkanContext_->device(), &allocInfo, commandBuffers_.data());
    }

private:
    bool hasActiveFeature(IRenderFeature::Stage stage) const {
        for (const auto& feature : features_) {
            if (feature->isActive() && feature->getStage() == stage) return true;
        }
        return false;
    }

    void executeFeatures(VkCommandBuffer cmd, const RenderView& view, IRenderFeature::Stage stage) {
        if (stage == IRenderFeature::Stage::Overlay) {
            auto rit = features_.rbegin();
            auto rend = features_.rend();
            for (; rit != rend; ++rit) {
                if ((*rit)->isActive() && (*rit)->getStage() == stage) {
                    (*rit)->render(cmd, currentFrame_, view);
                }
            }
        } else {
            for (auto& feature : features_) {
                if (feature->isActive() && feature->getStage() == stage) {
                    feature->render(cmd, currentFrame_, view);
                }
            }
        }
    }

    Platform* platform_;
    MaterialManager* materialManager_;
    std::shared_ptr<VulkanContext> vulkanContext_;
    AssetRegistry* assetRegistry_ = nullptr;
    std::unique_ptr<VulkanSwapchain> swapchain_;

    std::vector<std::shared_ptr<IRenderFeature>> features_;
    std::vector<std::unique_ptr<IRenderPass>> passes_;
    RenderOrchestrator orchestrator_;
    const std::vector<RenderOrchestrator::Step> renderPlan_ = {
        RenderOrchestrator::Step::FeaturesBeforeOpaque,
        RenderOrchestrator::Step::DeferredLighting,
        RenderOrchestrator::Step::OpaquePass,
        RenderOrchestrator::Step::FeaturesAfterOpaque,
        RenderOrchestrator::Step::FeaturesAfterTransparent,
        RenderOrchestrator::Step::TransparentPass,
        RenderOrchestrator::Step::FeaturesOverlay,
    };


    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout boneDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;

    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VkDeviceMemory> uniformBuffersMemory_;
    std::vector<void*> uniformBuffersMapped_;

    VkImage defaultImage_ = VK_NULL_HANDLE;
    VkDeviceMemory defaultImageMemory_ = VK_NULL_HANDLE;
    VkImageView defaultImageView_ = VK_NULL_HANDLE;
    VkSampler defaultSampler_ = VK_NULL_HANDLE;

    VkImage defaultNormalImage_ = VK_NULL_HANDLE;
    VkDeviceMemory defaultNormalImageMemory_ = VK_NULL_HANDLE;
    VkImageView defaultNormalImageView_ = VK_NULL_HANDLE;
    VkSampler defaultNormalSampler_ = VK_NULL_HANDLE;

    VkImage defaultOrmImage_ = VK_NULL_HANDLE;
    VkDeviceMemory defaultOrmImageMemory_ = VK_NULL_HANDLE;
    VkImageView defaultOrmImageView_ = VK_NULL_HANDLE;
    VkSampler defaultOrmSampler_ = VK_NULL_HANDLE;

    VkImage defaultBlackImage_ = VK_NULL_HANDLE;
    VkDeviceMemory defaultBlackImageMemory_ = VK_NULL_HANDLE;
    VkImageView defaultBlackImageView_ = VK_NULL_HANDLE;
    VkSampler defaultBlackSampler_ = VK_NULL_HANDLE;

    std::unordered_map<std::string, TextureResources> textures_;
    std::unordered_map<std::string, uint32_t> textureNameToIndex_;
    std::vector<uint32_t> textureHandleToIndex_;
    uint32_t nextTextureIndex_ = 0;

    uint32_t defaultDiffuseIndex_ = 0;
    uint32_t defaultNormalIndex_ = 0;
    uint32_t defaultOrmIndex_ = 0;
    uint32_t defaultEmissiveIndex_ = 0;
    uint32_t defaultMaskIndex_ = 0;

    std::array<VkSemaphore, kMaxFrames> imageAvailableSemaphores_;
    // Semafory sygnalizowane dla prezentacji są per OBRAZ swapchaina, nie per klatkę w locie:
    // fence klatki nie gwarantuje, że silnik prezentacji przestał już używać semafora.
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkSemaphore> presentReadySemaphores_;
    std::array<VkSemaphore, kMaxFrames> renderTimelineSemaphores_;
    std::array<uint64_t, kMaxFrames> timelineCounters_{};
    uint64_t globalTimelineCounter_ = 0;
    std::array<VkFence, kMaxFrames> inFlightFences_;
    std::vector<VkCommandBuffer> commandBuffers_;
    uint32_t currentFrame_ = 0;
    SynchronizationMode synchronizationMode_ = SynchronizationMode::Standard;
    PresentMode presentMode_ = PresentMode::Fifo;
    std::array<std::array<float, 4>, 3> lightDirection_{};
    float lightAmbient_ = 0.25f;
    std::array<std::array<float, 4>, 3> lightColor_{};
    bool lightingEnabled_ = false;
    float ambientStrength_ = 0.3f;
    float ambientColor_[3] = {1.0f, 1.0f, 1.0f};
    bool shadowsEnabled_ = false;
    bool wireframeEnabled_ = false;
    bool tessellationEnabled_ = true;
    float tessellationLevel_ = 4.0f;
    TextureFilter textureFilter_ = TextureFilter::Anisotropic16x;
    float dirShadowHalfSize_ = 100.0f;
    std::array<float, kShadowCascades> dirShadowSplitDepths_ = {50.0f, 150.0f, 400.0f, 1000.0f};
    float shadowFarDepth_ = 1000.0f;                  // koniec zasięgu kaskad w bieżącej klatce
    bool visDepthValid_ = false;
    bool visDepthSmoothed_ = false;                      // zakres głębokości widocznej geometrii (updateVisibleDepthRange)
    float visMinDepth_ = 0.0f, visMaxDepth_ = 0.0f;   // ustabilizowany kierunek świateł kierunkowych dla map cieni
    float dirShadowSplitStart_ = 0.0f;
    float dirShadowStart_ = 0.0f;     // od jakiej głębokości widoku zaczynają się kaskady
    float dirShadowLambda_ = 0.6f;    // 0 = podział równomierny, 1 = logarytmiczny
    bool cleanedUp_ = false;
    bool fogEnabled_ = false;
    float fogColor_[3] = {0.8f, 0.8f, 0.8f};
    float fogStart_ = 10.0f;
    float fogEnd_ = 100.0f;
    float fogDensity_ = 0.01f;

    float windTime_ = 0.0f;
    float windStrength_ = 0.3f;

    LightData storedLights_[kMaxLights] = {};
    std::vector<ShadowCaster> shadowCasters_;   // światła rzucające cień w bieżącej klatce
    uint32_t storedLightCount_ = 0;

    static constexpr uint32_t kShadowMapSize = 2048;
    VkImage shadowMapImage_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowMapMemory_ = VK_NULL_HANDLE;
    VkImageView shadowMapView_ = VK_NULL_HANDLE;
    std::vector<VkImageView> shadowMapLayerViews_;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowSunPipeline_ = VK_NULL_HANDLE;
    // Cienie terenu teselowanego (terrain_shadow).
    struct TerrainShadowPush {
        Matrix4 model;
        Matrix4 lvp;
        uint32_t heightmapTextureSet;
        float scaleY;
        float tessellationFactor;
        float layerWorldSize;
    };
    VkPipeline terrainShadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline terrainShadowSunPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout terrainShadowPipelineLayout_ = VK_NULL_HANDLE;          // jak shadowPipeline_, z depthClamp (kaskady słońca)
    VkPipeline shadowSkinnedSunPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline shadowSkinnedPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowSkinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkImageLayout shadowMapLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // GBuffer
    static constexpr int kGBufferAttachments = 4;

    // Format bufora G-Buffera. Kolor, normalna i materiał mieszczą się w 16-bit float, ale POZYCJA ŚWIATA
    // (bufor 3) potrzebuje 32 bitów: half float ma tylko 11 bitów mantysy, więc przy współrzędnych
    // rzędu 256-512 krok wynosi 0,25 jednostki. Cienie liczone z tak zaokrąglonej pozycji miały
    // regularne paski (shadow acne) niezależnie od biasu.
    static VkFormat gbufferAttachmentFormat(int attachment) {
        return attachment == 3 ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    VkImage gbufferImages_[kGBufferAttachments][kMaxFrames] = {};
    VkDeviceMemory gbufferMemories_[kGBufferAttachments][kMaxFrames] = {};
    VkImageView gbufferViews_[kGBufferAttachments][kMaxFrames] = {};
    VkSampler gbufferSampler_ = VK_NULL_HANDLE;

    // GBuffer pipeline
    VkPipelineLayout gbufferPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline gbufferPipeline_ = VK_NULL_HANDLE;

    // GBuffer skinned pipeline
    VkPipelineLayout gbufferSkinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline gbufferSkinnedPipeline_ = VK_NULL_HANDLE;

    // Deferred lighting pipeline
    VkPipelineLayout deferredPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline deferredPipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout deferredDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool deferredDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet deferredDescriptorSets_[kMaxFrames] = {};

    // Ocean scene color (captured scene before ocean renders)
    VkImage sceneColorImages_[kMaxFrames] = {};
    VkDeviceMemory sceneColorMemories_[kMaxFrames] = {};
    VkImageView sceneColorViews_[kMaxFrames] = {};
    VkSampler sceneColorSampler_ = VK_NULL_HANDLE;

};

// Facade
Renderer::Renderer(
    Platform* platform,
    MaterialManager* materialManager,
    std::shared_ptr<VulkanContext> context,
    AssetRegistry* assetRegistry)
    : impl_(std::make_unique<RendererImpl>(platform, materialManager, context, assetRegistry)) {}
Renderer::~Renderer() = default;
bool Renderer::initialize(const std::string& appName, bool enableValidationLayers) {
    return impl_->initialize(appName, enableValidationLayers);
}
void Renderer::cleanup() { impl_->cleanup(); }
void Renderer::render(const RenderScene& s, const RenderView& v) {
    impl_->render(s, v);
}
void Renderer::addFeature(std::shared_ptr<IRenderFeature> feature) {
    impl_->addFeature(feature, this);
}
IRenderFeature* Renderer::getFeature(const std::string& name) {
    return impl_->getFeature(name);
}
void Renderer::removeFeature(const std::string& name) {
    impl_->removeFeature(name);
}
void Renderer::waitIdle() {
    if (impl_) impl_->waitIdle();
}
uint32_t Renderer::width() const { return impl_->extent().width; }
uint32_t Renderer::height() const { return impl_->extent().height; }
VkFormat Renderer::colorFormat() const { return impl_->colorFormat(); }
VkFormat Renderer::depthFormat() const { return impl_->depthFormat(); }
TextureHandle Renderer::loadTexture(const std::string& n, const std::string& p, bool m, bool disableAlpha) { return impl_->loadTexture(n, p, m, disableAlpha); }
TextureHandle Renderer::loadTextureFromMemory(const std::string& n, const std::vector<float>& d, uint32_t w, uint32_t h, bool m) { return impl_->loadTextureFromMemory(n, d, w, h, m); }
uint32_t Renderer::getTextureIndex(TextureHandle handle) const {
    return impl_->getTextureIndex(handle);
}

uint32_t Renderer::getTextureIndex(const std::string& name) const {
    return impl_->getTextureIndex(name);
}
void Renderer::setClearColor(float r, float g, float b, float a) { impl_->clearColor_ = {{r,g,b,a}}; }
void Renderer::setSynchronizationMode(SynchronizationMode mode) { impl_->setSynchronizationMode(mode); }
SynchronizationMode Renderer::synchronizationMode() const { return impl_->synchronizationMode(); }
void Renderer::setPresentMode(PresentMode mode) { impl_->setPresentMode(mode); }
PresentMode Renderer::presentMode() const { return impl_->presentMode(); }
void Renderer::setShadowsEnabled(bool enabled) { impl_->setShadowsEnabled(enabled); }
bool Renderer::shadowsEnabled() const { return impl_->shadowsEnabled(); }
void Renderer::setWireframeEnabled(bool enabled) { impl_->setWireframeEnabled(enabled); }
bool Renderer::isWireframeEnabled() const { return impl_->isWireframeEnabled(); }
void Renderer::setTessellationEnabled(bool enabled) { impl_->setTessellationEnabled(enabled); }
bool Renderer::isTessellationEnabled() const { return impl_->isTessellationEnabled(); }
bool Renderer::isTessellationSupported() const { return impl_->isTessellationSupported(); }
void Renderer::setTessellationLevel(float level) { impl_->setTessellationLevel(level); }
float Renderer::getTessellationLevel() const { return impl_->getTessellationLevel(); }
void Renderer::setTextureFilter(TextureFilter filter) { impl_->setTextureFilter(filter); }
TextureFilter Renderer::getTextureFilter() const { return impl_->getTextureFilter(); }
void Renderer::configureDirectionalShadow(float halfSize, float startDistance, float splitLambda) {
    impl_->setDirectionalShadowConfig(halfSize, startDistance, splitLambda);
}
void Renderer::setLightingEnabled(bool e) { impl_->setLightingEnabled(e); }
bool Renderer::isLightingEnabled() const { return impl_->isLightingEnabled(); }
void Renderer::setAmbientStrength(float s) { impl_->setAmbientStrength(s); }
void Renderer::setAmbientColor(float r, float g, float b) { impl_->setAmbientColor(r, g, b); }
void Renderer::setLights(const std::vector<SceneLight>& lights) { impl_->setLights(lights); }

void Renderer::setFogEnabled(bool enabled) { if (impl_) impl_->setFogEnabled(enabled); }
void Renderer::setFogOfWar(bool enabled, float originX, float originZ, float worldSize, float exploredBrightness) {
    if (impl_) impl_->setFogOfWar(enabled, originX, originZ, worldSize, exploredBrightness);
}
void Renderer::updateFogOfWar(const std::vector<uint8_t>& visibility) {
    if (impl_) impl_->updateFogOfWar(visibility);
}
bool Renderer::isFogEnabled() const { return impl_ ? impl_->isFogEnabled() : false; }
void Renderer::setFogColor(float r, float g, float b) { if (impl_) impl_->setFogColor(r, g, b); }
void Renderer::setFogStart(float start) { if (impl_) impl_->setFogStart(start); }
void Renderer::setFogEnd(float end) { if (impl_) impl_->setFogEnd(end); }
void Renderer::setFogDensity(float density) { if (impl_) impl_->setFogDensity(density); }

void Renderer::prepareRenderPacket(RenderPacket& packet) const {
    impl_->prepareRenderPacket(packet);
}

VkDevice Renderer::device() const { return impl_->device(); }
VkPhysicalDevice Renderer::physicalDevice() const { return impl_->physicalDevice(); }

VkDescriptorSetLayout Renderer::descriptorSetLayout() const { return impl_->descriptorSetLayout(); }
VkCommandPool Renderer::commandPool() const { return impl_->commandPool(); }
VkQueue Renderer::graphicsQueue() const { return impl_->graphicsQueue(); }
VkExtent2D Renderer::extent() const { return impl_->extent(); }
VkDescriptorSet Renderer::getTextureDescriptorSet(const std::string& n, uint32_t f) const {
    return impl_->getDescriptorSet(f); // Always return global set for bindless
}
VkBuffer Renderer::getUniformBuffer(uint32_t f) const {
    return impl_->getUniformBuffer(f);
}
bool Renderer::getTextureInfo(const std::string& n, VkImageView& v, VkSampler& s) const {
    return impl_->getTextureInfo(n, v, s);
}

bool Renderer::getTextureInfo(TextureHandle handle, VkImageView& v, VkSampler& s) const {
    return impl_->getTextureInfo(handle, v, s);
}

void Renderer::setWindTime(float time) { if (impl_) impl_->setWindTime(time); }
float Renderer::getWindTime() const { return impl_ ? impl_->getWindTime() : 0.0f; }
void Renderer::setWindStrength(float strength) { if (impl_) impl_->setWindStrength(strength); }
float Renderer::getWindStrength() const { return impl_ ? impl_->getWindStrength() : 0.3f; }

} // namespace engine3d
