#include "render/renderer_impl.h"

namespace engine3d {

TextureHandle Renderer::RendererImpl::loadTexture(const std::string& name, const std::string& path, bool mirroredRepeat, bool disableAlpha) {
    TextureUploadData uploadData = TextureAssetLoader::load(path);
    return uploadTexture(name, uploadData, mirroredRepeat, disableAlpha);
}

TextureHandle Renderer::RendererImpl::loadTextureFromMemory(const std::string& name, const std::vector<float>& data, uint32_t width, uint32_t height, bool mirroredRepeat) {
    // Dane 0..255 (mapa wysokości, maski) -> 16-bit UNORM z zaokrągleniem. Wcześniej było 8 bit z obcięciem:
    // teren przesuwany na GPU leżał w "schodkach" do 1 jednostki mapy niżej niż siatka CPU, która rzuca cienie
    // - siatka cieni wystawała ponad widoczny teren i rzucała pasy cieni wzdłuż schodków.
    TextureUploadData uploadData{};
    uploadData.width = width;
    uploadData.height = height;
    uploadData.mipLevels = 1;   // pełny łańcuch mipmap wygeneruje uploadTexture
    uploadData.format = VK_FORMAT_R16_UNORM;
    uploadData.pixelBytes.resize(static_cast<size_t>(width) * height * 2);
    for (uint32_t i = 0; i < width * height; ++i) {
        const float val = std::clamp(data[i], 0.0f, 255.0f);
        const auto v = static_cast<uint16_t>(std::lround(val / 255.0f * 65535.0f));
        uploadData.pixelBytes[i * 2] = static_cast<uint8_t>(v & 0xFF);
        uploadData.pixelBytes[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
    }
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    uploadData.copyRegions.push_back(region);
    return uploadTexture(name, uploadData, mirroredRepeat, false);
}

// Wspólna część wczytywania tekstur (z pliku i z pamięci): obraz GPU, wgranie pikseli przez bufor pośredni,
// mipmapy (gotowe z pliku DDS albo generowane blitami), widok, sampler i miejsce w tablicy bindless.
TextureHandle Renderer::RendererImpl::uploadTexture(const std::string& name, TextureUploadData& uploadData,
                                                    bool mirroredRepeat, bool disableAlpha) {
    VkDevice device = vulkanContext_->device();
    VkPhysicalDevice physicalDevice = vulkanContext_->physicalDevice();

    // Przeładowanie pod tą samą nazwą: stara tekstura może być używana przez klatki w locie - niszczymy ją
    // dopiero po nich (retireTexture), zamiast zatrzymywać GPU (vkDeviceWaitIdle) przy każdym przeładowaniu.
    auto oldIt = textures_.find(name);
    if (oldIt != textures_.end()) {
        retireTexture(oldIt->second.image, oldIt->second.imageMemory, oldIt->second.imageView, oldIt->second.sampler);
        textures_.erase(oldIt);
    }

    // Plik bez gotowych mipmap (PNG, BMP, dane z pamięci) - pełny łańcuch liczymy na GPU.
    const bool needsMipmapGen = (uploadData.mipLevels <= 1);
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

    if (needsMipmapGen) vk::generateMipmaps(cmd, tex.image, uploadData.width, uploadData.height, uploadData.mipLevels);

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
            throw std::runtime_error("nie udało się utworzyć widoku obrazu (bez kanału alfa)");
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
    LOG_INFO("Wczytano teksturę: {} (indeks {}, poziomów mip: {})", name, textureNameToIndex_[name], uploadData.mipLevels);

    if (assetRegistry_) {
        return assetRegistry_->registerTexture(name);
    }
    return {};
}

void Renderer::RendererImpl::registerTextureInBindless(const std::string& name, VkImageView imageView, VkSampler sampler,
                                                      VkImageLayout layout) {
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
    imageInfo.imageLayout = layout;
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

// Tekstura 1x1 w jednym kolorze (domyślne: biała, płaska normalna, ORM, czarna). Piksel trafia do obrazu
// z CPU (hostImageCopy, Vulkan 1.4) - bez bufora pośredniego i bez bufora poleceń.
Renderer::RendererImpl::SolidTexture Renderer::RendererImpl::createSolidTexture(const uint8_t rgba[4], VkSampler sampler) {
    VkDevice device = vulkanContext_->device();
    SolidTexture t;
    vk::createImage(device, vulkanContext_->physicalDevice(), 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, t.image, t.memory);
    vk::hostTransitionToCopyLayout(device, t.image);
    vk::hostCopyToImage(device, t.image, rgba, 1, 1);
    t.view = vk::createImageView(device, t.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    t.sampler = sampler;
    return t;
}

void Renderer::RendererImpl::destroySolidTexture(SolidTexture& t) {
    VkDevice device = vulkanContext_->device();
    if (t.sampler) vkDestroySampler(device, t.sampler, nullptr);
    if (t.view) vkDestroyImageView(device, t.view, nullptr);
    if (t.image) vkDestroyImage(device, t.image, nullptr);
    if (t.memory) vkFreeMemory(device, t.memory, nullptr);
    t = {};
}

void Renderer::RendererImpl::createDefaultTextures() {
    VkDevice device = vulkanContext_->device();
    const uint8_t white[4] = {255, 255, 255, 255};
    const uint8_t flatNormal[4] = {128, 128, 255, 255};   // normalna (0,0,1) w przestrzeni stycznej
    const uint8_t orm[4] = {255, 255, 0, 255};            // R=occlusion 1, G=roughness 1, B=metallic 0
    const uint8_t black[4] = {0, 0, 0, 0};
    defaultTex_ = createSolidTexture(white, vk::createSampler(device, vulkanContext_->physicalDevice(), 1,
                                                              textureFilter_, AddressMode::Repeat));
    defaultNormalTex_ = createSolidTexture(flatNormal, vk::createClampSampler(device));
    defaultOrmTex_ = createSolidTexture(orm, vk::createClampSampler(device));
    defaultBlackTex_ = createSolidTexture(black, vk::createClampSampler(device));

    const VkImageLayout layout = vk::hostCopyLayout();
    registerTextureInBindless("default", defaultTex_.view, defaultTex_.sampler, layout);
    registerTextureInBindless("default_normal", defaultNormalTex_.view, defaultNormalTex_.sampler, layout);
    registerTextureInBindless("default_orm", defaultOrmTex_.view, defaultOrmTex_.sampler, layout);
    registerTextureInBindless("default_black", defaultBlackTex_.view, defaultBlackTex_.sampler, layout);
}

} // namespace engine3d
