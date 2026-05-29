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

class Renderer::RendererImpl {
public:
    RendererImpl(Platform* platform, MaterialManager* materialManager, std::shared_ptr<VulkanContext> context)
        : platform_(platform), materialManager_(materialManager), vulkanContext_(context) {}

    ~RendererImpl() { cleanup(); }

    bool initialize(const std::string& appName, bool enableValidationLayers) {
        if (!vulkanContext_) return false;
        try {
            swapchain_ = std::make_unique<VulkanSwapchain>(vulkanContext_, platform_->width(), platform_->height());

            createUniformBuffers();
            createDefaultTexture();
            createDefaultNormalTexture();
            createDescriptorResources();

            passes_.push_back(std::make_unique<OpaquePass>());
            passes_.push_back(std::make_unique<TransparentPass>());

            createSyncObjects();
            createCommandBuffers();
            std::cout << "Modern Renderer initialized with Swapchain." << std::endl;
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
        vkWaitForFences(device, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        try {
            imageIndex = swapchain_->acquireNextImage(imageAvailableSemaphores_[currentFrame_]);
        } catch (const std::runtime_error&) {
            recreateSwapChain();
            currentFrame_ = (currentFrame_ + 1) % kMaxFrames;
            return;
        }
        vkResetFences(device, 1, &inFlightFences_[currentFrame_]);

        updateUniformBuffer(view);

        VkCommandBuffer cmd = commandBuffers_[currentFrame_];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkImage colorImage = swapchain_->handle() ? swapchain_->images()[imageIndex] : VK_NULL_HANDLE; // Need to expose images
        
        // Manual transition for color image: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
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

        auto getTextureDescSet = [this](const std::string& textureName, uint32_t frameIndex) -> VkDescriptorSet {
            auto it = textureDescriptorSets_.find(textureName);
            return it != textureDescriptorSets_.end() ? it->second[frameIndex] : descriptorSets_[frameIndex];
        };

        // Before Opaque Features
        executeFeatures(cmd, view, IRenderFeature::Stage::BeforeOpaque);

        for (auto& pass : passes_) {
            if (pass->getName() == "OpaquePass") {
                pass->execute(cmd, currentFrame_, scene, pipelineLayout_, getTextureDescSet);
            }
        }

        // After Opaque Features
        executeFeatures(cmd, view, IRenderFeature::Stage::AfterOpaque);

        // After Transparent Features (ocean first, before sprites/particles)
        executeFeatures(cmd, view, IRenderFeature::Stage::AfterTransparent);

        for (auto& pass : passes_) {
            if (pass->getName() == "TransparentPass") {
                pass->execute(cmd, currentFrame_, scene, pipelineLayout_, getTextureDescSet);
            }
        }

        // Overlay Features
        executeFeatures(cmd, view, IRenderFeature::Stage::Overlay);

        vkCmdEndRendering(cmd);

        // Transition back to PRESENT_SRC_KHR
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
        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        vkQueueSubmit(vulkanContext_->graphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]);

        if (!swapchain_->present(imageIndex, renderFinishedSemaphores_[currentFrame_]))
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
        createPerTextureDescriptorSets(name, tex);
        std::cout << "Loaded texture: " << name << " (" << uploadData.width << "x" << uploadData.height << ")" << std::endl;
    }

    void linkNormalMap(const std::string& diffuseName, const std::string& normalName) {
        normalMapLinks_[diffuseName] = normalName;
        auto diffIt = textureDescriptorSets_.find(diffuseName);
        auto normIt = textures_.find(normalName);
        if (diffIt == textureDescriptorSets_.end() || normIt == textures_.end()) return;
        VkDevice device = vulkanContext_->device();
        VkDescriptorImageInfo normalInfo{};
        normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalInfo.imageView = normIt->second.imageView;
        normalInfo.sampler = normIt->second.sampler;
        for (int i = 0; i < kMaxFrames; i++) {
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = diffIt->second[i];
            w.dstBinding = 2;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.descriptorCount = 1;
            w.pImageInfo = &normalInfo;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }
    }

    void allocateDescriptorSet(const std::string& name) {
        VkDevice device = vulkanContext_->device();
        std::array<VkDescriptorSet, kMaxFrames> sets;
        std::vector<VkDescriptorSetLayout> layouts(kMaxFrames, descriptorSetLayout_);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = kMaxFrames;
        allocInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &allocInfo, sets.data()) != VK_SUCCESS) {
            std::cerr << "[Renderer] Failed to allocate descriptor set: " << name << std::endl;
            return;
        }
        VkDescriptorImageInfo defImg{};
        defImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        defImg.imageView = defaultImageView_;
        defImg.sampler = defaultSampler_;
        VkDescriptorImageInfo defNorm{};
        defNorm.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        defNorm.imageView = defaultNormalImageView_;
        defNorm.sampler = defaultNormalSampler_;
        for (int i = 0; i < kMaxFrames; i++) {
            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = uniformBuffers_[i];
            bufInfo.offset = 0;
            bufInfo.range = sizeof(GlobalUBO);
            VkWriteDescriptorSet w[3] = {};
            w[0].sType = w[1].sType = w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = w[1].dstSet = w[2].dstSet = sets[i];
            w[0].dstBinding = 0;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[0].descriptorCount = 1;
            w[0].pBufferInfo = &bufInfo;
            w[1].dstBinding = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].descriptorCount = 1;
            w[1].pImageInfo = &defImg;
            w[2].dstBinding = 2;
            w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[2].descriptorCount = 1;
            w[2].pImageInfo = &defNorm;
            vkUpdateDescriptorSets(device, 3, w, 0, nullptr);
        }
        textureDescriptorSets_[name] = sets;
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
    VkDescriptorSet getTexDescSet(const std::string& n, uint32_t f) const {
        auto it = textureDescriptorSets_.find(n);
        return it != textureDescriptorSets_.end() ? it->second[f] : descriptorSets_[f];
    }
    VkBuffer getUniformBuffer(uint32_t frame) const {
        return frame < uniformBuffers_.size() ? uniformBuffers_[frame] : VK_NULL_HANDLE;
    }
    bool getTextureInfo(const std::string& name, VkImageView& v, VkSampler& s) const {
        auto it = textures_.find(name);
        if (it == textures_.end()) return false;
        v = it->second.imageView;
        s = it->second.sampler;
        return v != VK_NULL_HANDLE && s != VK_NULL_HANDLE;
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
        textures_.clear(); textureDescriptorSets_.clear();
        for (int i = 0; i < kMaxFrames; i++) {
            vkDestroySemaphore(device, renderFinishedSemaphores_[i], nullptr);
            vkDestroySemaphore(device, imageAvailableSemaphores_[i], nullptr);
            vkDestroyFence(device, inFlightFences_[i], nullptr);
            if (uniformBuffersMapped_[i]) vkUnmapMemory(device, uniformBuffersMemory_[i]);
            if (uniformBuffers_[i]) { vkDestroyBuffer(device, uniformBuffers_[i], nullptr);
                vkFreeMemory(device, uniformBuffersMemory_[i], nullptr); }
        }
        if (descriptorPool_) vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
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
        defaultSampler_ = vk::createClampSampler(device);
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
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxDescriptorSets * 2}
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = kMaxDescriptorSets;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }

        VkDescriptorSetLayoutBinding bindings[3] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout");
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(Matrix4) + sizeof(std::array<float,4>) * 2 + sizeof(float) * 4;

        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &descriptorSetLayout_;
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

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalUBO);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = defaultImageView_;
        imageInfo.sampler = defaultSampler_;

        VkDescriptorImageInfo normalInfo{};
        normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalInfo.imageView = defaultNormalImageView_;
        normalInfo.sampler = defaultNormalSampler_;

        for (int i = 0; i < kMaxFrames; i++) {
            bufferInfo.buffer = uniformBuffers_[i];
            VkWriteDescriptorSet w[3] = {};
            w[0].sType = w[1].sType = w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = w[1].dstSet = w[2].dstSet = descriptorSets_[i];
            w[0].dstBinding = 0;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[0].descriptorCount = 1;
            w[0].pBufferInfo = &bufferInfo;
            w[1].dstBinding = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].descriptorCount = 1;
            w[1].pImageInfo = &imageInfo;
            w[2].dstBinding = 2;
            w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[2].descriptorCount = 1;
            w[2].pImageInfo = &normalInfo;
            vkUpdateDescriptorSets(device, 3, w, 0, nullptr);
        }
    }

    void createPerTextureDescriptorSets(const std::string& name, const TextureResources& tex) {
        VkDevice device = vulkanContext_->device();
        std::array<VkDescriptorSet, kMaxFrames> descriptorSets_;
        std::vector<VkDescriptorSetLayout> layouts(kMaxFrames, descriptorSetLayout_);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = kMaxFrames;
        allocInfo.pSetLayouts = layouts.data();
        vkAllocateDescriptorSets(device, &allocInfo, descriptorSets_.data());
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = tex.imageView;
        imageInfo.sampler = tex.sampler;
        VkDescriptorImageInfo normalInfo{};
        normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalInfo.imageView = defaultNormalImageView_;
        normalInfo.sampler = defaultNormalSampler_;
        auto it = normalMapLinks_.find(name);
        if (it != normalMapLinks_.end()) {
            auto nIt = textures_.find(it->second);
            if (nIt != textures_.end()) {
                normalInfo.imageView = nIt->second.imageView;
                normalInfo.sampler = nIt->second.sampler;
            }
        }
        for (int i = 0; i < kMaxFrames; i++) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers_[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(GlobalUBO);
            VkWriteDescriptorSet writes[3] = {};
            writes[0].sType = writes[1].sType = writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = writes[1].dstSet = writes[2].dstSet = descriptorSets_[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &bufferInfo;
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &imageInfo;
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].descriptorCount = 1;
            writes[2].pImageInfo = &normalInfo;
            vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
        }
        textureDescriptorSets_[name] = descriptorSets_;
        normalMapLinks_[name] = it != normalMapLinks_.end() ? it->second : "";
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
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < kMaxFrames; i++) {
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]);
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]);
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences_[i]);
        }
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
    std::unordered_map<std::string, std::array<VkDescriptorSet, kMaxFrames>> textureDescriptorSets_;
    std::unordered_map<std::string, std::string> normalMapLinks_;

    std::array<VkSemaphore, 2> imageAvailableSemaphores_;
    std::array<VkSemaphore, 2> renderFinishedSemaphores_;
    std::array<VkFence, 2> inFlightFences_;
    std::vector<VkCommandBuffer> commandBuffers_;
    uint32_t currentFrame_ = 0;
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
void Renderer::allocateDescriptorSet(const std::string& n) { impl_->allocateDescriptorSet(n); }
void Renderer::setClearColor(float r, float g, float b, float a) { impl_->clearColor_ = {{r,g,b,a}}; }

VkDevice Renderer::device() const { return impl_->device(); }
VkPhysicalDevice Renderer::physicalDevice() const { return impl_->physicalDevice(); }

VkDescriptorSetLayout Renderer::descriptorSetLayout() const { return impl_->descriptorSetLayout(); }
VkCommandPool Renderer::commandPool() const { return impl_->commandPool(); }
VkQueue Renderer::graphicsQueue() const { return impl_->graphicsQueue(); }
VkExtent2D Renderer::extent() const { return impl_->extent(); }
VkDescriptorSet Renderer::getTextureDescriptorSet(const std::string& n, uint32_t f) const {
    return impl_->getTexDescSet(n, f);
}
VkBuffer Renderer::getUniformBuffer(uint32_t f) const {
    return impl_->getUniformBuffer(f);
}
bool Renderer::getTextureInfo(const std::string& n, VkImageView& v, VkSampler& s) const {
    return impl_->getTextureInfo(n, v, s);
}

} // namespace engine3d
