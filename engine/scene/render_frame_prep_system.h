#pragma once

#include "core/job_system.h"
#include "scene/scene.h"

namespace engine3d {

// System ECS przygotowujacy paczki renderingu do sortowania i passow.
// Używa puli wątków do równoległego obliczania dystansu do kamery.
class RenderFramePrepSystem final : public ISceneSystem {
public:
    explicit RenderFramePrepSystem(JobSystem& jobs) : jobs_(jobs) {}

    // Uzupelnia klucze sortowania (distanceToCamera) w packetach sceny - wielowątkowo.
    void update(Scene& scene, float deltaTime) override;

    // Nazwa systemu do logowania/profilowania.
    const char* name() const override { return "RenderFramePrepSystem"; }

private:
    JobSystem& jobs_;
};

} // namespace engine3d
