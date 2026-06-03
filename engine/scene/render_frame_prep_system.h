#pragma once

#include "scene/scene.h"

namespace engine3d {

// System ECS przygotowujacy paczki renderingu do sortowania i passow.
class RenderFramePrepSystem final : public ISceneSystem {
public:
    // Uzupelnia klucze sortowania (distanceToCamera) w packetach sceny.
    void update(Scene& scene, float deltaTime) override;

    // Nazwa systemu do logowania/profilowania.
    const char* name() const override { return "RenderFramePrepSystem"; }
};

} // namespace engine3d
