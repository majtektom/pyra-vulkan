#pragma once

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include "render_pass.h"

namespace engine3d {

class NewRenderer {
public:
    NewRenderer(VkDevice device, VkRenderPass renderPass);
    ~NewRenderer();

    void addPass(std::unique_ptr<IRenderPass> pass);
    
    void render(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene) {
        // Here we could start the render pass, set viewport/scissor, etc.
        // But the main point is the delegation:
        
        for (auto& pass : passes_) {
            // Logically: pass->begin(commandBuffer);
            pass->execute(commandBuffer, currentFrame, scene);
            // Logically: pass->end(commandBuffer);
        }
    }

private:
    VkDevice device_;
    VkRenderPass renderPass_;
    std::vector<std::unique_ptr<IRenderPass>> passes_;
};

} // namespace engine3d
