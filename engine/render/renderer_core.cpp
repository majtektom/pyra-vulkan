#include "render/renderer_impl.h"

namespace engine3d {

bool Renderer::RendererImpl::initialize(const std::string& appName, bool enableValidationLayers) {
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

void Renderer::RendererImpl::render(const RenderScene& scene, const RenderView& view) {
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
    ++frameNumber_;
    applyDescriptorWrites();   // set tej klatki jest wolny (fence) - zaległe podmiany tekstur

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
    resolveTerrainSurfaces(scene);
    if (shadowsEnabled_) updateVisibleDepthRange(scene, view);   // służy tylko do rozłożenia kaskad cieni
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

    if (shadowsEnabled_) recordShadowPass(cmd, scene);
    Profiler::get().lap("render.cienie (nagrywanie)");
    recordGBufferPass(cmd, scene, imageIndex);
    Profiler::get().lap("render.gbuffer (nagrywanie)");
    recordLightingPass(cmd, scene, view, imageIndex);
    copySceneColor(cmd, imageIndex);
    recordTransparentPass(cmd, scene, view, imageIndex);
    recordOverlayPass(cmd, view, imageIndex);

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

void Renderer::RendererImpl::cleanup() {
    if (cleanedUp_ || !vulkanContext_ || !vulkanContext_->device()) return;
    VkDevice device = vulkanContext_->device();
    vkDeviceWaitIdle(device);
    for (const auto& r : retiredTextures_) destroyRetired(r);   // GPU bezczynne - odłożone zasoby można zniszczyć
    retiredTextures_.clear();
    pendingDescriptorWrites_.clear();

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

void Renderer::RendererImpl::setSynchronizationMode(SynchronizationMode mode) {
    if (synchronizationMode_ == mode) return;
    // Klatki w locie zostały zgłoszone ze starym mechanizmem (fence albo timeline);
    // nowy tryb by na nie nie czekał i nadpisałby bufory komend używane przez GPU.
    waitIdle();
    synchronizationMode_ = mode;
}

void Renderer::RendererImpl::setPresentMode(PresentMode mode) {
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

void Renderer::RendererImpl::setWireframeEnabled(bool enabled) {
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

void Renderer::RendererImpl::setLights(const std::vector<SceneLight>& lights) {
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

IRenderFeature* Renderer::RendererImpl::getFeature(const std::string& name) {
    for (auto& f : features_) {
        if (f->getName() == name) return f.get();
    }
    return nullptr;
}

void Renderer::RendererImpl::removeFeature(const std::string& name) {
    auto it = std::remove_if(features_.begin(), features_.end(),
        [&name](const std::shared_ptr<IRenderFeature>& f) {
            return f->getName() == name;
        });
    features_.erase(it, features_.end());
}

void Renderer::RendererImpl::createUniformBuffers() {
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

void Renderer::RendererImpl::updateUniformBuffer(const RenderView& view) {
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

void Renderer::RendererImpl::createDescriptorResources() {
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

void Renderer::RendererImpl::recreateSwapChain() {
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

VkCommandBuffer Renderer::RendererImpl::beginSingleTimeCommands() {
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

void Renderer::RendererImpl::endSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(vulkanContext_->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkanContext_->graphicsQueue());
    vkFreeCommandBuffers(vulkanContext_->device(), vulkanContext_->commandPool(), 1, &cmd);
}

void Renderer::RendererImpl::createSyncObjects() {
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

void Renderer::RendererImpl::createPresentSemaphores() {
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

void Renderer::RendererImpl::destroyPresentSemaphores() {
    VkDevice device = vulkanContext_->device();
    for (VkSemaphore sem : renderFinishedSemaphores_) if (sem) vkDestroySemaphore(device, sem, nullptr);
    for (VkSemaphore sem : presentReadySemaphores_) if (sem) vkDestroySemaphore(device, sem, nullptr);
    renderFinishedSemaphores_.clear();
    presentReadySemaphores_.clear();
}

void Renderer::RendererImpl::createCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vulkanContext_->commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kMaxFrames;
    commandBuffers_.resize(kMaxFrames);
    vkAllocateCommandBuffers(vulkanContext_->device(), &allocInfo, commandBuffers_.data());
}


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
