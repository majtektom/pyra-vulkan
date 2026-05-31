#include "render/material_system.h"
#include "render/base_material_instance.h"
#include "render/vulkan_util.h"
#include <array>
#include <stdexcept>
#include <vector>

namespace engine3d {

MaterialSystem::MaterialSystem(std::shared_ptr<VulkanContext> context, VkFormat colorFormat, VkFormat depthFormat) 
    : context_(context), colorFormat_(colorFormat), depthFormat_(depthFormat) {
    createDescriptorSetLayout();
    createBoneDescriptorSetLayout();
}

MaterialSystem::~MaterialSystem() {
    VkDevice device = context_->device();
    for (auto& pair : pipelineCache_) {
        vkDestroyPipeline(device, pair.second.pipeline, nullptr);
        vkDestroyPipelineLayout(device, pair.second.layout, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
    }
    if (boneDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, boneDescriptorSetLayout_, nullptr);
    }
}

void MaterialSystem::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1024; // kMaxBindlessTextures
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};
    
    VkDescriptorBindingFlags bindingFlags[2] = {0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT};
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = 2;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(context_->device(), &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }
}

VkDescriptorSetLayout MaterialSystem::createBoneDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding boneBinding{};
    boneBinding.binding = 0;
    boneBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    boneBinding.descriptorCount = 1;
    boneBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &boneBinding;

    VkDescriptorSetLayout boneLayout;
    vkCreateDescriptorSetLayout(context_->device(), &layoutInfo, nullptr, &boneLayout);
    return boneLayout;
}

std::shared_ptr<MaterialInstance> MaterialSystem::getOrCreateMaterialInstance(const Material& materialData) {
    return materialCache_.load(materialData.name, [&]() {
        VulkanPipeline pipeline = getOrCreatePipeline(materialData);
        return std::make_shared<BaseMaterialInstance>(materialData, pipeline, VK_NULL_HANDLE);
    });
}

VulkanPipeline MaterialSystem::getOrCreatePipeline(const Material& materialData) {
    PipelineKey key{materialData.shaderProgram, materialData.blendMode, materialData.depthTest, materialData.depthWrite, materialData.cullMode};
    if (pipelineCache_.count(key)) return pipelineCache_[key];

    VkDevice device = context_->device();
    std::string shaderName = materialData.shaderProgram.empty() ? "default" : materialData.shaderProgram;
    std::string shaderDir = "bin/assets/shaders/" + shaderName + "/";
    auto vertCode = vk::readFile(shaderDir + shaderName + ".vert.spv");
    auto fragCode = vk::readFile(shaderDir + shaderName + ".frag.spv");
    VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].pName = materialData.vertexEntryPoint.c_str();
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = materialData.fragmentEntryPoint.c_str();

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();

    bool isParticle = (materialData.shaderProgram == "particle");
    bool isSprite = (materialData.shaderProgram == "sprite");

    VkVertexInputBindingDescription binding2{};
    std::vector<VkVertexInputAttributeDescription> binding2Attrs;

    if (isSprite) {
        binding2.binding = 1;
        binding2.stride = sizeof(SpriteInstanceData);
        binding2.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        VkVertexInputAttributeDescription attrPosSize{};
        attrPosSize.binding = 1;
        attrPosSize.location = 4;
        attrPosSize.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrPosSize.offset = offsetof(SpriteInstanceData, posX);
        binding2Attrs.push_back(attrPosSize);

        VkVertexInputAttributeDescription attrColor{};
        attrColor.binding = 1;
        attrColor.location = 5;
        attrColor.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrColor.offset = offsetof(SpriteInstanceData, r);
        binding2Attrs.push_back(attrColor);

        VkVertexInputAttributeDescription attrUV{};
        attrUV.binding = 1;
        attrUV.location = 6;
        attrUV.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrUV.offset = offsetof(SpriteInstanceData, uvOffsetU);
        binding2Attrs.push_back(attrUV);
    } else if (isParticle) {
        binding2.binding = 1;
        binding2.stride = sizeof(ParticleInstanceData);
        binding2.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        VkVertexInputAttributeDescription attrPosSize{};
        attrPosSize.binding = 1;
        attrPosSize.location = 4;
        attrPosSize.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrPosSize.offset = offsetof(ParticleInstanceData, posX);
        binding2Attrs.push_back(attrPosSize);

        VkVertexInputAttributeDescription attrColor{};
        attrColor.binding = 1;
        attrColor.location = 5;
        attrColor.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrColor.offset = offsetof(ParticleInstanceData, r);
        binding2Attrs.push_back(attrColor);
    } else {
        binding2.binding = 1;
        binding2.stride = sizeof(uint32_t) * 4 + sizeof(float) * 4;
        binding2.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription skinAttrs[2]{};
        skinAttrs[0].binding = 1;
        skinAttrs[0].location = 4;
        skinAttrs[0].format = VK_FORMAT_R32G32B32A32_UINT;
        skinAttrs[0].offset = 0;
        skinAttrs[1].binding = 1;
        skinAttrs[1].location = 5;
        skinAttrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        skinAttrs[1].offset = sizeof(uint32_t) * 4;
        binding2Attrs.push_back(skinAttrs[0]);
        binding2Attrs.push_back(skinAttrs[1]);
    }

    std::vector<VkVertexInputBindingDescription> bindings = {bindingDesc, binding2};
    std::vector<VkVertexInputAttributeDescription> allAttrs(attrDescs.begin(), attrDescs.end());
    allAttrs.insert(allAttrs.end(), binding2Attrs.begin(), binding2Attrs.end());

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttrs.size());
    vertexInput.pVertexAttributeDescriptions = allAttrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    if (materialData.cullMode == CullMode::Disabled) {
        rasterizer.cullMode = VK_CULL_MODE_NONE;
    } else if (materialData.cullMode == CullMode::Front) {
        rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    } else if (materialData.cullMode == CullMode::FrontAndBack) {
        rasterizer.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
    } else {
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    }

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = materialData.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = materialData.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = (materialData.blendMode != BlendMode::Disabled) ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (materialData.blendMode == BlendMode::Additive) {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (materialData.blendMode == BlendMode::Alpha) {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        colorBlendAttachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = 128;
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, boneDescriptorSetLayout_};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    VulkanPipeline pipeline;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipeline.layout);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline.layout;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat_;
    renderingInfo.depthAttachmentFormat = depthFormat_;
    pipelineInfo.pNext = &renderingInfo;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline");
    }
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    pipelineCache_[key] = pipeline;
    return pipeline;
}

void MaterialSystem::invalidatePipelinesForShader(const std::string& shaderName) {
    auto it = pipelineCache_.begin();
    while (it != pipelineCache_.end()) {
        if (it->first.shaderName == shaderName || (shaderName.empty() && it->first.shaderName.empty())) {
            it = pipelineCache_.erase(it);
        } else {
            ++it;
        }
    }
    materialCache_.clear();
}

} // namespace engine3d
