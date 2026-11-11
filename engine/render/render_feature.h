#pragma once

#include <vulkan/vulkan.h>
#include "render/render_types.h"
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
    virtual void preRender(VkCommandBuffer /*cmd*/, uint32_t /*frameIndex*/, const RenderView& /*view*/) {}
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
    // Czy feature czyta kopię koloru sceny (odbicia/załamania, np. ocean). Gdy żaden aktywny jej nie potrzebuje,
    // renderer pomija kopię obrazu ekranu (~8 MB na klatkę przy 1080p).
    virtual bool needsSceneColor() const { return false; }
};

} // namespace engine3d
