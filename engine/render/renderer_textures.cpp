#include "render/renderer_impl.h"

namespace engine3d {

TextureHandle Renderer::RendererImpl::loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat, bool disableAlpha) {
    VkDevice device = vulkanContext_->device();
    
    // Clean up existing texture with the same name to prevent memory leak on reload
    auto oldIt = textures_.find(name);
    if (oldIt != textures_.end()) {
        // Stara tekstura może być używana przez klatki w locie - niszczymy ją dopiero po nich (retireTexture),
        // zamiast zatrzymywać GPU (vkDeviceWaitIdle) przy każdym przeładowaniu.
        retireTexture(oldIt->second.image, oldIt->second.imageMemory, oldIt->second.imageView, oldIt->second.sampler);
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

TextureHandle Renderer::RendererImpl::loadTextureFromMemory(const std::string& name, const std::vector<float>& data, uint32_t width, uint32_t height, bool mirroredRepeat) {
    VkDevice device = vulkanContext_->device();
    
    // Clean up existing texture with the same name to prevent memory leak on reload
    auto oldIt = textures_.find(name);
    if (oldIt != textures_.end()) {
        // Stara tekstura może być używana przez klatki w locie - niszczymy ją dopiero po nich (retireTexture),
        // zamiast zatrzymywać GPU (vkDeviceWaitIdle) przy każdym przeładowaniu.
        retireTexture(oldIt->second.image, oldIt->second.imageMemory, oldIt->second.imageView, oldIt->second.sampler);
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

void Renderer::RendererImpl::registerTextureInBindless(const std::string& name, VkImageView imageView, VkSampler sampler) {
    VkDevice device = vulkanContext_->device();
    uint32_t index = 0;
    auto it = textureNameToIndex_.find(name);
    const bool reused = it != textureNameToIndex_.end();
    if (!reused) {
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

    if (reused) {
        // Ten element tablicy mogą czytać klatki w locie - podmiana w secie klatki dopiero, gdy ta klatka skończy.
        queueDescriptorWrite(index, imageView, sampler);
        return;
    }
    // Nowy element: żadna klatka w locie go nie czyta (UPDATE_UNUSED_WHILE_PENDING) - zapis od razu.
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

void Renderer::RendererImpl::setTextureFilter(TextureFilter filter) {
    if (textureFilter_ == filter) return;
    textureFilter_ = filter;
    VkDevice device = vulkanContext_->device();
    // Nowe samplery od razu; stare używają jeszcze klatki w locie - trafiają do kolejki niszczenia, a deskryptory
    // każdej klatki podmieniamy, gdy ta klatka skończy (bez vkDeviceWaitIdle - bez zacięcia przy zmianie filtra).
    for (auto& [name, tex] : textures_) {
        retireTexture(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, tex.sampler);
        tex.sampler = vk::createSampler(device, vulkanContext_->physicalDevice(), tex.mipLevels, filter, tex.addressMode);
        queueDescriptorWrite(getTextureIndex(name), tex.imageView, tex.sampler);
    }
}

// Zasób tekstury do zniszczenia, gdy skończą wszystkie klatki, które mogły go używać.
void Renderer::RendererImpl::retireTexture(VkImage image, VkDeviceMemory memory, VkImageView view, VkSampler sampler) {
    retiredTextures_.push_back({image, memory, view, sampler, frameNumber_});
}

// Podmiana elementu tablicy bindless: w secie każdej klatki osobno, na początku jej nagrywania (applyDescriptorWrites).
void Renderer::RendererImpl::queueDescriptorWrite(uint32_t index, VkImageView view, VkSampler sampler) {
    for (auto& p : pendingDescriptorWrites_) {
        if (p.index == index) {   // nowsza podmiana tego samego elementu zastępuje starszą
            p.view = view;
            p.sampler = sampler;
            p.done.fill(false);
            return;
        }
    }
    pendingDescriptorWrites_.push_back({index, view, sampler, {}});
}

// Po czekaniu na fence klatki currentFrame_: jej set nie jest już używany przez GPU - wolno go zmieniać.
// Tu też niszczymy zasoby, których nie może już czytać żadna klatka w locie.
void Renderer::RendererImpl::applyDescriptorWrites() {
    VkDevice device = vulkanContext_->device();
    for (auto& p : pendingDescriptorWrites_) {
        if (p.done[currentFrame_]) continue;
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = p.view;
        imageInfo.sampler = p.sampler;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[currentFrame_];
        write.dstBinding = 1;
        write.dstArrayElement = p.index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        p.done[currentFrame_] = true;
    }
    pendingDescriptorWrites_.erase(std::remove_if(pendingDescriptorWrites_.begin(), pendingDescriptorWrites_.end(),
                                                  [](const PendingDescriptorWrite& p) {
                                                      for (bool d : p.done) if (!d) return false;
                                                      return true;
                                                  }),
                                   pendingDescriptorWrites_.end());
    // Zasób odłożony w klatce N mogły czytać klatki do N + kMaxFrames - 1 (w tym te, którym jeszcze nie
    // podmieniono deskryptora) - po ich fence'ach jest wolny.
    retiredTextures_.erase(std::remove_if(retiredTextures_.begin(), retiredTextures_.end(),
                                          [&](const RetiredTexture& r) {
                                              if (frameNumber_ < r.frame + kMaxFrames + 1) return false;
                                              destroyRetired(r);
                                              return true;
                                          }),
                           retiredTextures_.end());
}

void Renderer::RendererImpl::destroyRetired(const RetiredTexture& r) {
    VkDevice device = vulkanContext_->device();
    if (r.view) vkDestroyImageView(device, r.view, nullptr);
    if (r.image) vkDestroyImage(device, r.image, nullptr);
    if (r.memory) vkFreeMemory(device, r.memory, nullptr);
    if (r.sampler) vkDestroySampler(device, r.sampler, nullptr);
}

uint32_t Renderer::RendererImpl::getTextureIndex(TextureHandle handle) const {
    if (!handle.isValid()) {
        return 0;
    }
    if (handle.value < textureHandleToIndex_.size()) {
        return textureHandleToIndex_[handle.value];
    }
    return 0;
}

void Renderer::RendererImpl::prepareRenderPacket(RenderPacket& packet) {
    packet.forwardOpaquePass = false;
    packet.gbufferMaterialPass = false;
    const bool wantsLayeredForward = packet.layerBlendEnabled();
    if (materialManager_) {
        if (const Material* materialData = materialManager_->get(packet.materialHandle)) {
            const std::string& shaderProgram = materialData->shaderProgram;
            const bool usesCustomOpaqueShader =
                !shaderProgram.empty() &&
                shaderProgram != "default" &&
                shaderProgram != "gbuffer" &&
                shaderProgram != "gbuffer_skinned";
            if ((usesCustomOpaqueShader || wantsLayeredForward) && materialData->blendMode == BlendMode::Disabled) {
                // Własny shader piszący G-Buffer (teren) - przebieg G-Buffer; inny - przebieg forward.
                if (MaterialCompiler::writesGBuffer(shaderProgram)) {
                    packet.gbufferMaterialPass = true;
                } else {
                    packet.forwardOpaquePass = true;
                }
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
    // Indeksy tekstur terenu (packet.terrain) ustala resolveTerrainSurfaces - raz na teren, nie na paczkę.
}

// Indeksy bindless warstw i mapy wysokości terenu. Wywoływane na początku render(), na jednym wątku - paczki
// jednego terenu dzielą TerrainSurface, więc prepareRenderPacket (równolegle w wątkach) nie może jej zmieniać.
void Renderer::RendererImpl::resolveTerrainSurface(TerrainSurface& t) const {
    t.layerCount = std::min(t.layerCount, kMaxTerrainLayers);
    for (uint32_t i = 0; i < kMaxTerrainLayers; ++i) {
        t.layerTextureIndices[i] = defaultDiffuseIndex_;
        t.layerMaskIndices[i] = defaultMaskIndex_;
    }
    if (t.layerBlendEnabled) {
        for (uint32_t i = 0; i < t.layerCount; ++i) {
            if (t.layerTextureHandles[i].isValid()) t.layerTextureIndices[i] = getTextureIndex(t.layerTextureHandles[i]);
            if (i > 0 && t.layerMaskHandles[i].isValid()) t.layerMaskIndices[i] = getTextureIndex(t.layerMaskHandles[i]);
        }
    }
    t.heightmapTextureIndex = t.heightmapTextureHandle.isValid() ? getTextureIndex(t.heightmapTextureHandle) : 0;
}

void Renderer::RendererImpl::resolveTerrainSurfaces(const RenderScene& scene) {
    const TerrainSurface* last = nullptr;   // kolejne paczki zwykle mają ten sam teren
    scene.forEachPacket([&](const RenderPacket& p) {
        if (!p.terrain || p.terrain.get() == last) return;
        last = p.terrain.get();
        resolveTerrainSurface(*p.terrain);
    });
}

bool Renderer::RendererImpl::getTextureInfo(const std::string& name, VkImageView& v, VkSampler& s) const {
    auto it = textures_.find(name);
    if (it == textures_.end()) return false;
    v = it->second.imageView;
    s = it->second.sampler;
    return v != VK_NULL_HANDLE && s != VK_NULL_HANDLE;
}

bool Renderer::RendererImpl::getTextureInfo(TextureHandle handle, VkImageView& v, VkSampler& s) const {
    if (!handle.isValid() || !assetRegistry_) {
        return false;
    }

    const auto name = assetRegistry_->textureName(handle);
    if (!name.has_value()) {
        return false;
    }

    return getTextureInfo(*name, v, s);
}

void Renderer::RendererImpl::createDefaultTexture() {
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

void Renderer::RendererImpl::createDefaultNormalTexture() {
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

void Renderer::RendererImpl::createDefaultOrmTexture() {
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

void Renderer::RendererImpl::createDefaultBlackTexture() {
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

} // namespace engine3d
