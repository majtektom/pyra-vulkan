#include "renderer.h"
#include "backend/vulkan_context.h"
#include "backend/vulkan_swapchain.h"
#include "render_pass.h"
#include "standard_passes.h"
#include "platform.h"
#include "entity.h"
#include "mesh_component.h"
#include "texture_manager.h"
#include "vulkan_util.h"
#include "texture_asset_loader.h"
#include "camera_component.h"
#include <cstring>
#include <iostream>
#include <array>
#include <unordered_map>

#include "render_feature.h"
#include <vector>

namespace engine3d {

constexpr int kMaxFrames = 2;
constexpr int kMaxDescriptorSets = 64;

constexpr int kMaxLights = 4;

struct LightData {
    float position[4];    // xyz = world position (point/spot), w = pad
    float direction[4];   // xyz = normalized direction OF light rays (NOT negated), w = type: 1=dir, 2=point, 3=spot
    float color[4];       // rgb = color, w = intensity multiplier
    float params[4];      // spot: innerCutoffCos, outerCutoffCos, range, pad
};

struct GlobalUBO {
    Matrix4 view;
    Matrix4 proj;
    float camRight[4];
    float camUp[4];
    Matrix4 lightViewProj;
    float _padLightDir[4];
    uint32_t shadowMapIndex;
    uint32_t lightingEnabled;
    float ambientStrength;
    uint32_t lightCount;
    float _padLightColor[4];
    float ambientColor[4];
    float cameraPos[4];
    LightData lights[kMaxLights];
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
    RendererImpl(Platform* platform, MaterialManager* materialManager, std::shared_ptr<VulkanContext> context)
        : platform_(platform), materialManager_(materialManager), vulkanContext_(context) {}

    ~RendererImpl() { cleanup(); }

    bool initialize(const std::string& appName, bool enableValidationLayers) {
        if (!vulkanContext_) return false;
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
            createGBufferResources();
            std::cout << "Deferred PBR Renderer initialized." << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Renderer Init Error: " << e.what() << std::endl;
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
            shadowBarrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &shadowBarrier);

            VkRenderingAttachmentInfo shadowDepthAttachment{};
            shadowDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            shadowDepthAttachment.imageView = shadowMapView_;
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

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);

            for (const auto& packet : scene.getOpaquePackets()) {
                if (!packet.material || packet.vertexBuffer == VK_NULL_HANDLE) continue;
                VkBuffer vbs[] = {packet.vertexBuffer};
                VkDeviceSize off[] = {0};
                vkCmdBindVertexBuffers(cmd, 0, 1, vbs, off);
                if (packet.indexBuffer != VK_NULL_HANDLE)
                    vkCmdBindIndexBuffer(cmd, packet.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(Matrix4), &packet.transform);

                if (packet.indexBuffer != VK_NULL_HANDLE)
                    vkCmdDrawIndexed(cmd, packet.indexCount, 1, 0, 0, 0);
            }

            vkCmdEndRendering(cmd);

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
            if (!packet.material || packet.vertexBuffer == VK_NULL_HANDLE) continue;

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

            struct { Matrix4 m; std::array<float,4> bc; float met; float rough; float bump; uint32_t bts; uint32_t pts; uint32_t nts; uint32_t ots; uint32_t ets; float amc; float _p[3]; } push;
            push.m = packet.transform; push.bc = packet.materialColor;
            push.met = packet.metallicFactor; push.rough = packet.roughnessFactor;
            push.bump = packet.bumpIntensity; push.bts = packet.diffuseTextureIndex;
            push.pts = packet.metallicRoughnessTextureIndex; push.nts = packet.normalTextureIndex;
            push.ots = packet.occlusionTextureIndex; push.ets = packet.emissiveTextureIndex;
            push.amc = 0.5f;

            VkPipelineLayout layout = isSkinned ? gbufferSkinnedPipelineLayout_ : gbufferPipelineLayout_;
            vkCmdPushConstants(cmd, layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, 128, &push);

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
        executeFeatures(cmd, view, IRenderFeature::Stage::BeforeOpaque);
        executeFeatures(cmd, view, IRenderFeature::Stage::AfterOpaque);

        // Deferred lighting pass (fullscreen, reads GBuffer)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipelineLayout_, 1, 1, &deferredDescriptorSets_[currentFrame_], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        // AfterTransparent features + transparent pass + overlay
        executeFeatures(cmd, view, IRenderFeature::Stage::AfterTransparent);

        for (auto& pass : passes_) {
            if (pass->getName() == "TransparentPass") {
                pass->execute(cmd, currentFrame_, scene, pipelineLayout_);
            }
        }

        executeFeatures(cmd, view, IRenderFeature::Stage::Overlay);

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

    void loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat) {
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
        std::cout << "Loaded texture: " << name << " at index " << textureNameToIndex_[name] << std::endl;
    }

    void linkNormalMap(const std::string& diffuseName, const std::string& normalName) {
        normalMapLinks_[diffuseName] = normalName;
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

    uint32_t getNormalIndex(const std::string& diffuseName) const {
        auto it = normalMapLinks_.find(diffuseName);
        if (it != normalMapLinks_.end()) return getTextureIndex(it->second);
        return getTextureIndex("default_normal");
    }

    void prepareRenderPacket(RenderPacket& packet) {
        packet.diffuseTextureIndex = packet.textureName.empty() ? getTextureIndex("default") : getTextureIndex(packet.textureName);
        packet.normalTextureIndex = packet.normalMapName.empty()
            ? getNormalIndex(packet.textureName)
            : getTextureIndex(packet.normalMapName);
        packet.metallicRoughnessTextureIndex = packet.pbrMetallicRoughnessTextureName.empty()
            ? getTextureIndex("default_orm") : getTextureIndex(packet.pbrMetallicRoughnessTextureName);
        packet.occlusionTextureIndex = packet.pbrOcclusionTextureName.empty()
            ? getTextureIndex("default") : getTextureIndex(packet.pbrOcclusionTextureName);
        packet.emissiveTextureIndex = packet.pbrEmissiveTextureName.empty()
            ? getTextureIndex("default_black") : getTextureIndex(packet.pbrEmissiveTextureName);
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
        if (!vulkanContext_ || !vulkanContext_->device()) return;
        VkDevice device = vulkanContext_->device();
        vkDeviceWaitIdle(device);
        cleanupGBuffer();
        for (auto& [name, tex] : textures_) {
            if (tex.sampler) vkDestroySampler(device, tex.sampler, nullptr);
            if (tex.imageView) vkDestroyImageView(device, tex.imageView, nullptr);
            if (tex.image) {
                vkDestroyImage(device, tex.image, nullptr);
                vkFreeMemory(device, tex.imageMemory, nullptr);
            }
        }
        textures_.clear();
        for (int i = 0; i < kMaxFrames; i++) {
            vkDestroySemaphore(device, imageAvailableSemaphores_[i], nullptr);
            vkDestroySemaphore(device, renderFinishedSemaphores_[i], nullptr);
            vkDestroySemaphore(device, presentReadySemaphores_[i], nullptr);
            vkDestroySemaphore(device, renderTimelineSemaphores_[i], nullptr);
            vkDestroyFence(device, inFlightFences_[i], nullptr);
            if (uniformBuffersMapped_[i]) vkUnmapMemory(device, uniformBuffersMemory_[i]);
            if (uniformBuffers_[i]) { vkDestroyBuffer(device, uniformBuffers_[i], nullptr);
                vkFreeMemory(device, uniformBuffersMemory_[i], nullptr); }
        }
        if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        if (descriptorSetLayout_) vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
        if (boneDescriptorSetLayout_) vkDestroyDescriptorSetLayout(device, boneDescriptorSetLayout_, nullptr);
        if (pipelineLayout_) vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        
        if (defaultSampler_) vkDestroySampler(device, defaultSampler_, nullptr);
        if (defaultImageView_) vkDestroyImageView(device, defaultImageView_, nullptr);
        if (defaultImage_) { vkDestroyImage(device, defaultImage_, nullptr); vkFreeMemory(device, defaultImageMemory_, nullptr); }
        if (defaultNormalSampler_) vkDestroySampler(device, defaultNormalSampler_, nullptr);
        if (defaultNormalImageView_) vkDestroyImageView(device, defaultNormalImageView_, nullptr);
        if (defaultNormalImage_) { vkDestroyImage(device, defaultNormalImage_, nullptr); vkFreeMemory(device, defaultNormalImageMemory_, nullptr); }
        if (defaultOrmSampler_) vkDestroySampler(device, defaultOrmSampler_, nullptr);
        if (defaultOrmImageView_) vkDestroyImageView(device, defaultOrmImageView_, nullptr);
        if (defaultOrmImage_) { vkDestroyImage(device, defaultOrmImage_, nullptr); vkFreeMemory(device, defaultOrmImageMemory_, nullptr); }
        if (defaultBlackSampler_) vkDestroySampler(device, defaultBlackSampler_, nullptr);
        if (defaultBlackImageView_) vkDestroyImageView(device, defaultBlackImageView_, nullptr);
        if (defaultBlackImage_) { vkDestroyImage(device, defaultBlackImage_, nullptr); vkFreeMemory(device, defaultBlackImageMemory_, nullptr); }

        if (shadowPipeline_) vkDestroyPipeline(device, shadowPipeline_, nullptr);
        if (shadowPipelineLayout_) vkDestroyPipelineLayout(device, shadowPipelineLayout_, nullptr);
        if (shadowSampler_) vkDestroySampler(device, shadowSampler_, nullptr);
        if (shadowMapView_) vkDestroyImageView(device, shadowMapView_, nullptr);
        if (shadowMapImage_) { vkDestroyImage(device, shadowMapImage_, nullptr); vkFreeMemory(device, shadowMapMemory_, nullptr); }
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

    void setLightDirection(float x, float y, float z, float ambient) {
        lightAmbient_ = ambient;
        storedLights_[0].direction[0] = x;
        storedLights_[0].direction[1] = y;
        storedLights_[0].direction[2] = z;
        storedLights_[0].direction[3] = 1.0f; // directional
        if (storedLightCount_ == 0) storedLightCount_ = 1;
    }

    void setLightDirection(const std::array<float, 4>& dir) {
        setLightDirection(dir[0], dir[1], dir[2], dir[3]);
    }

    void setShadowsEnabled(bool enabled) { shadowsEnabled_ = enabled; }
    bool shadowsEnabled() const { return shadowsEnabled_; }

    void setLightColor(float r, float g, float b) {
        storedLights_[0].color[0] = r;
        storedLights_[0].color[1] = g;
        storedLights_[0].color[2] = b;
        storedLights_[0].color[3] = 1.0f;
    }

    void setLightingEnabled(bool enabled) { lightingEnabled_ = enabled; }
    bool isLightingEnabled() const { return lightingEnabled_; }

    void setAmbientStrength(float s) { ambientStrength_ = s; }

    void setAmbientColor(float r, float g, float b) {
        ambientColor_[0] = r; ambientColor_[1] = g; ambientColor_[2] = b;
    }

    void clearLights() {
        storedLightCount_ = 0;
        memset(storedLights_, 0, sizeof(storedLights_));
    }

    uint32_t addDirectionalLight(float dx, float dy, float dz, float r, float g, float b, float intensity) {
        if (storedLightCount_ >= kMaxLights) return storedLightCount_;
        auto& l = storedLights_[storedLightCount_];
        float len = std::sqrt(dx*dx + dy*dy + dz*dz);
        l.direction[0] = dx / len; l.direction[1] = dy / len; l.direction[2] = dz / len;
        l.direction[3] = 1.0f; // directional
        l.color[0] = r; l.color[1] = g; l.color[2] = b; l.color[3] = intensity;
        storedLightCount_++;
        return storedLightCount_;
    }

    uint32_t addPointLight(float px, float py, float pz, float r, float g, float b, float intensity, float range) {
        if (storedLightCount_ >= kMaxLights) return storedLightCount_;
        auto& l = storedLights_[storedLightCount_];
        l.position[0] = px; l.position[1] = py; l.position[2] = pz;
        l.direction[3] = 2.0f; // point
        l.color[0] = r; l.color[1] = g; l.color[2] = b; l.color[3] = intensity;
        l.params[2] = range;
        storedLightCount_++;
        return storedLightCount_;
    }

    uint32_t addSpotLight(float px, float py, float pz, float dx, float dy, float dz,
                          float r, float g, float b, float intensity, float range,
                          float innerAngleDeg, float outerAngleDeg) {
        if (storedLightCount_ >= kMaxLights) return storedLightCount_;
        auto& l = storedLights_[storedLightCount_];
        l.position[0] = px; l.position[1] = py; l.position[2] = pz;
        float dlen = std::sqrt(dx*dx + dy*dy + dz*dz);
        l.direction[0] = dx / dlen; l.direction[1] = dy / dlen; l.direction[2] = dz / dlen;
        l.direction[3] = 3.0f; // spot
        l.color[0] = r; l.color[1] = g; l.color[2] = b; l.color[3] = intensity;
        l.params[0] = std::cos(innerAngleDeg * 3.14159265f / 180.0f);
        l.params[1] = std::cos(outerAngleDeg * 3.14159265f / 180.0f);
        l.params[2] = range;
        storedLightCount_++;
        return storedLightCount_;
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
        ubo.camRight[0] = view.cameraRight.x; ubo.camRight[1] = view.cameraRight.y; ubo.camRight[2] = view.cameraRight.z; ubo.camRight[3] = 0.0f;
        ubo.camUp[0] = view.cameraUp.x; ubo.camUp[1] = view.cameraUp.y; ubo.camUp[2] = view.cameraUp.z; ubo.camUp[3] = 0.0f;
        
        Vec3 lightTarget = {0, 0, 0};
        float dist = 15.0f;
        float ldx = storedLightCount_ > 0 ? storedLights_[0].direction[0] : 0.5f;
        float ldy = storedLightCount_ > 0 ? storedLights_[0].direction[1] : 1.0f;
        float ldz = storedLightCount_ > 0 ? storedLights_[0].direction[2] : 0.3f;
        Vec3 lightPos = {
            lightTarget.x - ldx * dist,
            lightTarget.y - ldy * dist,
            lightTarget.z - ldz * dist
        };
        Vec3 up = {0, 1, 0};
        if (std::abs(ldy) > 0.99f) up = {1, 0, 0};

        Vec3 f = normalize(subtract(lightTarget, lightPos));
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);

        Matrix4 lightView{};
        lightView.values[0] = s.x;   lightView.values[4] = s.y;   lightView.values[8]  = s.z;   lightView.values[12] = -dot(s, lightPos);
        lightView.values[1] = u.x;   lightView.values[5] = u.y;   lightView.values[9]  = u.z;   lightView.values[13] = -dot(u, lightPos);
        lightView.values[2] = f.x;  lightView.values[6] = f.y;  lightView.values[10] = f.z;  lightView.values[14] = -dot(f, lightPos);
        lightView.values[3] = 0;     lightView.values[7] = 0;     lightView.values[11] = 0;     lightView.values[15] = 1;

        float half = 20.0f;
        float zNear = 0.1f;
        float zFar = 100.0f;
        Matrix4 lightProj{};
        lightProj.values[0] = 1.0f/half; lightProj.values[4] = 0;          lightProj.values[8]  = 0;                lightProj.values[12] = 0;
        lightProj.values[1] = 0;          lightProj.values[5] = 1.0f/half; lightProj.values[9]  = 0;                lightProj.values[13] = 0;
        lightProj.values[2] = 0;          lightProj.values[6] = 0;          lightProj.values[10] = 1.0f/(zFar-zNear); lightProj.values[14] = -zNear/(zFar-zNear);
        lightProj.values[3] = 0;          lightProj.values[7] = 0;          lightProj.values[11] = 0;                lightProj.values[15] = 1;

        ubo.lightViewProj = multiply(lightProj, lightView);
        ubo.shadowMapIndex = shadowsEnabled_ ? getTextureIndex("shadow_map") : 0;
        ubo.lightingEnabled = lightingEnabled_ ? 1u : 0u;
        ubo.ambientStrength = ambientStrength_;
        ubo.lightCount = storedLightCount_;
        ubo.ambientColor[0] = ambientColor_[0];
        ubo.ambientColor[1] = ambientColor_[1];
        ubo.ambientColor[2] = ambientColor_[2];
        ubo.ambientColor[3] = 1.0f;

        ubo.cameraPos[0] = view.cameraPosition.x;
        ubo.cameraPos[1] = view.cameraPosition.y;
        ubo.cameraPos[2] = view.cameraPosition.z;
        ubo.cameraPos[3] = 0.0f;

        memcpy(ubo.lights, storedLights_, sizeof(storedLights_));

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
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(kMaxBindlessTextures * kMaxFrames)}
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

        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = kMaxBindlessTextures;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorBindingFlags bindingFlags[2] = {0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT};
        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flagsInfo.bindingCount = 2;
        flagsInfo.pBindingFlags = bindingFlags;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = &flagsInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = 2;
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
                        shadowMapImage_, shadowMapMemory_);

        shadowMapView_ = vk::createImageView(device, shadowMapImage_, VK_FORMAT_D32_SFLOAT,
                                              VK_IMAGE_ASPECT_DEPTH_BIT, 1);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.maxLod = 1.0f;
        vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler_);

        registerTextureInBindless("shadow_map", shadowMapView_, shadowSampler_);

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
        barrier.subresourceRange.layerCount = 1;

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
        VkShaderModule vertModule = vk::createShaderModule(device, vertCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "vertexMain";

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

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(Matrix4);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowPipelineLayout_);

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 1;
        pipelineInfo.pStages = &vertStage;
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
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
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
        pushRange.size = 128;

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
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
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
        pushRange.size = 128;

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

        // Set 1: GBuffer textures
        VkDescriptorSetLayoutBinding gbufferBindings[4] = {};
        for (int i = 0; i < 4; i++) {
            gbufferBindings[i].binding = i;
            gbufferBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbufferBindings[i].descriptorCount = 1;
            gbufferBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo dslInfo{};
        dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslInfo.bindingCount = 4;
        dslInfo.pBindings = gbufferBindings;
        vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &deferredDescriptorSetLayout_);

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * 2};
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

            VkWriteDescriptorSet writes[4] = {};
            VkDescriptorImageInfo imageInfos[4] = {};
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
            vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
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
    std::unique_ptr<VulkanSwapchain> swapchain_;

    std::vector<std::shared_ptr<IRenderFeature>> features_;
    std::vector<std::unique_ptr<IRenderPass>> passes_;


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
    uint32_t nextTextureIndex_ = 0;
    std::unordered_map<std::string, std::string> normalMapLinks_;

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

    LightData storedLights_[kMaxLights] = {};
    uint32_t storedLightCount_ = 0;

    static constexpr uint32_t kShadowMapSize = 2048;
    VkImage shadowMapImage_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowMapMemory_ = VK_NULL_HANDLE;
    VkImageView shadowMapView_ = VK_NULL_HANDLE;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
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
Renderer::Renderer(Platform* platform, MaterialManager* materialManager, std::shared_ptr<VulkanContext> context)
    : impl_(std::make_unique<RendererImpl>(platform, materialManager, context)) {}
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
void Renderer::loadTexture(const std::string& n, const std::string& p, bool m) { impl_->loadTexture(n, p, m); }
void Renderer::linkNormalMap(const std::string& d, const std::string& n) { impl_->linkNormalMap(d, n); }
void Renderer::setClearColor(float r, float g, float b, float a) { impl_->clearColor_ = {{r,g,b,a}}; }
void Renderer::setSynchronizationMode(SynchronizationMode mode) { impl_->setSynchronizationMode(mode); }
SynchronizationMode Renderer::synchronizationMode() const { return impl_->synchronizationMode(); }
void Renderer::setPresentMode(PresentMode mode) { impl_->setPresentMode(mode); }
PresentMode Renderer::presentMode() const { return impl_->presentMode(); }
void Renderer::setLightDirection(float x, float y, float z, float ambient) { impl_->setLightDirection(x, y, z, ambient); }
void Renderer::setLightDirection(const std::array<float, 4>& dir) { impl_->setLightDirection(dir); }
void Renderer::setShadowsEnabled(bool enabled) { impl_->setShadowsEnabled(enabled); }
bool Renderer::shadowsEnabled() const { return impl_->shadowsEnabled(); }
void Renderer::setLightColor(float r, float g, float b) { impl_->setLightColor(r, g, b); }
void Renderer::setLightingEnabled(bool e) { impl_->setLightingEnabled(e); }
bool Renderer::isLightingEnabled() const { return impl_->isLightingEnabled(); }
void Renderer::setAmbientStrength(float s) { impl_->setAmbientStrength(s); }
void Renderer::setAmbientColor(float r, float g, float b) { impl_->setAmbientColor(r, g, b); }
void Renderer::clearLights() { impl_->clearLights(); }
uint32_t Renderer::addDirectionalLight(float dx, float dy, float dz, float r, float g, float b, float intensity) {
    return impl_->addDirectionalLight(dx, dy, dz, r, g, b, intensity);
}
uint32_t Renderer::addPointLight(float px, float py, float pz, float r, float g, float b, float intensity, float range) {
    return impl_->addPointLight(px, py, pz, r, g, b, intensity, range);
}
uint32_t Renderer::addSpotLight(float px, float py, float pz, float dx, float dy, float dz,
                                float r, float g, float b, float intensity, float range,
                                float innerAngle, float outerAngle) {
    return impl_->addSpotLight(px, py, pz, dx, dy, dz, r, g, b, intensity, range, innerAngle, outerAngle);
}

uint32_t Renderer::getTextureIndex(const std::string& name) const { return impl_->getTextureIndex(name); }
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

} // namespace engine3d
