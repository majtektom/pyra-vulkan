#include "feature/gui/gui_renderer.h"
#include "render/vulkan_util.h"
#include <cstring>
#include <stdexcept>

namespace engine3d {

VkVertexInputBindingDescription GuiVertex::bindingDescription() {
    VkVertexInputBindingDescription d{}; d.binding = 0; d.stride = sizeof(GuiVertex); d.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return d;
}
std::array<VkVertexInputAttributeDescription, 3> GuiVertex::attributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> a{};
    a[0].binding = 0; a[0].location = 0; a[0].format = VK_FORMAT_R32G32B32_SFLOAT; a[0].offset = offsetof(GuiVertex, pos);
    a[1].binding = 0; a[1].location = 1; a[1].format = VK_FORMAT_R32G32_SFLOAT; a[1].offset = offsetof(GuiVertex, texCoord);
    a[2].binding = 0; a[2].location = 2; a[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; a[2].offset = offsetof(GuiVertex, color);
    return a;
}

void GuiRenderer::init(VkDevice d, VkPhysicalDevice p, VkFormat colorFormat, VkFormat depthFormat, VkCommandPool cp, VkQueue q, VkExtent2D) {
    device_ = d; pd_ = p; cp_ = cp; q_ = q;

    std::string dir = "bin/assets/shaders/text_overlay/";
    auto vc = vk::readFile(dir + "text.vert.spv");
    auto fc = vk::readFile(dir + "text.frag.spv");
    auto vm = vk::createShaderModule(d, vc);
    auto fm = vk::createShaderModule(d, fc);

    VkPipelineShaderStageCreateInfo ss[2] = {};
    ss[0].sType = ss[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ss[0].stage = VK_SHADER_STAGE_VERTEX_BIT; ss[0].module = vm; ss[0].pName = "vertexMain";
    ss[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; ss[1].module = fm; ss[1].pName = "fragmentMain";

    auto bd = GuiVertex::bindingDescription(); auto ad = GuiVertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bd;
    vi.vertexAttributeDescriptionCount = (uint32_t)ad.size(); vi.pVertexAttributeDescriptions = ad.data();

    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState ba{};
    ba.blendEnable = VK_TRUE; ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.colorBlendOp = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; ba.alphaBlendOp = VK_BLEND_OP_ADD;
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &ba;

    std::vector<VkDynamicState> dyn = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = (uint32_t)dyn.size(); dy.pDynamicStates = dyn.data();

    VkDescriptorSetLayoutBinding lb{}; lb.binding = 0; lb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    lb.descriptorCount = 1; lb.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo sl{}; sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.bindingCount = 1; sl.pBindings = &lb;
    vkCreateDescriptorSetLayout(d, &sl, nullptr, &dsl_);

    VkPipelineLayoutCreateInfo pl{}; pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1; pl.pSetLayouts = &dsl_;
    vkCreatePipelineLayout(d, &pl, nullptr, &pipelineLayout_);

    VkGraphicsPipelineCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2; pi.pStages = ss; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp; pi.pRasterizationState = &rs; pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds; pi.pColorBlendState = &cb; pi.pDynamicState = &dy;
    pi.layout = pipelineLayout_; pi.renderPass = VK_NULL_HANDLE; pi.subpass = 0;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;
    pi.pNext = &renderingInfo;

    vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_);

    vkDestroyShaderModule(d, fm, nullptr); vkDestroyShaderModule(d, vm, nullptr);

    createWhiteTexture();
    createDescriptorSet();
    ready_ = true;
}

void GuiRenderer::createWhiteTexture() {
    uint32_t w = 0xFFFFFFFF;
    VkBuffer sb; VkDeviceMemory sm;
    vk::createBuffer(device_, pd_, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sb, sm);
    void* m; vkMapMemory(device_, sm, 0, 4, 0, &m); std::memcpy(m, &w, 4); vkUnmapMemory(device_, sm);
    vk::createImage(device_, pd_, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, whiteImage_, whiteMem_);
    VkCommandBufferAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cp_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(device_, &ai, &cb);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    VkImageMemoryBarrier bar{}; bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; bar.image = whiteImage_;
    bar.srcAccessMask = 0; bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; bar.subresourceRange.levelCount = 1; bar.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    VkBufferImageCopy r{}; r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; r.imageSubresource.layerCount = 1; r.imageExtent = {1,1,1};
    vkCmdCopyBufferToImage(cb, sb, whiteImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(q_, 1, &si, VK_NULL_HANDLE); vkQueueWaitIdle(q_);
    vkFreeCommandBuffers(device_, cp_, 1, &cb);
    vkDestroyBuffer(device_, sb, nullptr); vkFreeMemory(device_, sm, nullptr);
    whiteView_ = vk::createImageView(device_, whiteImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    sampler_ = vk::createClampSampler(device_);
}

void GuiRenderer::createDescriptorSet() {
    VkDescriptorPoolSize ps{}; ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = 1; pi.pPoolSizes = &ps; pi.maxSets = 1;
    vkCreateDescriptorPool(device_, &pi, nullptr, &dp_);
    VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = dp_; ai.descriptorSetCount = 1; ai.pSetLayouts = &dsl_;
    vkAllocateDescriptorSets(device_, &ai, &ds_);
    VkDescriptorImageInfo ii{}; ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; ii.imageView = whiteView_; ii.sampler = sampler_;
    VkWriteDescriptorSet w{}; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = ds_; w.dstBinding = 0; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1; w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

void GuiRenderer::addRect(float x, float y, float w, float h, std::array<float,4> c, float z) {
    float actualZ = (z == -999.0f) ? currentDepth_ : z;
    GuiVertex v;
    v.texCoord[0] = 0.5f; v.texCoord[1] = 0.5f;
    v.color[0] = c[0]; v.color[1] = c[1]; v.color[2] = c[2]; v.color[3] = c[3];
    v.pos[0] = x; v.pos[1] = y; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x+w; v.pos[1] = y; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x+w; v.pos[1] = y+h; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x; v.pos[1] = y; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x+w; v.pos[1] = y+h; v.pos[2] = actualZ; vertices_.push_back(v);
    v.pos[0] = x; v.pos[1] = y+h; v.pos[2] = actualZ; vertices_.push_back(v);
}

void GuiRenderer::addText(const std::vector<GuiVertex>& verts) {
    vertices_.insert(vertices_.end(), verts.begin(), verts.end());
}

void GuiRenderer::build(uint32_t fi) {
    if (vertices_.empty()) { vc_ = 0; return; }
    vc_ = (uint32_t)vertices_.size();
    VkDeviceSize sz = sizeof(GuiVertex) * vertices_.size();
    ensureBuffer(sz, fi);
    void* m; vkMapMemory(device_, vbm_[fi], 0, sz, 0, &m);
    std::memcpy(m, vertices_.data(), (size_t)sz);
    vkUnmapMemory(device_, vbm_[fi]);
    vertices_.clear();
}

void GuiRenderer::ensureBuffer(VkDeviceSize sz, uint32_t fi) {
    if (vb_[fi] && sz <= cap_[fi]) return;
    if (vb_[fi]) { vkDestroyBuffer(device_, vb_[fi], nullptr); vkFreeMemory(device_, vbm_[fi], nullptr); }
    cap_[fi] = sz;
    vk::createBuffer(device_, pd_, sz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vb_[fi], vbm_[fi]);
}

void GuiRenderer::recordDrawCommands(VkCommandBuffer cmd, uint32_t fi) {
    if (vc_ == 0) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &ds_, 0, nullptr);
    VkBuffer b[] = {vb_[fi]}; VkDeviceSize o[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, b, o);
    vkCmdDraw(cmd, vc_, 1, 0, 0);
}

void GuiRenderer::cleanup() {
    for (uint32_t i = 0; i < kGuiMaxFrames; i++) {
        if (vb_[i]) { vkDestroyBuffer(device_, vb_[i], nullptr); vkFreeMemory(device_, vbm_[i], nullptr); vb_[i] = VK_NULL_HANDLE; }
    }
    ds_ = VK_NULL_HANDLE; // zwalniany razem z pulą
    if (dp_) { vkDestroyDescriptorPool(device_, dp_, nullptr); dp_ = VK_NULL_HANDLE; }
    if (sampler_) { vkDestroySampler(device_, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
    if (whiteView_) { vkDestroyImageView(device_, whiteView_, nullptr); whiteView_ = VK_NULL_HANDLE; }
    if (whiteImage_) { vkDestroyImage(device_, whiteImage_, nullptr); vkFreeMemory(device_, whiteMem_, nullptr); whiteImage_ = VK_NULL_HANDLE; }
    if (pipeline_) { vkDestroyPipeline(device_, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_) { vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if (dsl_) { vkDestroyDescriptorSetLayout(device_, dsl_, nullptr); dsl_ = VK_NULL_HANDLE; }
    ready_ = false;
}

} // namespace engine3d
