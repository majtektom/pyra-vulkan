#include "render/renderer_impl.h"

namespace engine3d {

// Kopia koloru i głębokości sceny do tekstur (ocean czyta je do odbić/załamań), przejście głębokości w tryb odczytu.
void Renderer::RendererImpl::copySceneColor(VkCommandBuffer cmd, uint32_t imageIndex) {
    bool sceneColorNeeded = false;
    for (const auto& f : features_) sceneColorNeeded |= f->isActive() && f->needsSceneColor();
    if (!sceneColorNeeded) {
        // Nikt nie czyta kopii koloru (brak oceanu) - bez kopii. Część 2 wczytuje obraz (loadOp = LOAD),
        // więc zapisy części 1 muszą być dla niej widoczne.
        VkImageMemoryBarrier colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.image = swapchain_->images()[imageIndex];
        colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorBarrier.subresourceRange.levelCount = 1;
        colorBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &colorBarrier);
    } else {
        copySceneColorImage(cmd, imageIndex);
    }
    transitionDepthForSampling(cmd, imageIndex);
}

// Kopia obrazu ekranu do tekstury koloru sceny (ocean: odbicia i załamanie).
void Renderer::RendererImpl::copySceneColorImage(VkCommandBuffer cmd, uint32_t imageIndex) {
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
}

// Głębokość: ATTACHMENT -> READ_ONLY (część 2 próbkuje ją w shaderach i dalej testuje głębokość).
void Renderer::RendererImpl::transitionDepthForSampling(VkCommandBuffer cmd, uint32_t imageIndex) {
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
