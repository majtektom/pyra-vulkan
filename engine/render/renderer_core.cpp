#include <array>
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

constexpr int kMaxFrames = 2;
constexpr int kMaxDescriptorSets = 64;

constexpr int kMaxLights = 16;

struct LightData {
    Matrix4 lightViewProj;
    float position[4];    // xyz = world position (point/spot), w = shadow map index
    float direction[4];   // xyz = normalized direction OF light rays, w = type: 1=dir, 2=point, 3=spot
    float color[4];       // rgb = color, w = intensity multiplier
    float params[4];      // spot: innerCutoffCos, outerCutoffCos, range, pad
};

struct GlobalUBO {
    Matrix4 view;
    Matrix4 proj;
    float camRight[4];
    float camUp[4];
    float cameraPos[4];
    float ambientColor[4];
    float ambientStrength;
    uint32_t lightingEnabled;
    uint32_t lightCount;
    uint32_t shadowsEnabled;
    LightData lights[kMaxLights];
    float fogColor[4];
    float fogStart;
    float fogEnd;
    float fogDensity;
    uint32_t fogEnabled;
};

struct TextureResources {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
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

            passes_.push_back(std::make_unique<OpaquePass>());
            passes_.push_back(std::make_unique<TransparentPass>());

            createSyncObjects();
            createCommandBuffers();
            createShadowResources();
            createShadowPipeline();
            createShadowSkinnedPipeline();
            createGBufferResources();
            LOG_INFO("Deferred PBR Renderer initialized.");
            return true;
        } catch (const std::exception& e) {
            LOG_CRITICAL("Renderer Init Error: {}", e.what());
            return false;
        }
    }

    void render(const RenderScene& scene, const RenderView& view) {
        if (!vulkanContext_ || !vulkanContext_->device()) return;

        if (platform_->wasResized()) {
            platform_->clearResizeFlag();
            recreateSwapChain();
        }

        if (!swapchain_) return;

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

        updateUniformBuffer(view);

        VkCommandBuffer cmd = commandBuffers_[currentFrame_];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

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
            shadowBarrier.subresourceRange.layerCount = kMaxLights;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &shadowBarrier);

            for (uint32_t i = 0; i < std::min(storedLightCount_, (uint32_t)kMaxLights); i++) {
                VkRenderingAttachmentInfo shadowDepthAttachment{};
                shadowDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                shadowDepthAttachment.imageView = shadowMapLayerViews_[i];
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

                VkViewport shadowViewport{0.0f, 0.0f, (float)kShadowMapSize, (float)kShadowMapSize, 0.0f, 1.0f};
                VkRect2D shadowScissor{{0, 0}, {kShadowMapSize, kShadowMapSize}};
                vkCmdSetViewport(cmd, 0, 1, &shadowViewport);
                vkCmdSetScissor(cmd, 0, 1, &shadowScissor);

                for (const auto& packet : scene.getShadowPackets()) {
                    bool isSkinned = (packet.boneDescriptorSet != VK_NULL_HANDLE);

                    if (isSkinned) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowSkinnedPipeline_);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowSkinnedPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowSkinnedPipelineLayout_, 1, 1, &packet.boneDescriptorSet, 0, nullptr);
                    } else {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
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
                    push.lvp = storedLights_[i].lightViewProj;
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

                vkCmdEndRendering(cmd);
            }

            shadowBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            shadowBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            shadowBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            shadowBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &shadowBarrier);
        }

        auto getTextureIndex = [this](const std::string& name) -> uint32_t {
            auto it = textureNameToIndex_.find(name);
            return it != textureNameToIndex_.end() ? it->second : 0;
        };

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

        auto opaquePackets = scene.getOpaquePackets();

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
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
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

        // Skybox / BeforeOpaque features (render directly to swapchain)
        RenderOrchestrator::Callbacks callbacks;
        callbacks.onFeaturesBeforeOpaque = [&]() {
            executeFeatures(cmd, view, IRenderFeature::Stage::BeforeOpaque);
        };
        callbacks.onFeaturesAfterOpaque = [&]() {
            executeFeatures(cmd, view, IRenderFeature::Stage::AfterOpaque);
        };
        callbacks.onDeferredLighting = [&]() {
            // Deferred lighting pass (fullscreen, reads GBuffer)
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipelineLayout_, 1, 1, &deferredDescriptorSets_[currentFrame_], 0, nullptr);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        };
        callbacks.onOpaquePass = [&]() {
            for (auto& pass : passes_) {
                if (pass->getName() == "OpaquePass") {
                    pass->execute(cmd, currentFrame_, scene, pipelineLayout_);
                }
            }
        };
        callbacks.onFeaturesAfterTransparent = [&]() {
            executeFeatures(cmd, view, IRenderFeature::Stage::AfterTransparent);
        };
        callbacks.onTransparentPass = [&]() {
            for (auto& pass : passes_) {
                if (pass->getName() == "TransparentPass") {
                    pass->execute(cmd, currentFrame_, scene, pipelineLayout_);
                }
            }
        };
        callbacks.onFeaturesOverlay = [&]() {
            executeFeatures(cmd, view, IRenderFeature::Stage::Overlay);
        };

        orchestrator_.execute(renderPlan_, callbacks);

        vkCmdEndRendering(cmd);

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

            VkSemaphore signalSemaphores[] = {presentReadySemaphores_[currentFrame_], renderTimelineSemaphores_[currentFrame_]};
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
            VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;
            vkQueueSubmit(vulkanContext_->graphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]);
        }
        VkSemaphore presentWaitSemaphore = (synchronizationMode_ == SynchronizationMode::Timeline)
            ? presentReadySemaphores_[currentFrame_] : renderFinishedSemaphores_[currentFrame_];
        if (!swapchain_->present(imageIndex, presentWaitSemaphore))
            recreateSwapChain();
        currentFrame_ = (currentFrame_ + 1) % kMaxFrames;
    }

    TextureHandle loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat) {
        VkDevice device = vulkanContext_->device();
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

        tex.imageView = vk::createImageView(device, tex.image, uploadData.format,
                                            VK_IMAGE_ASPECT_COLOR_BIT, uploadData.mipLevels);
        tex.sampler = mirroredRepeat ? vk::createMirrorRepeatSampler(device, uploadData.mipLevels) : vk::createTextureSampler(device, uploadData.mipLevels);
        textures_[name] = tex;
        
        registerTextureInBindless(name, tex.imageView, tex.sampler);
        LOG_INFO("Loaded texture: {} at index {}", name, textureNameToIndex_[name]);

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
    
    uint32_t getTextureIndex(const std::string& name) const {
        auto it = textureNameToIndex_.find(name);
        if (it != textureNameToIndex_.end()) return it->second;
        return 0;
    }

    uint32_t getTextureIndex(TextureHandle handle) const {
        if (!handle.isValid()) {
            return 0;
        }

        auto it = textureHandleToIndex_.find(handle.value);
        if (it != textureHandleToIndex_.end()) {
            return it->second;
        }
        return 0;
    }

    void prepareRenderPacket(RenderPacket& packet) {
        const uint32_t defaultDiffuse = getTextureIndex("default");
        const uint32_t defaultNormal = getTextureIndex("default_normal");
        const uint32_t defaultOrm = getTextureIndex("default_orm");
        const uint32_t defaultEmissive = getTextureIndex("default_black");
        const uint32_t defaultMask = getTextureIndex("default_black");

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
            : defaultDiffuse;

        if (packet.normalTextureHandle.isValid()) {
            packet.normalTextureIndex = getTextureIndex(packet.normalTextureHandle);
        } else {
            packet.normalTextureIndex = defaultNormal;
            packet.bumpIntensity = 0.0f;
        }

        packet.metallicRoughnessTextureIndex = packet.metallicRoughnessTextureHandle.isValid()
            ? getTextureIndex(packet.metallicRoughnessTextureHandle)
            : defaultOrm;

        packet.occlusionTextureIndex = packet.occlusionTextureHandle.isValid()
            ? getTextureIndex(packet.occlusionTextureHandle)
            : defaultDiffuse;

        packet.emissiveTextureIndex = packet.emissiveTextureHandle.isValid()
            ? getTextureIndex(packet.emissiveTextureHandle)
            : defaultEmissive;

        packet.layerCount = std::min(packet.layerCount, kMaxTerrainLayers);
        for (uint32_t i = 0; i < kMaxTerrainLayers; ++i) {
            packet.layerTextureIndices[i] = defaultDiffuse;
            packet.layerMaskIndices[i] = defaultMask;
        }

        if (packet.layerBlendEnabled && packet.layerCount > 0) {
            for (uint32_t i = 0; i < packet.layerCount; ++i) {
                packet.layerTextureIndices[i] = packet.layerTextureHandles[i].isValid()
                    ? getTextureIndex(packet.layerTextureHandles[i])
                    : defaultDiffuse;
                if (i > 0 && packet.layerMaskHandles[i].isValid()) {
                    packet.layerMaskIndices[i] = getTextureIndex(packet.layerMaskHandles[i]);
                }
            }
        }
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
        for (int f = 0; f < 2; f++) {
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

    void cleanup() {
        if (cleanedUp_ || !vulkanContext_ || !vulkanContext_->device()) return;
        VkDevice device = vulkanContext_->device();
        vkDeviceWaitIdle(device);

        features_.clear();
        passes_.clear();

        cleanupGBuffer();
        if (shadowMapView_) { vkDestroyImageView(device, shadowMapView_, nullptr); shadowMapView_ = VK_NULL_HANDLE; }
        for (auto view : shadowMapLayerViews_) {
            if (view) vkDestroyImageView(device, view, nullptr);
        }
        shadowMapLayerViews_.clear();
        if (shadowMapImage_) { vkDestroyImage(device, shadowMapImage_, nullptr); shadowMapImage_ = VK_NULL_HANDLE; }
        if (shadowMapMemory_) { vkFreeMemory(device, shadowMapMemory_, nullptr); shadowMapMemory_ = VK_NULL_HANDLE; }
        if (shadowSampler_) { vkDestroySampler(device, shadowSampler_, nullptr); shadowSampler_ = VK_NULL_HANDLE; }
        if (shadowPipeline_) { vkDestroyPipeline(device, shadowPipeline_, nullptr); shadowPipeline_ = VK_NULL_HANDLE; }
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
        for (int i = 0; i < kMaxFrames; i++) {
            if (imageAvailableSemaphores_[i]) { vkDestroySemaphore(device, imageAvailableSemaphores_[i], nullptr); imageAvailableSemaphores_[i] = VK_NULL_HANDLE; }
            if (renderFinishedSemaphores_[i]) { vkDestroySemaphore(device, renderFinishedSemaphores_[i], nullptr); renderFinishedSemaphores_[i] = VK_NULL_HANDLE; }
            if (presentReadySemaphores_[i]) { vkDestroySemaphore(device, presentReadySemaphores_[i], nullptr); presentReadySemaphores_[i] = VK_NULL_HANDLE; }
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
            swapchain_->recreate(platform_->width(), platform_->height());
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

    void setAmbientStrength(float s) { ambientStrength_ = s; }

    void setDirectionalShadowConfig(float halfSize, bool /*unused*/) {
        dirShadowHalfSize_ = halfSize;
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
    IRenderFeature* getFeature(const std::string& name) {
        for (auto& f : features_) {
            if (f->getName() == name) return f.get();
        }
        return nullptr;
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

    void updateUniformBuffer(const RenderView& view) {
        GlobalUBO ubo{};
        ubo.view = view.view;
        ubo.proj = view.projection;
        std::memcpy(ubo.camRight, &view.cameraRight, sizeof(Vec3));
        std::memcpy(ubo.camUp, &view.cameraUp, sizeof(Vec3));
        std::memcpy(ubo.cameraPos, &view.cameraPosition, sizeof(Vec3));
        
        ubo.lightingEnabled = lightingEnabled_ ? 1u : 0u;
        ubo.shadowsEnabled = shadowsEnabled_ ? 1u : 0u;
        ubo.ambientStrength = ambientStrength_;
        ubo.lightCount = std::min(storedLightCount_, (uint32_t)kMaxLights);
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

        for (uint32_t i = 0; i < ubo.lightCount; i++) {
            LightData& l = storedLights_[i];
            uint32_t type = (uint32_t)l.direction[3];
            
            Matrix4 lView, lProj;
            Vec3 lPos = {l.position[0], l.position[1], l.position[2]};
            Vec3 lDir = {l.direction[0], l.direction[1], l.direction[2]};
            
            if (type == 1) { // Directional
                Vec3 camPos = view.cameraPosition;
                Vec3 target = {camPos.x, 0.0f, camPos.z};
                float halfSize = dirShadowHalfSize_;
                float dist = halfSize * 1.5f;
                lPos = {target.x + lDir.x * dist, target.y + lDir.y * dist, target.z + lDir.z * dist};
                Vec3 up = std::abs(lDir.y) > 0.99f ? Vec3{1,0,0} : Vec3{0,1,0};
                lView = lookAt(lPos, target, up);
                float farPlane = dist + halfSize * 3.0f;
                lProj = ortho(-halfSize, halfSize, -halfSize, halfSize, 0.5f, farPlane);
            } else if (type == 3) { // Spot
                Vec3 target = add(lPos, lDir);
                Vec3 up = std::abs(lDir.y) > 0.99f ? Vec3{1,0,0} : Vec3{0,1,0};
                lView = lookAt(lPos, target, up);
                float outerAngle = std::acos(l.params[1]) * 2.0f;
                lProj = perspective(outerAngle, 1.0f, 0.1f, l.params[2]);
            } else if (type == 2) { // Point
                // Point lights look straight down at the scene center for better floor coverage
                Vec3 target = {lPos.x, 0.0f, lPos.z};
                lView = lookAt(lPos, target, Vec3{0, 0, 1});
                lProj = perspective(radians(120.0f), 1.0f, 0.1f, l.params[2]);
            } else {
                lView = Matrix4::identity();
                lProj = Matrix4::identity();
            }
            
            l.lightViewProj = multiply(lProj, lView);
            l.position[3] = (float)i; 
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
        defaultSampler_ = vk::createTextureSampler(device, 1);
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
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = kMaxBindlessTextures;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorBindingFlags bindingFlags[3] = {0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, 0};
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
        cleanupGBuffer();
        swapchain_->recreate(platform_->width(), platform_->height());
        createGBufferResources();
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
                        shadowMapImage_, shadowMapMemory_, kMaxLights);

        shadowMapView_ = vk::createImageView(device, shadowMapImage_, VK_FORMAT_D32_SFLOAT,
                                              VK_IMAGE_ASPECT_DEPTH_BIT, 1, kMaxLights, VK_IMAGE_VIEW_TYPE_2D_ARRAY);

        shadowMapLayerViews_.resize(kMaxLights);
        for (uint32_t i = 0; i < kMaxLights; i++) {
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

        transitionShadowMapLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);
        shadowMapLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    void transitionShadowMapLayout(VkImageLayout newLayout, VkImageLayout afterShadow, bool initial) {
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
        barrier.subresourceRange.layerCount = kMaxLights;

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
        shadowMapLayout_ = initial ? afterShadow : newLayout;
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
        auto attrDescs = Vertex::attributeDescriptions();

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

        for (int f = 0; f < 2; f++) {
            for (int a = 0; a < kGBufferAttachments; a++) {
                VkImageCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                ci.imageType = VK_IMAGE_TYPE_2D;
                ci.extent = {ext.width, ext.height, 1};
                ci.mipLevels = 1; ci.arrayLayers = 1;
                ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
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
                vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
                vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                vi.subresourceRange.levelCount = 1;
                vi.subresourceRange.layerCount = 1;
                vkCreateImageView(device, &vi, nullptr, &gbufferViews_[a][f]);

                vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                          gbufferImages_[a][f], VK_FORMAT_R16G16B16A16_SFLOAT,
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
        auto attrDescs = Vertex::attributeDescriptions();

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

        VkFormat gbufferFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat depthFmt = swapchain_->depthFormat();
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 4;
        VkFormat formats[4] = {gbufferFormat, gbufferFormat, gbufferFormat, gbufferFormat};
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

        VkFormat gbufferFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat depthFmt = swapchain_->depthFormat();
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 4;
        VkFormat formats[4] = {gbufferFormat, gbufferFormat, gbufferFormat, gbufferFormat};
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
        VkDescriptorSetLayoutBinding gbufferBindings[5] = {};
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

        VkDescriptorSetLayoutCreateInfo dslInfo{};
        dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslInfo.bindingCount = 5;
        dslInfo.pBindings = gbufferBindings;
        vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &deferredDescriptorSetLayout_);

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 * 2};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &deferredDescriptorPool_);

        for (int f = 0; f < 2; f++) {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = deferredDescriptorPool_;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &deferredDescriptorSetLayout_;
            vkAllocateDescriptorSets(device, &allocInfo, &deferredDescriptorSets_[f]);

            VkWriteDescriptorSet writes[5] = {};
            VkDescriptorImageInfo imageInfos[5] = {};
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
            vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
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
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]);
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &presentReadySemaphores_[i]);
            vkCreateSemaphore(device, &timelineInfo, nullptr, &renderTimelineSemaphores_[i]);
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences_[i]);
            timelineCounters_[i] = 0;
        }
        globalTimelineCounter_ = 0;
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
    void executeFeatures(VkCommandBuffer cmd, const RenderView& view, IRenderFeature::Stage stage) {
        if (stage == IRenderFeature::Stage::Overlay) {
            VkClearAttachment clearAttachment{};
            clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            clearAttachment.clearValue.depthStencil = {1.0f, 0};

            VkClearRect clearRect{};
            clearRect.rect.offset = {0, 0};
            clearRect.rect.extent = swapchain_->extent();
            clearRect.baseArrayLayer = 0;
            clearRect.layerCount = 1;

            vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

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
    std::unordered_map<uint32_t, uint32_t> textureHandleToIndex_;
    uint32_t nextTextureIndex_ = 0;

    std::array<VkSemaphore, 2> imageAvailableSemaphores_;
    std::array<VkSemaphore, 2> renderFinishedSemaphores_;
    std::array<VkSemaphore, 2> presentReadySemaphores_;
    std::array<VkSemaphore, 2> renderTimelineSemaphores_;
    std::array<uint64_t, 2> timelineCounters_{};
    uint64_t globalTimelineCounter_ = 0;
    std::array<VkFence, 2> inFlightFences_;
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
    float dirShadowHalfSize_ = 100.0f;
    bool cleanedUp_ = false;
    bool fogEnabled_ = false;
    float fogColor_[3] = {0.8f, 0.8f, 0.8f};
    float fogStart_ = 10.0f;
    float fogEnd_ = 100.0f;
    float fogDensity_ = 0.01f;

    LightData storedLights_[kMaxLights] = {};
    uint32_t storedLightCount_ = 0;

    static constexpr uint32_t kShadowMapSize = 2048;
    VkImage shadowMapImage_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowMapMemory_ = VK_NULL_HANDLE;
    VkImageView shadowMapView_ = VK_NULL_HANDLE;
    std::vector<VkImageView> shadowMapLayerViews_;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline shadowSkinnedPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowSkinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkImageLayout shadowMapLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    // GBuffer
    static constexpr int kGBufferAttachments = 4;
    VkImage gbufferImages_[kGBufferAttachments][2] = {};
    VkDeviceMemory gbufferMemories_[kGBufferAttachments][2] = {};
    VkImageView gbufferViews_[kGBufferAttachments][2] = {};
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
    VkDescriptorSet deferredDescriptorSets_[2] = {};
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
void Renderer::waitIdle() {
    if (impl_) impl_->waitIdle();
}
uint32_t Renderer::width() const { return impl_->extent().width; }
uint32_t Renderer::height() const { return impl_->extent().height; }
VkFormat Renderer::colorFormat() const { return impl_->colorFormat(); }
VkFormat Renderer::depthFormat() const { return impl_->depthFormat(); }
TextureHandle Renderer::loadTexture(const std::string& n, const std::string& p, bool m) { return impl_->loadTexture(n, p, m); }
void Renderer::setClearColor(float r, float g, float b, float a) { impl_->clearColor_ = {{r,g,b,a}}; }
void Renderer::setSynchronizationMode(SynchronizationMode mode) { impl_->setSynchronizationMode(mode); }
SynchronizationMode Renderer::synchronizationMode() const { return impl_->synchronizationMode(); }
void Renderer::setPresentMode(PresentMode mode) { impl_->setPresentMode(mode); }
PresentMode Renderer::presentMode() const { return impl_->presentMode(); }
void Renderer::setShadowsEnabled(bool enabled) { impl_->setShadowsEnabled(enabled); }
bool Renderer::shadowsEnabled() const { return impl_->shadowsEnabled(); }
void Renderer::setWireframeEnabled(bool enabled) { impl_->setWireframeEnabled(enabled); }
bool Renderer::isWireframeEnabled() const { return impl_->isWireframeEnabled(); }
void Renderer::configureDirectionalShadow(float halfSize) { impl_->setDirectionalShadowConfig(halfSize, false); }
void Renderer::setLightingEnabled(bool e) { impl_->setLightingEnabled(e); }
bool Renderer::isLightingEnabled() const { return impl_->isLightingEnabled(); }
void Renderer::setAmbientStrength(float s) { impl_->setAmbientStrength(s); }
void Renderer::setAmbientColor(float r, float g, float b) { impl_->setAmbientColor(r, g, b); }
void Renderer::setLights(const std::vector<SceneLight>& lights) { impl_->setLights(lights); }

void Renderer::setFogEnabled(bool enabled) { if (impl_) impl_->setFogEnabled(enabled); }
bool Renderer::isFogEnabled() const { return impl_ ? impl_->isFogEnabled() : false; }
void Renderer::setFogColor(float r, float g, float b) { if (impl_) impl_->setFogColor(r, g, b); }
void Renderer::setFogStart(float start) { if (impl_) impl_->setFogStart(start); }
void Renderer::setFogEnd(float end) { if (impl_) impl_->setFogEnd(end); }
void Renderer::setFogDensity(float density) { if (impl_) impl_->setFogDensity(density); }

uint32_t Renderer::getTextureIndex(TextureHandle handle) const { return impl_->getTextureIndex(handle); }
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

} // namespace engine3d
