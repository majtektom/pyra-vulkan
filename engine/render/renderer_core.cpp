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

struct GlobalUBO {
    Matrix4 view;
    Matrix4 proj;
    float camRight[4];
    float camUp[4];
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
            createDescriptorResources();

            // Register default textures
            registerTextureInBindless("default", defaultImageView_, defaultSampler_);
            registerTextureInBindless("default_normal", defaultNormalImageView_, defaultNormalSampler_);

            passes_.push_back(std::make_unique<OpaquePass>());
            passes_.push_back(std::make_unique<TransparentPass>());

            createSyncObjects();
            createCommandBuffers();
            std::cout << "Modern Bindless Renderer initialized." << std::endl;
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

        VkImage colorImage = swapchain_->handle() ? swapchain_->images()[imageIndex] : VK_NULL_HANDLE;
        
        VkImageMemoryBarrier colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.srcAccessMask = 0;
        colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.image = swapchain_->handle() ? swapchain_->images()[imageIndex] : VK_NULL_HANDLE;
        colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorBarrier.subresourceRange.baseMipLevel = 0;
        colorBarrier.subresourceRange.levelCount = 1;
        colorBarrier.subresourceRange.baseArrayLayer = 0;
        colorBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &colorBarrier);

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = swapchain_->imageView(imageIndex);
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = swapchain_->depthImageView(imageIndex);
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, swapchain_->extent()};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);

        VkViewport viewport{0.0f, 0.0f,
            static_cast<float>(swapchain_->extent().width),
            static_cast<float>(swapchain_->extent().height),
            0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, swapchain_->extent()};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Bind global bindless descriptor set once
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);

        auto getTextureIndex = [this](const std::string& name) -> uint32_t {
            auto it = textureNameToIndex_.find(name);
            return it != textureNameToIndex_.end() ? it->second : 0; // 0 is default texture
        };

        // Before Opaque Features
        executeFeatures(cmd, view, IRenderFeature::Stage::BeforeOpaque);

        for (auto& pass : passes_) {
            if (pass->getName() == "OpaquePass") {
                pass->execute(cmd, currentFrame_, scene, pipelineLayout_);
            }
        }

        // After Opaque Features
        executeFeatures(cmd, view, IRenderFeature::Stage::AfterOpaque);

        // After Transparent Features
        executeFeatures(cmd, view, IRenderFeature::Stage::AfterTransparent);

        for (auto& pass : passes_) {
            if (pass->getName() == "TransparentPass") {
                pass->execute(cmd, currentFrame_, scene, pipelineLayout_);
            }
        }

        // Overlay Features
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
        presentBarrier.subresourceRange.baseMipLevel = 0;
        presentBarrier.subresourceRange.levelCount = 1;
        presentBarrier.subresourceRange.baseArrayLayer = 0;
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

    void cleanup() {
        if (!vulkanContext_ || !vulkanContext_->device()) return;
        VkDevice device = vulkanContext_->device();
        vkDeviceWaitIdle(device);
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
        swapchain_->recreate(platform_->width(), platform_->height());
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

uint32_t Renderer::getTextureIndex(const std::string& name) const { return impl_->getTextureIndex(name); }
void Renderer::prepareRenderPacket(RenderPacket& packet) const {
    packet.diffuseTextureIndex = impl_->getTextureIndex(packet.textureName);
    packet.normalTextureIndex = impl_->getNormalIndex(packet.textureName);
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
