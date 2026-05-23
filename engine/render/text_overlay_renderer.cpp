#include "text_overlay_renderer.h"
#include "vulkan_util.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace engine3d {

TextOverlayRenderer::TextOverlayRenderer() = default;
TextOverlayRenderer::~TextOverlayRenderer() = default;

void TextOverlayRenderer::init(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkRenderPass renderPass,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkExtent2D /*extent*/
) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    commandPool_ = commandPool;
    graphicsQueue_ = graphicsQueue;

    std::string shaderDir = "bin/assets/shaders/text_overlay/";
    auto vertCode = vk::readFile(shaderDir + "text.vert.spv");
    auto fragCode = vk::readFile(shaderDir + "text.frag.spv");

    VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "vertexMain";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "fragmentMain";

    auto bindingDesc = TextVertex::bindingDescription();
    auto attrDescs = TextVertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

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
    blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttach.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttach;

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    std::vector<VkDynamicState> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates = dynStates.data();

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &samplerBinding;
    vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &descriptorSetLayout_);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 0;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw std::runtime_error("failed to create text overlay pipeline");
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);

    ready_ = true;
}

void TextOverlayRenderer::loadFont(const FontAsset& fontAsset) {
    fontAtlas_ = FontAtlasLoader::load(fontAsset, {});

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vk::createImage(device_, physicalDevice_, fontAtlas_.atlasWidth, fontAtlas_.atlasHeight, 1,
                    fontAtlas_.format, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    fontImage_, fontImageMemory_);

    vk::transitionImageLayout(device_, commandPool_, graphicsQueue_, fontImage_, fontAtlas_.format,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize dataSize = fontAtlas_.pixelBytes.size();
    vk::createBuffer(device_, physicalDevice_, dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);

    void* mapped;
    vkMapMemory(device_, stagingMemory, 0, dataSize, 0, &mapped);
    std::memcpy(mapped, fontAtlas_.pixelBytes.data(), dataSize);
    vkUnmapMemory(device_, stagingMemory);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {fontAtlas_.atlasWidth, fontAtlas_.atlasHeight, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, fontImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingMemory, nullptr);

    vk::transitionImageLayout(device_, commandPool_, graphicsQueue_, fontImage_, fontAtlas_.format,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);

    fontImageView_ = vk::createImageView(device_, fontImage_, fontAtlas_.format,
                                         VK_IMAGE_ASPECT_COLOR_BIT, 1);
    fontSampler_ = vk::createClampSampler(device_);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_);

    VkDescriptorSetAllocateInfo descAlloc{};
    descAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAlloc.descriptorPool = descriptorPool_;
    descAlloc.descriptorSetCount = 1;
    descAlloc.pSetLayouts = &descriptorSetLayout_;
    vkAllocateDescriptorSets(device_, &descAlloc, &descriptorSet_);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = fontImageView_;
    imageInfo.sampler = fontSampler_;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    fontLoaded_ = true;
}

void TextOverlayRenderer::deleteVertexBuffer() {
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, vertexBufferMemory_, nullptr);
        vertexBufferMemory_ = VK_NULL_HANDLE;
    }
    vertexBufferCapacity_ = 0;
}

void TextOverlayRenderer::setText(const std::string& text, float x, float y, float scale,
                                   const std::array<float, 4>& color,
                                   float screenWidth, float screenHeight) {
    if (!fontLoaded_ || fontAtlas_.glyphs.empty()) return;

    std::vector<TextVertex> vertices;
    float cursorX = x;
    float baselineY = y;

    for (char c : text) {
        uint32_t cp = static_cast<uint32_t>(static_cast<unsigned char>(c));
        auto it = fontAtlas_.glyphs.find(cp);
        if (it == fontAtlas_.glyphs.end()) {
            it = fontAtlas_.glyphs.find(static_cast<uint32_t>('?'));
            if (it == fontAtlas_.glyphs.end()) continue;
        }

        const auto& g = it->second;
        float gx = cursorX + g.bearingX * scale;
        float gy = baselineY - g.bearingY * scale;
        float gw = g.width * scale;
        float gh = g.height * scale;

        float ndcX0 = (gx      / screenWidth)  * 2.0f - 1.0f;
        float ndcX1 = ((gx+gw) / screenWidth)  * 2.0f - 1.0f;
        float ndcY0 = (gy      / screenHeight) * 2.0f - 1.0f;
        float ndcY1 = ((gy+gh) / screenHeight) * 2.0f - 1.0f;

        TextVertex v0, v1, v2, v3;
        // lewy górny
        v0.pos[0] = ndcX0; v0.pos[1] = ndcY0; v0.texCoord[0] = g.u0; v0.texCoord[1] = g.v0;
        // prawy górny
        v1.pos[0] = ndcX1; v1.pos[1] = ndcY0; v1.texCoord[0] = g.u1; v1.texCoord[1] = g.v0;
        // prawy dolny
        v2.pos[0] = ndcX1; v2.pos[1] = ndcY1; v2.texCoord[0] = g.u1; v2.texCoord[1] = g.v1;
        // lewy dolny
        v3.pos[0] = ndcX0; v3.pos[1] = ndcY1; v3.texCoord[0] = g.u0; v3.texCoord[1] = g.v1;

        for (int i = 0; i < 4; ++i) {
            v0.color[i] = v1.color[i] = v2.color[i] = v3.color[i] = color[i];
        }

        // trójkąt 1
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        // trójkąt 2
        vertices.push_back(v2);
        vertices.push_back(v3);
        vertices.push_back(v0);

        cursorX += g.advanceX * scale;
    }

    if (vertices.empty()) { vertexCount_ = 0; return; }

    vertexCount_ = static_cast<uint32_t>(vertices.size());
    VkDeviceSize requiredSize = sizeof(TextVertex) * vertices.size();

    if (vertexBuffer_ == VK_NULL_HANDLE || requiredSize > vertexBufferCapacity_) {
        deleteVertexBuffer();
        vertexBufferCapacity_ = requiredSize;
        vk::createBuffer(device_, physicalDevice_, requiredSize,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         vertexBuffer_, vertexBufferMemory_);
    }

    void* mapped;
    vkMapMemory(device_, vertexBufferMemory_, 0, requiredSize, 0, &mapped);
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(requiredSize));
    vkUnmapMemory(device_, vertexBufferMemory_);
}

void TextOverlayRenderer::recordDrawCommands(VkCommandBuffer commandBuffer) const {
    if (!ready_ || !fontLoaded_ || vertexCount_ == 0) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    VkBuffer vb[] = {vertexBuffer_};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, offsets);
    vkCmdDraw(commandBuffer, vertexCount_, 1, 0, 0);
}

void TextOverlayRenderer::cleanup() {
    deleteVertexBuffer();
    if (descriptorSet_ != VK_NULL_HANDLE && descriptorPool_ != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, descriptorPool_, 1, &descriptorSet_);
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (fontSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, fontSampler_, nullptr);
        fontSampler_ = VK_NULL_HANDLE;
    }
    if (fontImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, fontImageView_, nullptr);
        fontImageView_ = VK_NULL_HANDLE;
    }
    if (fontImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, fontImage_, nullptr);
        vkFreeMemory(device_, fontImageMemory_, nullptr);
        fontImage_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
    ready_ = false;
    fontLoaded_ = false;
}

} // namespace engine3d
