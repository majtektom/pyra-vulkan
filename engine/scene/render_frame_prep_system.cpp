#include "scene/render_frame_prep_system.h"

#include <cmath>
#include "core/job_system.h"

namespace engine3d {

void RenderFramePrepSystem::update(Scene& scene, float /*deltaTime*/) {
    auto& packets = scene.mutableRenderPackets();
    const size_t count = packets.size();

    if (!scene.hasFrameView()) {
        // Zerowanie dystansów
        if (count < 256) {
            for (size_t i = 0; i < count; ++i)
                packets[i].distanceToCamera = 0.0f;
        } else {
            jobs_.parallelFor(count, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i)
                    packets[i].distanceToCamera = 0.0f;
            });
        }
        return;
    }

    const Vec3 cameraPos = scene.frameView().cameraPosition;

    // Obliczanie kwadratu odległości do kamery (unikamy std::sqrt, sortowanie zachowuje tę samą kolejność)
    if (count < 256) {
        for (size_t i = 0; i < count; ++i) {
            const auto& t = packets[i].transform;
            Vec3 objPos = { t.values[12], t.values[13], t.values[14] };
            Vec3 diff = subtract(objPos, cameraPos);
            packets[i].distanceToCamera = dot(diff, diff);
        }
    } else {
        jobs_.parallelFor(count, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                const auto& t = packets[i].transform;
                Vec3 objPos = { t.values[12], t.values[13], t.values[14] };
                Vec3 diff = subtract(objPos, cameraPos);
                packets[i].distanceToCamera = dot(diff, diff);
            }
        });
    }
}

} // namespace engine3d
