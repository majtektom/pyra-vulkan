#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "render/pipeline_builder.h"
#include "render/vulkan_util.h"
#include "render/renderer.h"
#include "render/backend/vulkan_context.h"
#include "feature/fog/volumetric_fog_renderer.h"
#include "core/logger.h"

namespace engine3d {

namespace {

struct GpuLocalFogVolume {
    float centerDensity[4];
    float halfExtentsEdgeFade[4];
};

} // namespace

VolumetricFogRenderer::VolumetricFogRenderer() : name_("VolumetricFogRenderer") {}

VolumetricFogRenderer::~VolumetricFogRenderer() {
    if (renderer_) {
        cleanup(renderer_->device());
    }
}

void VolumetricFogRenderer::configure(const std::string& shaderDir) {
    shaderDir_ = shaderDir;
    enabled_ = false;
}

void VolumetricFogRenderer::setLocalVolumes(std::vector<LocalFogVolume> volumes) {
    if (volumes.size() > kMaxLocalVolumes) {
        volumes.resize(kMaxLocalVolumes);
    }
    for (auto& volume : volumes) {
        volume.density = std::max(0.0f, volume.density);
        volume.halfExtents.x = std::max(0.01f, volume.halfExtents.x);
        volume.halfExtents.y = std::max(0.01f, volume.halfExtents.y);
        volume.halfExtents.z = std::max(0.01f, volume.halfExtents.z);
        volume.edgeFade = std::clamp(volume.edgeFade, 0.01f, 1.0f);
    }
    localVolumes_ = std::move(volumes);
    localVolumesDirty_ = true;
}

void VolumetricFogRenderer::init(Renderer* renderer) {
    renderer_ = renderer;
    if (!enabled_) { ready_ = false; return; }

    VkDevice device = renderer_->device();
    VkPhysicalDevice physicalDevice = renderer_->physicalDevice();

    createVolumeTexture(device, physicalDevice);
    createLocalVolumeBuffer(device, physicalDevice);
    createComputePipeline(device);
    createCompositePipeline(device);
    writeDescriptorSets(device);

    ready_ = true;
    LOG_INFO("Mgła wolumetryczna gotowa (siatka {}x{}x{})", kFroxelX, kFroxelY, kFroxelZ);
}

void VolumetricFogRenderer::cleanup(VkDevice device) {
    if (computePipeline_) { vkDestroyPipeline(device, computePipeline_, nullptr); computePipeline_ = VK_NULL_HANDLE; }
    if (computePipelineLayout_) { vkDestroyPipelineLayout(device, computePipelineLayout_, nullptr); computePipelineLayout_ = VK_NULL_HANDLE; }
    if (computeDescriptorSetLayout_) { vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout_, nullptr); computeDescriptorSetLayout_ = VK_NULL_HANDLE; }

    if (compositePipeline_) { vkDestroyPipeline(device, compositePipeline_, nullptr); compositePipeline_ = VK_NULL_HANDLE; }
    if (compositePipelineLayout_) { vkDestroyPipelineLayout(device, compositePipelineLayout_, nullptr); compositePipelineLayout_ = VK_NULL_HANDLE; }
    if (compositeDescriptorSetLayout_) { vkDestroyDescriptorSetLayout(device, compositeDescriptorSetLayout_, nullptr); compositeDescriptorSetLayout_ = VK_NULL_HANDLE; }

    if (descriptorPool_) { vkDestroyDescriptorPool(device, descriptorPool_, nullptr); descriptorPool_ = VK_NULL_HANDLE; }

    if (volumeSampler_) { vkDestroySampler(device, volumeSampler_, nullptr); volumeSampler_ = VK_NULL_HANDLE; }
    if (volumeImageView_) { vkDestroyImageView(device, volumeImageView_, nullptr); volumeImageView_ = VK_NULL_HANDLE; }
    if (volumeImage_) { vkDestroyImage(device, volumeImage_, nullptr); volumeImage_ = VK_NULL_HANDLE; }
    if (volumeImageMemory_) { vkFreeMemory(device, volumeImageMemory_, nullptr); volumeImageMemory_ = VK_NULL_HANDLE; }
    if (localVolumeBufferMapped_) {
        vkUnmapMemory(device, localVolumeBufferMemory_);
        localVolumeBufferMapped_ = nullptr;
    }
    if (localVolumeBuffer_) { vkDestroyBuffer(device, localVolumeBuffer_, nullptr); localVolumeBuffer_ = VK_NULL_HANDLE; }
    if (localVolumeBufferMemory_) { vkFreeMemory(device, localVolumeBufferMemory_, nullptr); localVolumeBufferMemory_ = VK_NULL_HANDLE; }

    ready_ = false;
}


void VolumetricFogRenderer::createVolumeTexture(VkDevice device, VkPhysicalDevice physicalDevice) {
    // Musi odpowiadać formatowi storage image w fog_volume.hlsl (RWTexture3D<float4> -> Rgba32f).
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;

    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_3D;
    ci.format = format;
    ci.extent = {kFroxelX, kFroxelY, kFroxelZ};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &ci, nullptr, &volumeImage_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się utworzyć obrazu 3D mgły");
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, volumeImage_, &memReq);

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = memReq.size;

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    bool found = false;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            ai.memoryTypeIndex = i;
            found = true;
            break;
        }
    }
    if (!found || vkAllocateMemory(device, &ai, nullptr, &volumeImageMemory_) != VK_SUCCESS) {
        vkDestroyImage(device, volumeImage_, nullptr);
        volumeImage_ = VK_NULL_HANDLE;
        throw std::runtime_error("nie udało się przydzielić pamięci mgły");
    }
    vkBindImageMemory(device, volumeImage_, volumeImageMemory_, 0);

    VkImageViewCreateInfo iv{};
    iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = volumeImage_;
    iv.viewType = VK_IMAGE_VIEW_TYPE_3D;
    iv.format = format;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.baseMipLevel = 0;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.baseArrayLayer = 0;
    iv.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &iv, nullptr, &volumeImageView_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się utworzyć widoku obrazu mgły");
    }

    volumeSampler_ = vk::createClampSampler(device);

    // Obraz od razu w układzie do odczytu w shaderze.
    vk::transitionImageLayout(device, renderer_->commandPool(), renderer_->graphicsQueue(), volumeImage_,
                              VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
}

void VolumetricFogRenderer::createLocalVolumeBuffer(VkDevice device, VkPhysicalDevice physicalDevice) {
    const VkDeviceSize size = sizeof(GpuLocalFogVolume) * kMaxLocalVolumes;
    vk::createBuffer(
        device,
        physicalDevice,
        size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        localVolumeBuffer_,
        localVolumeBufferMemory_);
    if (vkMapMemory(device, localVolumeBufferMemory_, 0, size, 0, &localVolumeBufferMapped_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się zmapować bufora lokalnych obszarów mgły");
    }
}

void VolumetricFogRenderer::createComputePipeline(VkDevice device) {
    VkDescriptorSetLayoutBinding storageBinding{};
    storageBinding.binding = 0;
    storageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storageBinding.descriptorCount = 1;
    storageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding volumeBufferBinding{};
    volumeBufferBinding.binding = 1;
    volumeBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    volumeBufferBinding.descriptorCount = 1;
    volumeBufferBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    const std::array bindings = {storageBinding, volumeBufferBinding};
    VkDescriptorSetLayoutCreateInfo storageLayoutInfo{};
    storageLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    storageLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    storageLayoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &storageLayoutInfo, nullptr, &computeDescriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się utworzyć układu deskryptorów obliczeń mgły");
    }

    const vk::ShaderModule comp(device, shaderDir_ + "/fog_volume.comp.spv");
    computePipelineLayout_ = vk::createPipelineLayout(device, {computeDescriptorSetLayout_}, 128,
                                                      VK_SHADER_STAGE_COMPUTE_BIT, "fog_volume");

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = comp;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = computePipelineLayout_;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się utworzyć potoku obliczeń mgły");
    }
}

void VolumetricFogRenderer::createCompositePipeline(VkDevice device) {
    VkDescriptorSetLayoutBinding volumeBinding{};
    volumeBinding.binding = 0;
    volumeBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    volumeBinding.descriptorCount = 1;
    volumeBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo volumeLayoutInfo{};
    volumeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    volumeLayoutInfo.bindingCount = 1;
    volumeLayoutInfo.pBindings = &volumeBinding;
    if (vkCreateDescriptorSetLayout(device, &volumeLayoutInfo, nullptr, &compositeDescriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się utworzyć układu deskryptorów nakładania mgły");
    }

    // Pełnoekranowy trójkąt bez buforów wierzchołków; mgła jest już przemnożona przez alfę
    // (kolor + tło * (1 - a)), głębokości nie testujemy - shader sam czyta głębię sceny.
    const vk::ShaderModule vert(device, shaderDir_ + "/fog_composite.vert.spv");
    const vk::ShaderModule frag(device, shaderDir_ + "/fog_composite.frag.spv");

    compositePipelineLayout_ = vk::createPipelineLayout(
        device, {renderer_->descriptorSetLayout(), compositeDescriptorSetLayout_}, 8,
        VK_SHADER_STAGE_FRAGMENT_BIT, "fog_composite");
    compositePipeline_ = vk::GraphicsPipelineBuilder()
        .shaders(vert, frag)
        .cull(VK_CULL_MODE_NONE)
        .depth(false, false)
        .colorFormat(renderer_->colorFormat(), vk::BlendMode::Premultiplied)
        .depthFormat(renderer_->depthFormat())
        .build(device, compositePipelineLayout_, "fog_composite");
}

void VolumetricFogRenderer::writeDescriptorSets(VkDevice device) {
    VkDescriptorPoolSize poolSizes[3] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 2;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się utworzyć puli deskryptorów mgły");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &computeDescriptorSetLayout_;
    if (vkAllocateDescriptorSets(device, &allocInfo, &computeDescriptorSet_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się przydzielić deskryptorów obliczeń mgły");
    }

    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageInfo.imageView = volumeImageView_;
    storageInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = computeDescriptorSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &storageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    VkDescriptorBufferInfo localVolumeInfo{};
    localVolumeInfo.buffer = localVolumeBuffer_;
    localVolumeInfo.offset = 0;
    localVolumeInfo.range = sizeof(GpuLocalFogVolume) * kMaxLocalVolumes;

    VkWriteDescriptorSet localVolumeWrite{};
    localVolumeWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    localVolumeWrite.dstSet = computeDescriptorSet_;
    localVolumeWrite.dstBinding = 1;
    localVolumeWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    localVolumeWrite.descriptorCount = 1;
    localVolumeWrite.pBufferInfo = &localVolumeInfo;
    vkUpdateDescriptorSets(device, 1, &localVolumeWrite, 0, nullptr);

    allocInfo.pSetLayouts = &compositeDescriptorSetLayout_;
    if (vkAllocateDescriptorSets(device, &allocInfo, &compositeDescriptorSet_) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się przydzielić deskryptorów nakładania mgły");
    }

    VkDescriptorImageInfo sampledInfo{};
    sampledInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sampledInfo.imageView = volumeImageView_;
    sampledInfo.sampler = volumeSampler_;

    write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = compositeDescriptorSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &sampledInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void VolumetricFogRenderer::preRender(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) {
    if (!ready_ || !enabled_) return;

    // Bufor wolumenów jest jeden i czytany przez klatki w locie — nadpisujemy go tylko po
    // zmianie danych (rzadko), po upewnieniu się, że GPU skończyło poprzednie klatki.
    if (localVolumesDirty_) {
        vkDeviceWaitIdle(renderer_->device());
        std::array<GpuLocalFogVolume, kMaxLocalVolumes> gpuVolumes{};
        for (size_t i = 0; i < localVolumes_.size(); ++i) {
            const auto& source = localVolumes_[i];
            auto& destination = gpuVolumes[i];
            destination.centerDensity[0] = source.center.x;
            destination.centerDensity[1] = source.center.y;
            destination.centerDensity[2] = source.center.z;
            destination.centerDensity[3] = source.density;
            destination.halfExtentsEdgeFade[0] = source.halfExtents.x;
            destination.halfExtentsEdgeFade[1] = source.halfExtents.y;
            destination.halfExtentsEdgeFade[2] = source.halfExtents.z;
            destination.halfExtentsEdgeFade[3] = source.edgeFade;
        }
        std::memcpy(localVolumeBufferMapped_, gpuVolumes.data(), sizeof(gpuVolumes));
        localVolumesDirty_ = false;
    }

    // Wolumen mgły: z odczytu w shaderze (poprzednia klatka) na zapis w compute shaderze.
    vk::imageBarrier(cmd, {.image = volumeImage_, .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                           .srcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .srcAccess = VK_ACCESS_2_NONE,
                           .dstStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, .dstAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                           .mipCount = 1, .layerCount = 1});

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout_,
                            0, 1, &computeDescriptorSet_, 0, nullptr);

    struct FogPush {
        Matrix4 invViewProj;
        float cameraPos[4];
        float fogColor[4];
        float lightDir[4];
        float fogDensity;
        float fogHeightFalloff;
        float seaLevel;
        float fogRadius;
    } push{};

    push.invViewProj = inverse(multiply(view.projection, view.view));
    std::memcpy(push.cameraPos, &view.cameraPosition, sizeof(Vec3));
    push.cameraPos[3] = 0.0f;
    push.fogColor[0] = fogColor_.x; push.fogColor[1] = fogColor_.y; push.fogColor[2] = fogColor_.z; push.fogColor[3] = 1.0f;
    push.lightDir[0] = sunDirection_.x;
    push.lightDir[1] = sunDirection_.y;
    push.lightDir[2] = sunDirection_.z;
    // w = liczba lokalnych wolumenów (push constants mają już maksymalne 128 B, brak miejsca na osobne pole).
    push.lightDir[3] = static_cast<float>(localVolumes_.size());
    push.fogDensity = fogDensity_;
    push.fogHeightFalloff = fogHeightFalloff_;
    push.seaLevel = seaLevel_;
    push.fogRadius = fogRadius_;

    vkCmdPushConstants(cmd, computePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(push), &push);

    vkCmdDispatch(cmd, kFroxelX, kFroxelY, 1);

    // Zapisy compute widoczne dla odczytu w shaderze pikseli (nałożenie mgły na obraz).
    vk::imageBarrier(cmd, {.image = volumeImage_, .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                           .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           .srcStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, .srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                           .dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                           .mipCount = 1, .layerCount = 1});
}

void VolumetricFogRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) {
    if (!ready_ || !enabled_) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline_);

    VkDescriptorSet descSets[2] = {
        renderer_->getTextureDescriptorSet("", frameIndex),
        compositeDescriptorSet_
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipelineLayout_,
                            0, 2, descSets, 0, nullptr);

    // Głębia sceny zamiast pozycji z G-buffera — obejmuje też obiekty rysowane forwardem (np. teren).
    struct { uint32_t sceneDepthIdx; float fogRadius; } compositePush = {};
    compositePush.sceneDepthIdx = renderer_->getTextureIndex("ocean_scene_depth");
    compositePush.fogRadius = fogRadius_;
    vkCmdPushConstants(cmd, compositePipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(compositePush), &compositePush);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace engine3d
