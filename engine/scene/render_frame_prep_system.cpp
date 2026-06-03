#include "scene/render_frame_prep_system.h"

#include <cmath>

namespace engine3d {

void RenderFramePrepSystem::update(Scene& scene, float /*deltaTime*/) {
    auto& packets = scene.mutableRenderPackets();
    if (!scene.hasFrameView()) {
        for (auto& packet : packets) {
            packet.distanceToCamera = 0.0f;
        }
        return;
    }

    const Vec3 cameraPos = scene.frameView().cameraPosition;
    for (auto& packet : packets) {
        Vec3 objPos = { packet.transform.values[12], packet.transform.values[13], packet.transform.values[14] };
        Vec3 diff = subtract(objPos, cameraPos);
        packet.distanceToCamera = std::sqrt(dot(diff, diff));
    }
}

} // namespace engine3d
