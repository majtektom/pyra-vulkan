#include <array>
#include <stdexcept>
#include <vector>
#include "render/renderer.h"
#include "render/vulkan_util.h"
#include "core/math.h"
#include "core/types.h"
#include "object/ocean_renderer.h"

namespace engine3d {

namespace {

struct PushConstants {
    float cameraPosition[4];
    float time;
    float waterLevel;
    float waveAmplitude;
    float waveSpeed;
    float waveFrequency;
    float foamThreshold;
    float sceneColorIndex;   // Bindless index for ocean_scene_color texture
    float sceneDepthIndex;   // Bindless index for ocean_scene_depth texture
    float dudvMapIndex;      // Bindless index for water_dudv texture
    float lightDir[4];
};

void generateOceanMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    constexpr int n = 512;
    constexpr float size = 6500.0f;
    constexpr float half = size * 0.5f;
    constexpr float y = -8.0f;

    vertices.resize((n + 1) * (n + 1));
    for (int iz = 0; iz <= n; ++iz) {
        for (int ix = 0; ix <= n; ++ix) {
            float px = -half + (float(ix) / n) * size;
            float pz = -half + (float(iz) / n) * size;
            float u = float(ix) / n;
            float v = float(iz) / n;
            vertices[iz * (n + 1) + ix] = {
                {{px, y, pz}}, {{0, 1, 0}}, {{1, 1, 1}}, {{u, v}}
            };
        }
    }

    indices.resize(n * n * 6);
    uint32_t* idx = indices.data();
    for (int iz = 0; iz < n; ++iz) {
        for (int ix = 0; ix < n; ++ix) {
            uint32_t a = iz * (n + 1) + ix;
            uint32_t b = a + 1;
            uint32_t c = a + (n + 1);
            uint32_t d = c + 1;
            *idx++ = a; *idx++ = c; *idx++ = b;
            *idx++ = b; *idx++ = c; *idx++ = d;
        }
    }
}

}  // namespace

OceanRenderer::OceanRenderer() : name_("OceanRenderer") {}
OceanRenderer::~OceanRenderer() {
    if (renderer_) {
        cleanup(renderer_->device());
    }
}

void OceanRenderer::configure(const OceanConfig& config, const std::string& shaderDir, const std::string& descName) {
    config_ = config;
    config_.enabled = true;
    shaderDir_ = shaderDir;
    descName_ = descName;
}

void OceanRenderer::init(Renderer* renderer) {
    renderer_ = renderer;
    if (!enabled_) {
        ready_ = false;
        return;
    }

    VkDevice device = renderer_->device();
    VkPhysicalDevice physicalDevice = renderer_->physicalDevice();
    VkCommandPool commandPool = renderer_->commandPool();
    VkQueue graphicsQueue = renderer_->graphicsQueue();
    VkExtent2D extent = renderer_->extent();
    VkDescriptorSetLayout descriptorSetLayout = renderer_->descriptorSetLayout();
    VkFormat colorFormat = renderer_->colorFormat();
    VkFormat depthFormat = renderer_->depthFormat();

    const auto vertCode = vk::readFile(shaderDir_ + "/ocean.vert.spv");
    const auto fragCode = vk::readFile(shaderDir_ + "/ocean.frag.spv");

    const VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    const VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "vertexMain";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "fragmentMain";

    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

    auto bindingDesc = Vertex::bindingDescription();
    auto attribDescs = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribDescs.size());
    vertexInput.pVertexAttributeDescriptions = attribDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f; viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0}; scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = (renderer_ && renderer_->isWireframeEnabled()) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_TRUE;
    blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttach.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttach.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttach;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw std::runtime_error("failed to create ocean pipeline layout");
    }

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
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;
    pipelineInfo.pNext = &renderingInfo;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw std::runtime_error("failed to create ocean pipeline");
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);

    // Load water textures for reflection/refraction distortion
    renderer_->loadTexture("water_dudv", "assets/woda/water_dudv.bmp", true);
    renderer_->loadTexture("water_bump", "assets/woda/water_bump.bmp", true);

    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    generateOceanMesh(verts, inds);
    indexCount_ = static_cast<uint32_t>(inds.size());

    VkDeviceSize vbSize = sizeof(Vertex) * verts.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        verts.data(), vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer_, vertexBufferMemory_);

    VkDeviceSize ibSize = sizeof(uint32_t) * inds.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        inds.data(), ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexBuffer_, indexBufferMemory_);

    ready_ = true;
}

void OceanRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) {
    if (!enabled_ || !renderer_) return;

    bool currentWireframe = renderer_->isWireframeEnabled();
    if (currentWireframe != lastWireframe_) {
        lastWireframe_ = currentWireframe;
        if (ready_) {
            renderer_->waitIdle();
            cleanup(renderer_->device());
        }
        init(renderer_);
    }

    if (!ready_) return;

    VkDescriptorSet descSet = renderer_->getTextureDescriptorSet(descName_, frameIndex);
    recordDrawCommands(cmd, view.cameraPosition, time_, descSet, config_);
}

void OceanRenderer::cleanup(VkDevice device) {
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory_, nullptr);
        vertexBufferMemory_ = VK_NULL_HANDLE;
    }
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, indexBuffer_, nullptr);
        indexBuffer_ = VK_NULL_HANDLE;
    }
    if (indexBufferMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, indexBufferMemory_, nullptr);
        indexBufferMemory_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    ready_ = false;
}

void OceanRenderer::recordDrawCommands(
    VkCommandBuffer commandBuffer,
    const Vec3& cameraPosition,
    float time,
    VkDescriptorSet descriptorSet,
    const OceanConfig& config
) const {
    if (!ready_ || !enabled_) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &descriptorSet, 0, nullptr);

    PushConstants push{};
    push.cameraPosition[0] = cameraPosition.x;
    push.cameraPosition[1] = cameraPosition.y;
    push.cameraPosition[2] = cameraPosition.z;
    push.cameraPosition[3] = 0.0f;
    push.time = time;
    push.waterLevel = config.waterLevel;
    push.waveAmplitude = config.waveAmplitude;
    push.waveSpeed = config.waveSpeed;
    push.waveFrequency = config.waveFrequency;
    push.foamThreshold = config.foamThreshold;
    push.sceneColorIndex = static_cast<float>(renderer_->getTextureIndex("ocean_scene_color"));
    push.sceneDepthIndex = static_cast<float>(renderer_->getTextureIndex("ocean_scene_depth"));
    push.dudvMapIndex = static_cast<float>(renderer_->getTextureIndex("water_dudv"));
    push.lightDir[0] = 0.5f;
    push.lightDir[1] = 1.0f;
    push.lightDir[2] = 0.3f;
    push.lightDir[3] = 0.25f;

    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &push);

    VkBuffer vb[] = {vertexBuffer_};
    VkDeviceSize offset[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
}

}  // namespace engine3d
