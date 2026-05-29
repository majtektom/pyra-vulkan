#pragma once

#include <vulkan/vulkan.h>
#include "render_types.h"
#include <string>
#include <memory>

namespace engine3d {

class Renderer;

/**
 * Interfejs dla dodatkowych cech renderowania (Skybox, Grid, UI itp.)
 * Pozwala na rozszerzanie potoku renderowania bez modyfikacji klasy Engine.
 */
class IRenderFeature {
public:
    virtual ~IRenderFeature() = default;

    virtual void init(Renderer* renderer) = 0;
    virtual void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) = 0;
    virtual bool isActive() const = 0;
    virtual const std::string& getName() const = 0;
    
    // Określa, kiedy feature ma być wywołany
    enum class Stage {
        BeforeOpaque,
        AfterOpaque,
        AfterTransparent,
        Overlay
    };
    virtual Stage getStage() const { return Stage::AfterOpaque; }
};

} // namespace engine3d
