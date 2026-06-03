#pragma once

#include "scene/scene.h"

namespace engine3d {

// System ECS przygotowujacy RenderView na podstawie aktywnej kamery sceny.
class CameraExtractionSystem final : public ISceneSystem {
public:
    // Przechodzi po aktywnej kamerze sceny i aktualizuje scene.frameView().
    void update(Scene& scene, float deltaTime) override;

    // Nazwa systemu do logowania/profilowania.
    const char* name() const override { return "CameraExtractionSystem"; }
};

} // namespace engine3d
