#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>
#include "render/renderer.h"
#include "render/pipeline_builder.h"
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

void generateDomeGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    constexpr int n = 30;
    constexpr float r = 500.0f;
    constexpr float rh = 10000.0f;
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
    VkDescriptorSetLayout descriptorSetLayout = renderer_->descriptorSetLayout();
    VkFormat colorFormat = renderer_->colorFormat();
    VkFormat depthFormat = renderer_->depthFormat();

    // Kopuła oglądana od środka: bez cullingu, głębokość tylko testowana (LEQUAL) - niebo zostaje w tle.
    const vk::ShaderModule vert(device, shaderDir_ + "/skydome.vert.spv");
    const vk::ShaderModule frag(device, shaderDir_ + "/skydome.frag.spv");

    pipelineLayout_ = vk::createPipelineLayout(device, {descriptorSetLayout}, sizeof(SkydomePushConstants),
                                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, "skydome");
    pipeline_ = vk::GraphicsPipelineBuilder()
        .shaders(vert, frag)
        .vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()))
        .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
        .polygonMode(renderer_->isWireframeEnabled() ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL)
        .cull(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE)
        .depth(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
        .colorFormat(colorFormat)
        .depthFormat(depthFormat)
        .build(device, pipelineLayout_, "skydome");

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

    // Upływ pory dnia i wysokość słońca (cykl dnia i nocy)
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

// Barwa słońca z tabeli pory dnia (biała w dzień, pomarańczowa o świcie i zmierzchu), znormalizowana do
// jasności 1 - o sile światła decyduje getSunIntensity() (wysokość słońca), a nie przyciemniony kolor.
Vec3 SkydomeRenderer::getSunColor() const {
    std::array<float, 4> topCol, horCol, sunCol;
    computeSkyColors(config_.timeOfDay, topCol, horCol, sunCol);
    const float m = std::max({sunCol[0], sunCol[1], sunCol[2]});
    if (m < 1e-3f) return Vec3{1.0f, 0.25f, 0.02f};   // noc: barwa zachodu (siła i tak bliska zeru)
    return Vec3{sunCol[0] / m, sunCol[1] / m, sunCol[2] / m};
}

// Siła słońca zależy od jego wysokości nad horyzontem: pełna od ~18°, łagodnie słabnie aż do horyzontu
// (-2°). Wcześniej siła szła za godziną z tabeli i spadała 7x między 16 a 17 (słońce wciąż ~12° nad
// horyzontem) - cienie gasły długo przed zachodem.
float SkydomeRenderer::getSunIntensity() const {
    constexpr float kDayIntensity = 4.5f;
    float k = std::clamp((config_.sunElevation + 2.0f) / 20.0f, 0.0f, 1.0f);
    k = k * k * (3.0f - 2.0f * k);   // smoothstep
    return kDayIntensity * k;
}

float SkydomeRenderer::getAmbientStrength() const {
    // Światło otoczenia gaśnie, gdy słońce schodzi z +10 do -12 stopni
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
