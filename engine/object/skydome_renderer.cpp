#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>
#include "render/renderer.h"
#include "render/vulkan_util.h"
#include "core/math.h"
#include "core/types.h"
#include "object/skydome_renderer.h"

namespace engine3d {

namespace {

void lerpColor(const std::array<float, 4>& a, const std::array<float, 4>& b,
               float t, std::array<float, 4>& out) {
    for (int i = 0; i < 4; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
}

float sunHeightFromTime(float tod) {
    float t = std::fmod(tod, 24.0f);
    return std::sin((t - 6.0f) / 12.0f * PI);
}

void generateDomeGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    constexpr int n = 30;
    constexpr float r = 100.0f;
    constexpr float rh = 1000.0f;
    constexpr double theta1 = 0.0;
    constexpr double theta2 = PI;
    constexpr double phi1 = -PI / 2.0;
    constexpr double phi2 = 0.0;

    int numVertices = n * n;
    vertices.resize(numVertices);

    int idx = 0;
    for (int y = 0; y < n; ++y) {
        double t2 = phi1 + (y + 1) * (phi2 - phi1) / (n / 2.0);
        for (int x = 0; x < n; ++x) {
            double t1 = theta1 + x * (theta2 - theta1) / n;
            float px = static_cast<float>(x - n / 2) * r;
            float py = static_cast<float>(cos(t2) * sin(t1)) * rh;
            float pz = static_cast<float>(y - n / 2) * r;
            float u = static_cast<float>(x) / static_cast<float>(n);
            float v = static_cast<float>(y) / static_cast<float>(n);
            vertices[idx++] = {{{px, py, pz}}, {{0, 1, 0}}, {{1, 1, 1}}, {{u, v}}};
        }
    }

    int numIndices = (n - 1) * n * 2;
    indices.resize(numIndices);
    uint32_t* idxPtr = indices.data();
    int y;
    for (y = 0; y < n - 2; y += 2) {
        for (int x = 0; x < n; ++x) {
            *idxPtr++ = static_cast<uint32_t>(x + (y + 1) * n);
            *idxPtr++ = static_cast<uint32_t>(x + y * n);
        }
        for (int x = n - 1; x >= 0; --x) {
            *idxPtr++ = static_cast<uint32_t>(x + (y + 1) * n);
            *idxPtr++ = static_cast<uint32_t>(x + (y + 2) * n);
        }
    }
    for (int x = 0; x < n; ++x) {
        *idxPtr++ = static_cast<uint32_t>(x + (y + 1) * n);
        *idxPtr++ = static_cast<uint32_t>(x + y * n);
    }
}

}  // namespace

void computeSkyColors(float timeOfDay,
                      std::array<float, 4>& topColor,
                      std::array<float, 4>& horizonColor,
                      std::array<float, 4>& sunColorOut) {
    using Arr4 = std::array<float, 4>;

    const Arr4 nightTop       = {0.003f, 0.003f, 0.015f, 0.0f};
    const Arr4 nightHorizon   = {0.001f, 0.001f, 0.008f, 0.0f};
    const Arr4 nightSun       = {0.0f, 0.0f, 0.0f, 0.0f};

    const Arr4 sunriseTop     = {0.18f, 0.28f, 0.72f, 0.0f};
    const Arr4 sunriseHorizon = {0.92f, 0.52f, 0.28f, 0.0f};
    const Arr4 sunriseSun     = {1.0f, 0.40f, 0.05f, 0.6f};

    const Arr4 dayTop         = {0.20f, 0.45f, 0.95f, 0.0f};
    const Arr4 dayHorizon     = {0.50f, 0.75f, 0.98f, 0.0f};
    const Arr4 daySun         = {1.0f, 0.96f, 0.65f, 4.5f};

    const Arr4 sunsetTop      = {0.28f, 0.15f, 0.48f, 0.0f};
    const Arr4 sunsetHorizon  = {1.0f, 0.30f, 0.05f, 0.0f};
    const Arr4 sunsetSun      = {1.0f, 0.25f, 0.02f, 0.6f};

    float t = std::fmod(timeOfDay, 24.0f);
    if (t < 0.0f) t += 24.0f;

    if (t < 5.0f) {
        topColor = nightTop;
        horizonColor = nightHorizon;
        sunColorOut = nightSun;
    } else if (t < 6.0f) {
        float f = (t - 5.0f) / 1.0f;
        lerpColor(nightTop, sunriseTop, f, topColor);
        lerpColor(nightHorizon, sunriseHorizon, f, horizonColor);
        lerpColor(nightSun, sunriseSun, f, sunColorOut);
    } else if (t < 8.0f) {
        float f = (t - 6.0f) / 2.0f;
        lerpColor(sunriseTop, dayTop, f, topColor);
        lerpColor(sunriseHorizon, dayHorizon, f, horizonColor);
        lerpColor(sunriseSun, daySun, f, sunColorOut);
    } else if (t < 16.0f) {
        topColor = dayTop;
        horizonColor = dayHorizon;
        sunColorOut = daySun;
    } else if (t < 17.0f) {
        float f = (t - 16.0f) / 1.0f;
        lerpColor(dayTop, sunsetTop, f, topColor);
        lerpColor(dayHorizon, sunsetHorizon, f, horizonColor);
        lerpColor(daySun, sunsetSun, f, sunColorOut);
    } else if (t < 19.0f) {
        float f = (t - 17.0f) / 2.0f;
        lerpColor(sunsetTop, nightTop, f, topColor);
        lerpColor(sunsetHorizon, nightHorizon, f, horizonColor);
        lerpColor(sunsetSun, nightSun, f, sunColorOut);
    } else {
        topColor = nightTop;
        horizonColor = nightHorizon;
        sunColorOut = nightSun;
    }
}

SkydomeRenderer::SkydomeRenderer() : name_("SkydomeRenderer") {}
SkydomeRenderer::~SkydomeRenderer() {
    if (renderer_) {
        cleanup(renderer_->device());
    }
}

void SkydomeRenderer::configure(const SkydomeConfig& config, const std::string& shaderDir, const std::string& descName) {
    config_ = config;
    config_.enabled = true;
    shaderDir_ = shaderDir;
    descName_ = descName;
}

void SkydomeRenderer::init(Renderer* renderer) {
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

    const auto vertCode = vk::readFile(shaderDir_ + "/skydome.vert.spv");
    const auto fragCode = vk::readFile(shaderDir_ + "/skydome.frag.spv");

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
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttach;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(SkydomePushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw std::runtime_error("failed to create skydome pipeline layout");
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
        throw std::runtime_error("failed to create skydome pipeline");
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    generateDomeGeometry(vertices, indices);
    indexCount_ = static_cast<uint32_t>(indices.size());

    VkDeviceSize vbSize = sizeof(Vertex) * vertices.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        vertices.data(), vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer_, vertexBufferMemory_);

    VkDeviceSize ibSize = sizeof(uint32_t) * indices.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        indices.data(), ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexBuffer_, indexBufferMemory_);

    ready_ = true;
}

void SkydomeRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) {
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

    // Dynamically advance time of day and compute sun elevation for dynamic day/night cycle
    config_.timeOfDay = std::fmod(11.0f + time_ * 0.05f, 24.0f);
    if (config_.timeOfDay < 0.0f) config_.timeOfDay += 24.0f;
    
    float hourAngle = (config_.timeOfDay - 6.0f) / 12.0f * PI;
    float tiltRad = 35.0f * PI / 180.0f;
    float y = std::sin(hourAngle) * std::cos(tiltRad);
    config_.sunElevation = std::asin(y) * 180.0f / PI;
    config_.sunAzimuth = std::atan2(std::sin(hourAngle) * std::sin(tiltRad), -std::cos(hourAngle)) * 180.0f / PI;

    VkDescriptorSet cloudDescSet = renderer_->getTextureDescriptorSet(descName_, frameIndex);
    recordDrawCommands(cmd, view.cameraPosition, time_, cloudDescSet, config_);
}

void SkydomeRenderer::cleanup(VkDevice device) {
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

void SkydomeRenderer::recordDrawCommands(
    VkCommandBuffer commandBuffer,
    const Vec3& cameraPosition,
    float time,
    VkDescriptorSet cloudDescriptorSet,
    const SkydomeConfig& config
) const {
    if (!ready_ || !enabled_) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &cloudDescriptorSet, 0, nullptr);

    std::array<float, 4> topCol, horCol, sunCol;
    computeSkyColors(config.timeOfDay, topCol, horCol, sunCol);

    Vec3 sunDir = getSunDirection();
    float sunDirX = sunDir.x;
    float sunDirY = sunDir.y;
    float sunDirZ = sunDir.z;

    SkydomePushConstants push{};
    push.cameraPosition[0] = cameraPosition.x;
    push.cameraPosition[1] = cameraPosition.y;
    push.cameraPosition[2] = cameraPosition.z;
    push.cameraPosition[3] = 0.0f;
    push.time = time;
    push.domeScale = config.domeScale;
    push.cloudCoverage = config.cloudCoverage;
    push.cloudSpeed0 = config.cloudSpeed0;
    push.cloudSpeed1 = config.cloudSpeed1;
    push.cloudScale0 = config.cloudScale0;
    push.cloudScale1 = config.cloudScale1;
    push.sunDirX = sunDirX;
    push.sunDirY = sunDirY;
    push.sunDirZ = sunDirZ;
    push.sunSize = 120.0f;
    push.skyTopColor[0] = topCol[0]; push.skyTopColor[1] = topCol[1];
    push.skyTopColor[2] = topCol[2]; push.skyTopColor[3] = topCol[3];
    push.skyHorizonColor[0] = horCol[0]; push.skyHorizonColor[1] = horCol[1];
    push.skyHorizonColor[2] = horCol[2]; push.skyHorizonColor[3] = horCol[3];
    push.sunColor[0] = sunCol[0]; push.sunColor[1] = sunCol[1];
    push.sunColor[2] = sunCol[2]; push.sunColor[3] = sunCol[3];

    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(SkydomePushConstants), &push);

    VkBuffer vb[] = {vertexBuffer_};
    VkDeviceSize offset[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
}

Vec3 SkydomeRenderer::getSunDirection() const {
    float hourAngle = (config_.timeOfDay - 6.0f) / 12.0f * PI;
    float tiltRad = 35.0f * PI / 180.0f;
    float x = -std::cos(hourAngle);
    float y = std::sin(hourAngle) * std::cos(tiltRad);
    float z = std::sin(hourAngle) * std::sin(tiltRad);
    return Vec3{x, y, z};
}

Vec3 SkydomeRenderer::getSunColor() const {
    std::array<float, 4> topCol, horCol, sunCol;
    computeSkyColors(config_.timeOfDay, topCol, horCol, sunCol);
    return Vec3{sunCol[0], sunCol[1], sunCol[2]};
}

float SkydomeRenderer::getSunIntensity() const {
    std::array<float, 4> topCol, horCol, sunCol;
    computeSkyColors(config_.timeOfDay, topCol, horCol, sunCol);
    
    // Smooth fade factor based on elevation between -5.0 and +5.0 degrees
    float elevationFactor = std::clamp((config_.sunElevation + 5.0f) / 10.0f, 0.0f, 1.0f);
    elevationFactor = elevationFactor * elevationFactor * (3.0f - 2.0f * elevationFactor); // Smoothstep

    return sunCol[3] * elevationFactor;
}

float SkydomeRenderer::getAmbientStrength() const {
    // Ambient fades out between -12.0 and +10.0 degrees sun elevation
    float ambientFactor = std::clamp((config_.sunElevation + 12.0f) / 22.0f, 0.0f, 1.0f);
    ambientFactor = ambientFactor * ambientFactor * (3.0f - 2.0f * ambientFactor); // Smoothstep
    
    float nightStrength = 0.065f;
    float dayStrength = 0.28f;
    return nightStrength + (dayStrength - nightStrength) * ambientFactor;
}

Vec3 SkydomeRenderer::getAmbientColor() const {
    float ambientFactor = std::clamp((config_.sunElevation + 12.0f) / 22.0f, 0.0f, 1.0f);
    ambientFactor = ambientFactor * ambientFactor * (3.0f - 2.0f * ambientFactor); // Smoothstep
    
    Vec3 nightColor{0.08f, 0.10f, 0.18f};
    Vec3 dayColor{0.35f, 0.38f, 0.45f};
    
    return Vec3{
        nightColor.x + (dayColor.x - nightColor.x) * ambientFactor,
        nightColor.y + (dayColor.y - nightColor.y) * ambientFactor,
        nightColor.z + (dayColor.z - nightColor.z) * ambientFactor
    };
}

}  // namespace engine3d
