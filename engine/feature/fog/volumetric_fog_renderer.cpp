#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

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
    LOG_INFO("Volumetric Fog Renderer initialized ({}x{}x{})", kFroxelX, kFroxelY, kFroxelZ);
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

static void executeOneTimeCommand(VkDevice device, VkCommandPool pool, VkQueue queue,
                                   VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

void VolumetricFogRenderer::createVolumeTexture(VkDevice device, VkPhysicalDevice physicalDevice) {
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;

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
        throw std::runtime_error("failed to create fog volume 3D image");
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
        throw std::runtime_error("failed to allocate fog volume memory");
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
        throw std::runtime_error("failed to create fog volume image view");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0;
    samplerInfo.maxLod = 0;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &volumeSampler_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create fog volume sampler");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = renderer_->commandPool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = volumeImage_;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    executeOneTimeCommand(device, renderer_->commandPool(), renderer_->graphicsQueue(), cmd);
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
        throw std::runtime_error("failed to map local fog volume buffer");
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
        throw std::runtime_error("failed to create compute descriptor set layout");
    }

    auto compCode = vk::readFile(shaderDir_ + "/fog_volume.comp.spv");
    VkShaderModule compModule = vk::createShaderModule(device, compCode);

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = compModule;
    stage.pName = "main";

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout_;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = 128;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &computePipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, compModule, nullptr);
        throw std::runtime_error("failed to create fog compute pipeline layout");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = computePipelineLayout_;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline_) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, computePipelineLayout_, nullptr);
        computePipelineLayout_ = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, compModule, nullptr);
        throw std::runtime_error("failed to create fog compute pipeline");
    }

    vkDestroyShaderModule(device, compModule, nullptr);
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
        throw std::runtime_error("failed to create composite descriptor set layout");
    }

    auto vertCode = vk::readFile(shaderDir_ + "/fog_composite.vert.spv");
    auto fragCode = vk::readFile(shaderDir_ + "/fog_composite.frag.spv");

    VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
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

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.blendEnable = VK_TRUE;
    blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttach;

    VkDescriptorSetLayout setLayouts[2] = {
        renderer_->descriptorSetLayout(),
        compositeDescriptorSetLayout_
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 8;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &compositePipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw std::runtime_error("failed to create fog composite pipeline layout");
    }

    VkViewport viewport{};
    viewport.width = static_cast<float>(renderer_->extent().width);
    viewport.height = static_cast<float>(renderer_->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = renderer_->extent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    std::vector<VkDynamicState> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates = dynStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = compositePipelineLayout_;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    VkFormat colorFormat = renderer_->colorFormat();
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    pipelineInfo.pNext = &renderingInfo;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositePipeline_) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, compositePipelineLayout_, nullptr);
        compositePipelineLayout_ = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw std::runtime_error("failed to create fog composite pipeline");
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
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
        throw std::runtime_error("failed to create fog descriptor pool");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &computeDescriptorSetLayout_;
    if (vkAllocateDescriptorSets(device, &allocInfo, &computeDescriptorSet_) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate compute descriptor set");
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
        throw std::runtime_error("failed to allocate composite descriptor set");
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

    VkImageMemoryBarrier volToGeneral{};
    volToGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    volToGeneral.image = volumeImage_;
    volToGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    volToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    volToGeneral.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    volToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    volToGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    volToGeneral.subresourceRange.levelCount = 1;
    volToGeneral.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &volToGeneral);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout_,
                            0, 1, &computeDescriptorSet_, 0, nullptr);

    float lightDirArray[4] = {0.0f, -1.0f, 0.0f, 0.0f};
    float sunElevation = 50.0f * 3.14159265f / 180.0f;
    lightDirArray[0] = cos(sunElevation) * 0.5f;
    lightDirArray[1] = -sin(sunElevation);
    lightDirArray[2] = cos(sunElevation) * 0.866f;
    lightDirArray[3] = static_cast<float>(localVolumes_.size());

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
    push.fogColor[0] = 0.575f; push.fogColor[1] = 0.68f; push.fogColor[2] = 0.88f; push.fogColor[3] = 1.0f;
    std::memcpy(push.lightDir, lightDirArray, sizeof(float) * 4);
    push.fogDensity = fogDensity_;
    push.fogHeightFalloff = fogHeightFalloff_;
    push.seaLevel = seaLevel_;
    push.fogRadius = fogRadius_;

    vkCmdPushConstants(cmd, computePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(push), &push);

    vkCmdDispatch(cmd, kFroxelX, kFroxelY, 1);

    VkImageMemoryBarrier volumeBarrier{};
    volumeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    volumeBarrier.image = volumeImage_;
    volumeBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    volumeBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    volumeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    volumeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    volumeBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    volumeBarrier.subresourceRange.levelCount = 1;
    volumeBarrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &volumeBarrier);
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

    struct { uint32_t worldPosIdx; float fogRadius; } compositePush = {};
    compositePush.worldPosIdx = renderer_->getTextureIndex("gbuffer_world_pos");
    compositePush.fogRadius = fogRadius_;
    vkCmdPushConstants(cmd, compositePipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(compositePush), &compositePush);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace engine3d
