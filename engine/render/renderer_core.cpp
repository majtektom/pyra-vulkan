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
#include "skybox_renderer.h"
#include "grid_renderer.h"
#include "text_overlay_renderer.h"
#include "font_manager.h"
#include "font_atlas_loader.h"
#include <cstring>
#include <iostream>
#include <array>
#include <sstream>
#include <unordered_map>

namespace engine3d {

constexpr int kMaxFrames = 2;
constexpr int kMaxDescriptorSets = 32;

struct GlobalUBO {
    Matrix4 view;
    Matrix4 proj;
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
            createDescriptorResources();

            passes_.push_back(std::make_unique<OpaquePass>());
            passes_.push_back(std::make_unique<TransparentPass>());

            createSyncObjects();
            createCommandBuffers();

            initTextOverlayRenderer();

            std::cout << "Modern Renderer initialized with Swapchain." << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Renderer Init Error: " << e.what() << std::endl;
            return false;
        }
    }

    void render(const RenderScene& scene, CameraComponent* camera) {
        if (!vulkanContext_ || !vulkanContext_->device()) return;

        if (platform_->wasResized()) {
            platform_->clearResizeFlag();
            recreateSwapChain();
        }

        if (!swapchain_ || !swapchain_->renderPass()) return;

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

        updateUniformBuffer(camera);

        VkCommandBuffer cmd = commandBuffers_[currentFrame_];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = swapchain_->renderPass();
        renderPassInfo.framebuffer = swapchain_->framebuffer(imageIndex);
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchain_->extent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{0.0f, 0.0f,
            static_cast<float>(swapchain_->extent().width),
            static_cast<float>(swapchain_->extent().height),
            0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, swapchain_->extent()};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        auto getTextureDescSet = [this](const std::string& textureName, uint32_t frameIndex) -> VkDescriptorSet {
            auto it = textureDescriptorSets_.find(textureName);
            if (it != textureDescriptorSets_.end()) {
                return it->second[frameIndex];
            }
            return descriptorSets_[frameIndex];
        };

        bool skyboxRendered = false;
        bool gridRendered = false;
        for (auto& pass : passes_) {
            if (pass->getName() == "TransparentPass") {
                if (skyboxEnabled_ && skyboxRenderer_.isReady() && camera) {
                    std::array<VkDescriptorSet, 3> faceSets = {};
                    auto getFaceSet = [this](const std::string& key) -> VkDescriptorSet {
                        auto it = textureDescriptorSets_.find(key);
                        if (it != textureDescriptorSets_.end()) {
                            return it->second[currentFrame_];
                        }
                        return VK_NULL_HANDLE;
                    };
                    faceSets[0] = getFaceSet("skybox_fb");
                    faceSets[1] = getFaceSet("skybox_lr");
                    faceSets[2] = getFaceSet("skybox_top");
                    skyboxRenderer_.recordDrawCommands(cmd, skyboxRenderer_.getPipelineLayout(), camera->position, faceSets);
                    skyboxRendered = true;
                }
                if (gridEnabled_ && gridRenderer_.isReady()) {
                    VkDescriptorSet gridSet = descriptorSets_[currentFrame_];
                    gridRenderer_.recordDrawCommands(cmd, gridRenderer_.getPipelineLayout(), gridSet, currentFrame_);
                    gridRendered = true;
                }
            }
            pass->execute(cmd, currentFrame_, scene, pipelineLayout_, getTextureDescSet);
        }

        if (!skyboxRendered && skyboxEnabled_ && skyboxRenderer_.isReady() && camera) {
            std::array<VkDescriptorSet, 3> faceSets = {};
            auto getFaceSet = [this](const std::string& key) -> VkDescriptorSet {
                auto it = textureDescriptorSets_.find(key);
                if (it != textureDescriptorSets_.end()) {
                    return it->second[currentFrame_];
                }
                return VK_NULL_HANDLE;
            };
            faceSets[0] = getFaceSet("skybox_fb");
            faceSets[1] = getFaceSet("skybox_lr");
            faceSets[2] = getFaceSet("skybox_top");
            skyboxRenderer_.recordDrawCommands(cmd, skyboxRenderer_.getPipelineLayout(), camera->position, faceSets);
        }

        if (!gridRendered && gridEnabled_ && gridRenderer_.isReady()) {
            VkDescriptorSet gridSet = descriptorSets_[currentFrame_];
            gridRenderer_.recordDrawCommands(cmd, gridRenderer_.getPipelineLayout(), gridSet, currentFrame_);
        }

        if (textOverlayEnabled_ && textOverlayRenderer_.isReady()) {
            textOverlayRenderer_.buildTexts();
            textOverlayRenderer_.recordDrawCommands(cmd);
            textOverlayRenderer_.clearTexts();
        }

        vkCmdEndRenderPass(cmd);
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

        if (!swapchain_->present(imageIndex, renderFinishedSemaphores_[currentFrame_])) {
            recreateSwapChain();
        }

        currentFrame_ = (currentFrame_ + 1) % kMaxFrames;
    }

    void recreateSwapChain() {
        VkDevice device = vulkanContext_->device();
        vkDeviceWaitIdle(device);
        swapchain_->recreate(platform_->width(), platform_->height());

        if (skyboxEnabled_) initSkyboxRenderer();
        if (gridEnabled_) initGridRenderer();
        if (textOverlayEnabled_) initTextOverlayRenderer();
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

        void* mapped;
        vkMapMemory(device, stagingMemory, 0, dataSize, 0, &mapped);
        std::memcpy(mapped, uploadData.pixelBytes.data(), dataSize);
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
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.layerCount = 1;
                blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.layerCount = 1;
                blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
                                      mipHeight > 1 ? mipHeight / 2 : 1, 1};
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

        if (!needsMipmapGen) {
            vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                      tex.image, uploadData.format,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      uploadData.mipLevels);
        }

        tex.imageView = vk::createImageView(device, tex.image, uploadData.format,
                                            VK_IMAGE_ASPECT_COLOR_BIT, uploadData.mipLevels);

        if (mirroredRepeat) {
            tex.sampler = vk::createMirrorRepeatSampler(device, uploadData.mipLevels);
        } else {
            tex.sampler = vk::createTextureSampler(device, uploadData.mipLevels);
        }

        textures_[name] = tex;
        createPerTextureDescriptorSets(name, tex);

        std::cout << "Loaded texture: " << name << " (" << uploadData.width << "x" << uploadData.height << ")" << std::endl;
    }

    void createPerTextureDescriptorSets(const std::string& name, const TextureResources& tex) {
        VkDevice device = vulkanContext_->device();

        std::array<VkDescriptorSet, kMaxFrames> descSets;
        std::vector<VkDescriptorSetLayout> layouts(kMaxFrames, descriptorSetLayout_);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = kMaxFrames;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(device, &allocInfo, descSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor sets for texture: " + name);
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = tex.imageView;
        imageInfo.sampler = tex.sampler;

        for (int i = 0; i < kMaxFrames; i++) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers_[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(GlobalUBO);

            VkWriteDescriptorSet writes[2] = {};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = descSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &bufferInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = descSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }

        textureDescriptorSets_[name] = descSets;
    }

    void cleanup() {
        if (vulkanContext_ && vulkanContext_->device()) {
            VkDevice device = vulkanContext_->device();
            vkDeviceWaitIdle(device);

            skyboxRenderer_.cleanup(device);
            gridRenderer_.cleanup(device);
            textOverlayRenderer_.cleanup();

            for (auto& [name, tex] : textures_) {
                if (tex.sampler != VK_NULL_HANDLE) vkDestroySampler(device, tex.sampler, nullptr);
                if (tex.imageView != VK_NULL_HANDLE) vkDestroyImageView(device, tex.imageView, nullptr);
                if (tex.image != VK_NULL_HANDLE) {
                    vkDestroyImage(device, tex.image, nullptr);
                    vkFreeMemory(device, tex.imageMemory, nullptr);
                }
            }
            textures_.clear();
            textureDescriptorSets_.clear();

            for (size_t i = 0; i < kMaxFrames; i++) {
                vkDestroySemaphore(device, renderFinishedSemaphores_[i], nullptr);
                vkDestroySemaphore(device, imageAvailableSemaphores_[i], nullptr);
                vkDestroyFence(device, inFlightFences_[i], nullptr);
                if (uniformBuffersMapped_[i]) {
                    vkUnmapMemory(device, uniformBuffersMemory_[i]);
                }
                if (uniformBuffers_[i] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, uniformBuffers_[i], nullptr);
                    vkFreeMemory(device, uniformBuffersMemory_[i], nullptr);
                }
            }
            if (descriptorPool_ != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
            }
            if (defaultSampler_ != VK_NULL_HANDLE) {
                vkDestroySampler(device, defaultSampler_, nullptr);
            }
            if (defaultImageView_ != VK_NULL_HANDLE) {
                vkDestroyImageView(device, defaultImageView_, nullptr);
            }
            if (defaultImage_ != VK_NULL_HANDLE) {
                vkDestroyImage(device, defaultImage_, nullptr);
                vkFreeMemory(device, defaultImageMemory_, nullptr);
            }
        }
    }

    void waitIdle() { if (vulkanContext_ && vulkanContext_->device()) vkDeviceWaitIdle(vulkanContext_->device()); }
    uint32_t width() const { return platform_->width(); }
    uint32_t height() const { return platform_->height(); }
    VkRenderPass getRenderPass() const { return swapchain_ ? swapchain_->renderPass() : VK_NULL_HANDLE; }

    void initSkyboxRenderer() {
        if (!vulkanContext_ || !swapchain_ || skyboxRenderer_.isReady()) return;
        SkyboxConfig skyboxConfig;
        skyboxConfig.enabled = true;
        skyboxConfig.directory = skyboxDirectory_;
        skyboxConfig.size = 3500.0f;
        skyboxConfig.verticalOffset = 100.0f;
        skyboxRenderer_.init(
            vulkanContext_->device(),
            vulkanContext_->physicalDevice(),
            swapchain_->renderPass(),
            descriptorSetLayout_,
            vulkanContext_->commandPool(),
            vulkanContext_->graphicsQueue(),
            swapchain_->extent(),
            skyboxConfig,
            "bin/assets/shaders/skybox"
        );
    }

    void initGridRenderer() {
        if (!vulkanContext_ || !swapchain_ || gridRenderer_.isReady()) return;
        GridConfig gridConfig;
        gridConfig.enabled = true;
        gridConfig.size = 200.0f;
        gridConfig.spacing = 5.0f;
        gridRenderer_.init(
            vulkanContext_->device(),
            vulkanContext_->physicalDevice(),
            swapchain_->renderPass(),
            descriptorSetLayout_,
            vulkanContext_->commandPool(),
            vulkanContext_->graphicsQueue(),
            swapchain_->extent(),
            gridConfig,
            "bin/assets/shaders/grid"
        );
    }

    void initTextOverlayRenderer() {
        if (!vulkanContext_ || !swapchain_ || textOverlayRenderer_.isReady()) return;
        textOverlayRenderer_.init(
            vulkanContext_->device(),
            vulkanContext_->physicalDevice(),
            swapchain_->renderPass(),
            vulkanContext_->commandPool(),
            vulkanContext_->graphicsQueue(),
            swapchain_->extent()
        );
        textOverlayEnabled_ = true;
    }

    void loadFont(const std::string& name, const std::string& path, uint32_t pixelHeight) {
        if (!fontManager_ || !textOverlayRenderer_.isPipelineReady()) return;
        FontAsset asset{name, path, pixelHeight};
        textOverlayRenderer_.loadFont(asset);
        fontManager_->add(asset);
    }

    void setTextLabel(const std::string& text, const std::string&, float x, float y, float scale,
                      const std::array<float, 4>& color) {
        if (!textOverlayRenderer_.isReady()) return;
        float sw = static_cast<float>(swapchain_->extent().width);
        float sh = static_cast<float>(swapchain_->extent().height);
        textOverlayRenderer_.addText(text, x, y, scale, color, sw, sh);
    }

    SkyboxRenderer skyboxRenderer_;
    GridRenderer gridRenderer_;
    TextOverlayRenderer textOverlayRenderer_;
    FontManager* fontManager_ = nullptr;
    bool skyboxEnabled_ = false;
    bool gridEnabled_ = false;
    bool textOverlayEnabled_ = false;
    std::string skyboxDirectory_;
    std::array<float, 4> clearColor_ = {{0.1f, 0.1f, 0.1f, 1.0f}};
    TextLabel textLabel_;

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

    void updateUniformBuffer(CameraComponent* camera) {
        GlobalUBO ubo{};
        if (camera) {
            ubo.view = camera->getViewMatrix();
            float aspect = static_cast<float>(swapchain_->extent().width) /
                           static_cast<float>(swapchain_->extent().height);
            ubo.proj = perspective(radians(camera->fieldOfViewDegrees), aspect,
                                   camera->nearPlane, camera->farPlane);
        }
        std::memcpy(uniformBuffersMapped_[currentFrame_], &ubo, sizeof(ubo));
    }

    void createDefaultTexture() {
        VkDevice device = vulkanContext_->device();
        VkPhysicalDevice physicalDevice = vulkanContext_->physicalDevice();

        // 1x1 white pixel
        uint32_t whitePixel = 0xFFFFFFFF;
        VkDeviceSize imageSize = 4;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        vk::createBuffer(device, physicalDevice, imageSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingBufferMemory);
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        std::memcpy(data, &whitePixel, imageSize);
        vkUnmapMemory(device, stagingBufferMemory);

        vk::createImage(device, physicalDevice, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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
        region.imageExtent = {1, 1, 1};
        vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, defaultImage_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endSingleTimeCommands(cmdBuffer);

        vk::transitionImageLayout(device, vulkanContext_->commandPool(),
                                  vulkanContext_->graphicsQueue(),
                                  defaultImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        defaultImageView_ = vk::createImageView(device, defaultImage_, VK_FORMAT_R8G8B8A8_UNORM,
                                                VK_IMAGE_ASPECT_COLOR_BIT, 1);
        defaultSampler_ = vk::createClampSampler(device);
    }

    void createDescriptorResources() {
        VkDevice device = vulkanContext_->device();

        // Pool with enough capacity for default sets + texture sets
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxDescriptorSets},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxDescriptorSets}
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = kMaxDescriptorSets;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }

        // Layout (same as material system)
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout");
        }

        // Pipeline layout with push constants
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(Matrix4) + sizeof(std::array<float, 4>);

        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &descriptorSetLayout_;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout");
        }

        // Per-frame descriptor sets
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

        // Write each descriptor set
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalUBO);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = defaultImageView_;
        imageInfo.sampler = defaultSampler_;

        for (int i = 0; i < kMaxFrames; i++) {
            bufferInfo.buffer = uniformBuffers_[i];
            VkWriteDescriptorSet writes[2] = {};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = descriptorSets_[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &bufferInfo;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = descriptorSets_[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }
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
        for (size_t i = 0; i < kMaxFrames; i++) {
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

    Platform* platform_;
    MaterialManager* materialManager_;
    std::shared_ptr<VulkanContext> vulkanContext_;
    std::unique_ptr<VulkanSwapchain> swapchain_;
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

    std::unordered_map<std::string, TextureResources> textures_;
    std::unordered_map<std::string, std::array<VkDescriptorSet, kMaxFrames>> textureDescriptorSets_;

    std::array<VkSemaphore, 2> imageAvailableSemaphores_;
    std::array<VkSemaphore, 2> renderFinishedSemaphores_;
    std::array<VkFence, 2> inFlightFences_;
    std::vector<VkCommandBuffer> commandBuffers_;
    uint32_t currentFrame_ = 0;
};

Renderer::Renderer(Platform* platform, MaterialManager* materialManager, std::shared_ptr<VulkanContext> context)
    : impl_(std::make_unique<RendererImpl>(platform, materialManager, context)) {}

Renderer::~Renderer() = default;

bool Renderer::initialize(const std::string& appName, bool enableValidationLayers) {
    return impl_->initialize(appName, enableValidationLayers);
}

void Renderer::render(const RenderScene& scene, CameraComponent* camera) {
    impl_->render(scene, camera);
}

void Renderer::cleanup() { impl_->cleanup(); }
void Renderer::waitIdle() { impl_->waitIdle(); }
uint32_t Renderer::width() const { return impl_->width(); }
uint32_t Renderer::height() const { return impl_->height(); }
VkRenderPass Renderer::getRenderPass() const { return impl_->getRenderPass(); }

// Facade methods
void Renderer::setCamera(CameraComponent*) {}
void Renderer::setShaderManager(ShaderManager*) {}
void Renderer::setTextureManager(TextureManager*) {}
void Renderer::setFontManager(FontManager* fm) { impl_->fontManager_ = fm; }
void Renderer::loadMesh(const std::string&, const MeshData&) {}
void Renderer::loadDynamicMesh(const std::string&, const MeshData&) {}
void Renderer::updateDynamicMesh(const std::string&, const MeshData&) {}
void Renderer::setDynamicMeshTexture(const std::string&, const std::string&) {}
void Renderer::loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat) {
    impl_->loadTexture(name, path, mirroredRepeat);
}
void Renderer::loadFont(const std::string& name, const std::string& path, uint32_t pixelHeight) {
    impl_->loadFont(name, path, pixelHeight);
}
void Renderer::loadShader(const std::string&, const ShaderProgram&) {}
void Renderer::setGridEnabled(bool enabled) {
    impl_->gridEnabled_ = enabled;
    impl_->gridRenderer_.setEnabled(enabled);
    if (enabled) impl_->initGridRenderer();
}
void Renderer::setSkyboxEnabled(bool enabled, const std::string& directory) {
    impl_->skyboxEnabled_ = enabled;
    impl_->skyboxDirectory_ = directory;
    impl_->skyboxRenderer_.setEnabled(enabled);
    if (enabled) impl_->initSkyboxRenderer();
}
void Renderer::setSkyboxExposure(float exposure) {
    impl_->skyboxRenderer_.setExposure(exposure);
}
void Renderer::setClearColor(float r, float g, float b, float a) {
    impl_->clearColor_ = {{r, g, b, a}};
}
void Renderer::setViewportSize(uint32_t, uint32_t) {}
void Renderer::setTextLabel(const std::string& text, const std::string& fontName,
                            float x, float y, float scale,
                            const std::array<float, 4>& color) {
    impl_->setTextLabel(text, fontName, x, y, scale, color);
}

} // namespace engine3d
