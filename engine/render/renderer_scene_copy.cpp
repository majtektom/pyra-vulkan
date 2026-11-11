#include "render/renderer_impl.h"

namespace engine3d {

// Kopia koloru i głębokości sceny do tekstur (ocean czyta je do odbić/załamań), przejście głębokości w tryb odczytu.
void Renderer::RendererImpl::copySceneColor(VkCommandBuffer cmd, uint32_t imageIndex) {
    bool sceneColorNeeded = false;
    for (const auto& f : features_) sceneColorNeeded |= f->isActive() && f->needsSceneColor();
    if (!sceneColorNeeded) {
        // Nikt nie czyta kopii koloru (brak oceanu) - bez kopii. Część 2 wczytuje obraz (loadOp = LOAD),
        // więc zapisy części 1 muszą być dla niej widoczne.
        vk::imageBarrier(cmd, {.image = swapchain_->images()[imageIndex],
                               .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               .srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                               .dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT});
    } else {
        copySceneColorImage(cmd, imageIndex);
    }
    transitionDepthForSampling(cmd, imageIndex);
}

// Kopia obrazu ekranu do tekstury koloru sceny (ocean: odbicia i załamanie).
void Renderer::RendererImpl::copySceneColorImage(VkCommandBuffer cmd, uint32_t imageIndex) {
    // Przed kopią: obraz ekranu (po rysowaniu) -> źródło kopii, kopia koloru sceny (czytana w poprzedniej klatce przez
    // ocean) -> cel kopii.
    const vk::ImageBarrier beforeCopy[2] = {
        {.image = swapchain_->images()[imageIndex],
         .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
         .dstStage = VK_PIPELINE_STAGE_2_COPY_BIT, .dstAccess = VK_ACCESS_2_TRANSFER_READ_BIT},
        {.image = sceneColorImages_[currentFrame_],
         .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .srcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .srcAccess = VK_ACCESS_2_NONE,
         .dstStage = VK_PIPELINE_STAGE_2_COPY_BIT, .dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT},
    };
    vk::imageBarriers(cmd, beforeCopy, 2);

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

    // Po kopii: kopia koloru -> odczyt w shaderze (ocean), obraz ekranu -> znowu cel rysowania (część 2 wczytuje go
    // przez loadOp = LOAD i blenduje, więc potrzebny jest też odczyt).
    const vk::ImageBarrier afterCopy[2] = {
        {.image = sceneColorImages_[currentFrame_],
         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         .srcStage = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
         .dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT},
        {.image = swapchain_->images()[imageIndex],
         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .srcStage = VK_PIPELINE_STAGE_2_COPY_BIT, .srcAccess = VK_ACCESS_2_NONE,
         .dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT},
    };
    vk::imageBarriers(cmd, afterCopy, 2);

    // Update bindless descriptors for this frame
    registerSceneColorForFrame(currentFrame_);
}

// Głębokość: ATTACHMENT -> READ_ONLY (część 2 próbkuje ją w shaderach i dalej testuje głębokość).
void Renderer::RendererImpl::transitionDepthForSampling(VkCommandBuffer cmd, uint32_t imageIndex) {
    // Głębia jest w części 2 próbkowana przez shadery (ocean) ORAZ używana do testu głębokości (loadOp = LOAD).
    vk::imageBarrier(cmd, {.image = swapchain_->depthImage(imageIndex),
                           .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                           .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                           .srcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                           .srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           .dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                           .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                           .aspect = VK_IMAGE_ASPECT_DEPTH_BIT});

    registerSceneDepthForFrame(currentFrame_, imageIndex);
    registerGBufferWorldPosForFrame(currentFrame_);
}

void Renderer::RendererImpl::createSceneColorResources() {
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

void Renderer::RendererImpl::registerSceneColorForFrame(uint32_t frameIndex) {
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

void Renderer::RendererImpl::destroySceneColorResources() {
    VkDevice device = vulkanContext_->device();
    for (int f = 0; f < kMaxFrames; f++) {
        if (sceneColorViews_[f]) { vkDestroyImageView(device, sceneColorViews_[f], nullptr); sceneColorViews_[f] = VK_NULL_HANDLE; }
        if (sceneColorImages_[f]) { vkDestroyImage(device, sceneColorImages_[f], nullptr); sceneColorImages_[f] = VK_NULL_HANDLE; }
        if (sceneColorMemories_[f]) { vkFreeMemory(device, sceneColorMemories_[f], nullptr); sceneColorMemories_[f] = VK_NULL_HANDLE; }
    }
    if (sceneColorSampler_) { vkDestroySampler(device, sceneColorSampler_, nullptr); sceneColorSampler_ = VK_NULL_HANDLE; }
}

void Renderer::RendererImpl::registerSceneDepthForFrame(uint32_t frameIndex, uint32_t imageIndex) {
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

void Renderer::RendererImpl::registerGBufferWorldPosForFrame(uint32_t frameIndex) {
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

} // namespace engine3d
