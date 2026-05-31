#include "feature/particle/particle_event.h"
#include "feature/particle/particle.h"
#include <cmath>
#include <cstdlib>

namespace engine3d::particles {

static float randFloat(float min, float max) {
    if (min >= max) return min;
    return min + static_cast<float>(std::rand()) / RAND_MAX * (max - min);
}

void ParticleEvent::RunEvent(Particle* p) {
    float d = randFloat(min_duration, max_duration);
    if (d > 0.001f) {
        switch (type) {
        case EVENT_SIZE:
            p->size_step = (randFloat(min_1, max_1) - p->size) / d;
            p->size_time = d;
            break;
        case EVENT_MASS:
            p->mass_step = (randFloat(min_1, max_1) - p->mass) / d;
            p->mass_time = d;
            break;
        case EVENT_COLOR: {
            std::array<float, 4> target;
            target[0] = randFloat(min_1, max_1);
            target[1] = randFloat(min_2, max_2);
            target[2] = randFloat(min_3, max_3);
            target[3] = randFloat(min_4, max_4);
            p->color_step[0] = (target[0] - p->color[0]) / d;
            p->color_step[1] = (target[1] - p->color[1]) / d;
            p->color_step[2] = (target[2] - p->color[2]) / d;
            p->color_step[3] = (target[3] - p->color[3]) / d;
            p->color_time = d;
            break;
        }
        case EVENT_POSITION: {
            Vec3 target;
            target.x = randFloat(min_1, max_1);
            target.y = randFloat(min_2, max_2);
            target.z = randFloat(min_3, max_3);
            p->pos_step.x = (target.x - p->pos.x) / d;
            p->pos_step.y = (target.y - p->pos.y) / d;
            p->pos_step.z = (target.z - p->pos.z) / d;
            p->pos_time = d;
            break;
        }
        case EVENT_VELOCITY: {
            Vec3 target;
            target.x = randFloat(min_1, max_1);
            target.y = randFloat(min_2, max_2);
            target.z = randFloat(min_3, max_3);
            p->velocity_step.x = (target.x - p->velocity.x) / d;
            p->velocity_step.y = (target.y - p->velocity.y) / d;
            p->velocity_step.z = (target.z - p->velocity.z) / d;
            p->velocity_time = d;
            break;
        }
        default: break;
        }
    } else {
        switch (type) {
        case EVENT_SIZE:   p->size = randFloat(min_1, max_1); break;
        case EVENT_MASS:   p->mass = randFloat(min_1, max_1); break;
        case EVENT_LIFETIME: p->lifeTime = randFloat(min_1, max_1); break;
        case EVENT_COLOR:
            p->color[0] = randFloat(min_1, max_1);
            p->color[1] = randFloat(min_2, max_2);
            p->color[2] = randFloat(min_3, max_3);
            p->color[3] = randFloat(min_4, max_4);
            break;
        case EVENT_POSITION:
            p->pos.x = randFloat(min_1, max_1);
            p->pos.y = randFloat(min_2, max_2);
            p->pos.z = randFloat(min_3, max_3);
            break;
        case EVENT_VELOCITY:
            p->velocity.x = randFloat(min_1, max_1);
            p->velocity.y = randFloat(min_2, max_2);
            p->velocity.z = randFloat(min_3, max_3);
            break;
        default: break;
        }
    }
}

} // namespace engine3d::particles
