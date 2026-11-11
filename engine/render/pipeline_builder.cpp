#include "render/pipeline_builder.h"

#include <stdexcept>

#include "render/vulkan_util.h"

namespace engine3d::vk {

ShaderModule::ShaderModule(VkDevice device, const std::string& spvPath)
    : device_(device), code_(readFile(spvPath)), module_(createShaderModule(device, code_)) {}

ShaderModule::~ShaderModule() {
    if (module_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, module_, nullptr);
}

namespace {

VkPipelineColorBlendAttachmentState blendState(BlendMode mode) {
    VkPipelineColorBlendAttachmentState s{};
    s.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (mode == BlendMode::Opaque) return s;

    s.blendEnable = VK_TRUE;
    s.colorBlendOp = VK_BLEND_OP_ADD;
    s.alphaBlendOp = VK_BLEND_OP_ADD;
    switch (mode) {
        case BlendMode::Alpha:          // kolor: src * a + dst * (1 - a); alfa celu = alfa źródła
            s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            break;
        case BlendMode::Premultiplied:  // src + dst * (1 - a)
            s.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case BlendMode::Additive:       // src * a + dst
            s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case BlendMode::Opaque:
            break;
    }
    return s;
}

} // namespace

GraphicsPipelineBuilder::GraphicsPipelineBuilder() {
    inputAssembly_.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly_.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    tessellation_.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;

    rasterizer_.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer_.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer_.lineWidth = 1.0f;
    rasterizer_.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer_.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    depthStencil_.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil_.depthTestEnable = VK_TRUE;
    depthStencil_.depthWriteEnable = VK_TRUE;
    depthStencil_.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil_.maxDepthBounds = 1.0f;

    dynamicStates_ = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::shader(VkShaderStageFlagBits stage, VkShaderModule module,
                                                         const char* entry) {
    VkPipelineShaderStageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage = stage;
    info.module = module;
    info.pName = entry;
    stages_.push_back(info);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::shaders(VkShaderModule vert, VkShaderModule frag) {
    shader(VK_SHADER_STAGE_VERTEX_BIT, vert, "vertexMain");
    return shader(VK_SHADER_STAGE_FRAGMENT_BIT, frag, "fragmentMain");
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::vertexInput(
    const VkVertexInputBindingDescription& binding, const VkVertexInputAttributeDescription* attributes, uint32_t count) {
    bindings_.push_back(binding);
    attributes_.insert(attributes_.end(), attributes, attributes + count);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::topology(VkPrimitiveTopology topology) {
    inputAssembly_.topology = topology;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::patches(uint32_t controlPoints) {
    inputAssembly_.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    tessellation_.patchControlPoints = controlPoints;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::cull(VkCullModeFlags mode, VkFrontFace front) {
    rasterizer_.cullMode = mode;
    rasterizer_.frontFace = front;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::polygonMode(VkPolygonMode mode) {
    rasterizer_.polygonMode = mode;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::depthBias(float constantFactor, float slopeFactor) {
    rasterizer_.depthBiasEnable = VK_TRUE;
    rasterizer_.depthBiasConstantFactor = constantFactor;
    rasterizer_.depthBiasSlopeFactor = slopeFactor;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::depthClamp(bool enable) {
    rasterizer_.depthClampEnable = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::depth(bool test, bool write, VkCompareOp compare) {
    depthStencil_.depthTestEnable = test ? VK_TRUE : VK_FALSE;
    depthStencil_.depthWriteEnable = write ? VK_TRUE : VK_FALSE;
    depthStencil_.depthCompareOp = compare;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::colorFormat(VkFormat format, BlendMode blend) {
    colorFormats_.push_back(format);
    blends_.push_back(blendState(blend));
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::customBlend(const VkPipelineColorBlendAttachmentState& state) {
    if (!blends_.empty()) blends_.back() = state;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::depthFormat(VkFormat format) {
    depthFormat_ = format;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::dynamic(VkDynamicState state) {
    dynamicStates_.push_back(state);
    return *this;
}

VkPipeline GraphicsPipelineBuilder::build(VkDevice device, VkPipelineLayout layout, const std::string& name) const {
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings_.size());
    vertexInput.pVertexBindingDescriptions = bindings_.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes_.size());
    vertexInput.pVertexAttributeDescriptions = attributes_.data();

    // Viewport i scissor są dynamiczne - tu tylko ich liczba.
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Bez celu głębokości test głębokości nie ma sensu (walidacja i tak go ignoruje) - wyłączamy jawnie.
    VkPipelineDepthStencilStateCreateInfo depthStencil = depthStencil_;
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = static_cast<uint32_t>(blends_.size());
    colorBlending.pAttachments = blends_.data();

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates_.size());
    dynamicState.pDynamicStates = dynamicStates_.data();

    // Dynamic rendering: zamiast render passa potok zna tylko formaty celów.
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats_.size());
    renderingInfo.pColorAttachmentFormats = colorFormats_.data();
    renderingInfo.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &renderingInfo;
    info.stageCount = static_cast<uint32_t>(stages_.size());
    info.pStages = stages_.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly_;
    info.pTessellationState = inputAssembly_.topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST ? &tessellation_ : nullptr;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &rasterizer_;
    info.pMultisampleState = &multisampling;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &colorBlending;
    info.pDynamicState = &dynamicState;
    info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się utworzyć potoku graficznego: " + name);
    }
    return pipeline;
}

VkPipelineLayout createPipelineLayout(VkDevice device, const std::vector<VkDescriptorSetLayout>& setLayouts,
                                      uint32_t pushSize, VkShaderStageFlags pushStages, const std::string& name) {
    VkPushConstantRange range{pushStages, 0, pushSize};
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    info.pSetLayouts = setLayouts.data();
    info.pushConstantRangeCount = pushSize > 0 ? 1u : 0u;
    info.pPushConstantRanges = pushSize > 0 ? &range : nullptr;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("nie udało się utworzyć układu potoku: " + name);
    }
    return layout;
}

} // namespace engine3d::vk
