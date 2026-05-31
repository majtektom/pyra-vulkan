#include "feature/particle/emitter.h"
#include "feature/particle/particle_event.h"
#include "feature/particle/particle.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdlib>
#include <future>
#include <random>
#include <thread>

namespace engine3d::particles {

template<class T>
static void deleteAll(T& t) {
    for (auto i = t.begin(); i != t.end(); ++i) delete (*i);
    t.clear();
}

static float randFloat(float min, float max) {
    if (min >= max) return min;
    return min + static_cast<float>(std::rand()) / RAND_MAX * (max - min);
}

struct EventCompare {
    bool operator()(const ParticleEvent* lhs, const ParticleEvent* rhs) const {
        return lhs->GetTime() < rhs->GetTime();
    }
};

ParticleEmitter::ParticleEmitter() {
    numParticles_ = 1;
    emitRate_ = 1;
    minLife_ = maxLife_ = 3.0f;
    maxMass_ = minMass_ = 1.0f;
    minSize_ = maxSize_ = 1.0f;
    particles_ = nullptr;
}

ParticleEmitter::~ParticleEmitter() { destroy(); }

bool ParticleEmitter::createParticles() {
    newPartsExcess_ = 0;
    particles_ = new RecyclingArray<Particle>(numParticles_);
    sortEvents();
    return true;
}

bool ParticleEmitter::destroy() {
    delete particles_;
    particles_ = nullptr;
    deleteAll(events_);
    return true;
}

void ParticleEmitter::sortEvents() {
    std::sort(events_.begin(), events_.end(), EventCompare{});
    beginEvents_ = events_.begin();
    endEvents_ = events_.end();
}

void ParticleEmitter::update(float time) {
    if (!particles_) return;

    Particle* par;
    for (int i = 0; i < particles_->GetTotalElements(); ++i) {
        if (!particles_->IsAlive(i)) continue;
        par = &particles_->GetAt(i);

        par->age += time;
        if (par->age < par->lifeTime) {
            par->ageEvent += time;

            par->pos.x += par->velocity.x * time;
            par->pos.y += par->velocity.y * time;
            par->pos.z += par->velocity.z * time;
            par->pos.x += gravity_.x * par->mass * time;
            par->pos.y += gravity_.y * par->mass * time;
            par->pos.z += gravity_.z * par->mass * time;

            if (par->color_time > 0.0f) {
                par->color_time -= time;
                par->color[0] += par->color_step[0] * time;
                par->color[1] += par->color_step[1] * time;
                par->color[2] += par->color_step[2] * time;
                par->color[3] += par->color_step[3] * time;
            }
            if (par->mass_time > 0.0f) {
                par->mass_time -= time;
                par->mass += par->mass_step * time;
            }
            if (par->pos_time > 0.0f) {
                par->pos_time -= time;
                par->pos.x += par->pos_step.x * time;
                par->pos.y += par->pos_step.y * time;
                par->pos.z += par->pos_step.z * time;
            }
            if (par->size_time > 0.0f) {
                par->size_time -= time;
                par->size += par->size_step * time;
            }
            if (par->velocity_time > 0.0f) {
                par->velocity_time -= time;
                par->velocity.x += par->velocity_step.x * time;
                par->velocity.y += par->velocity_step.y * time;
                par->velocity.z += par->velocity_step.z * time;
            }

            for (EventIter ilist = par->iter_event;
                 ilist != endEvents_ && par->ageEvent > (*ilist)->GetTime(); ++ilist) {
                (*ilist)->RunEvent(par);
                par->iter_event = ilist;
            }
        } else {
            particles_->Delete(i);
        }
    }

    int newparticles = static_cast<int>(emitRate_ * time);
    newPartsExcess_ += (emitRate_ * time) - newparticles;
    if (newPartsExcess_ > 1.0f) {
        newparticles += static_cast<int>(newPartsExcess_);
        newPartsExcess_ -= static_cast<int>(newPartsExcess_);
    }

    for (int j = 0; j < newparticles; ++j) {
        par = particles_->New();
        if (!par) break;

        par->age = par->ageEvent = 0.0f;
        par->iter_event = beginEvents_;
        par->lifeTime = randFloat(minLife_, maxLife_);

        par->pos.x = randFloat(minPos_.x, maxPos_.x);
        par->pos.y = randFloat(minPos_.y, maxPos_.y);
        par->pos.z = randFloat(minPos_.z, maxPos_.z);
        par->pos_step = {0, 0, 0};
        par->pos_time = 0.0f;

        par->velocity.x = randFloat(minVel_.x, maxVel_.x);
        par->velocity.y = randFloat(minVel_.y, maxVel_.y);
        par->velocity.z = randFloat(minVel_.z, maxVel_.z);
        par->velocity_step = {0, 0, 0};
        par->velocity_time = 0.0f;

        par->mass = randFloat(minMass_, maxMass_);
        par->mass_step = 0.0f;
        par->mass_time = 0.0f;

        par->size = randFloat(minSize_, maxSize_);
        par->size_step = 0.0f;
        par->size_time = 0.0f;

        par->color[0] = randFloat(minColor_[0], maxColor_[0]);
        par->color[1] = randFloat(minColor_[1], maxColor_[1]);
        par->color[2] = randFloat(minColor_[2], maxColor_[2]);
        par->color[3] = randFloat(minColor_[3], maxColor_[3]);
        par->color_step = {0, 0, 0, 0};
        par->color_time = 0.0f;
    }
}

void ParticleEmitter::updateParallel(float time, int totalThreads) {
    if (!particles_) return;

    int totalElements = particles_->GetTotalElements();
    if (totalThreads < 2 || totalElements < 512) {
        update(time);
        return;
    }

    int chunkSize = (totalElements + totalThreads - 1) / totalThreads;
    std::vector<std::future<void>> futs;
    Vec3 grav = gravity_;

    for (int t = 0; t < totalThreads; ++t) {
        int cstart = t * chunkSize;
        int cend = std::min(cstart + chunkSize, totalElements);
        if (cstart >= cend) break;

        futs.push_back(std::async(std::launch::async, [this, time, cstart, cend, grav]() {
            thread_local std::mt19937 rng(std::random_device{}());
            for (int i = cstart; i < cend; ++i) {
                if (!particles_->IsAlive(i)) continue;
                Particle* par = &particles_->GetAt(i);

                par->age += time;
                if (par->age < par->lifeTime) {
                    par->ageEvent += time;

                    par->pos.x += par->velocity.x * time;
                    par->pos.y += par->velocity.y * time;
                    par->pos.z += par->velocity.z * time;
                    par->pos.x += grav.x * par->mass * time;
                    par->pos.y += grav.y * par->mass * time;
                    par->pos.z += grav.z * par->mass * time;

                    if (par->color_time > 0.0f) {
                        par->color_time -= time;
                        par->color[0] += par->color_step[0] * time;
                        par->color[1] += par->color_step[1] * time;
                        par->color[2] += par->color_step[2] * time;
                        par->color[3] += par->color_step[3] * time;
                    }
                    if (par->mass_time > 0.0f) {
                        par->mass_time -= time;
                        par->mass += par->mass_step * time;
                    }
                    if (par->pos_time > 0.0f) {
                        par->pos_time -= time;
                        par->pos.x += par->pos_step.x * time;
                        par->pos.y += par->pos_step.y * time;
                        par->pos.z += par->pos_step.z * time;
                    }
                    if (par->size_time > 0.0f) {
                        par->size_time -= time;
                        par->size += par->size_step * time;
                    }
                    if (par->velocity_time > 0.0f) {
                        par->velocity_time -= time;
                        par->velocity.x += par->velocity_step.x * time;
                        par->velocity.y += par->velocity_step.y * time;
                        par->velocity.z += par->velocity_step.z * time;
                    }

                    for (EventIter ilist = par->iter_event;
                         ilist != endEvents_ && par->ageEvent > (*ilist)->GetTime();
                         ++ilist) {
                        (*ilist)->RunEvent(par);
                        par->iter_event = ilist;
                    }
                }
            }
        }));
    }
    for (auto& f : futs) f.get();

    for (int i = 0; i < totalElements; ++i) {
        if (particles_->IsAlive(i)) {
            Particle* par = &particles_->GetAt(i);
            if (par->age >= par->lifeTime) {
                particles_->Delete(i);
            }
        }
    }

    int newparticles = static_cast<int>(emitRate_ * time);
    newPartsExcess_ += (emitRate_ * time) - newparticles;
    if (newPartsExcess_ > 1.0f) {
        newparticles += static_cast<int>(newPartsExcess_);
        newPartsExcess_ -= static_cast<int>(newPartsExcess_);
    }

    for (int j = 0; j < newparticles; ++j) {
        Particle* par = particles_->New();
        if (!par) break;

        par->age = par->ageEvent = 0.0f;
        par->iter_event = beginEvents_;
        par->lifeTime = randFloat(minLife_, maxLife_);

        par->pos.x = randFloat(minPos_.x, maxPos_.x);
        par->pos.y = randFloat(minPos_.y, maxPos_.y);
        par->pos.z = randFloat(minPos_.z, maxPos_.z);
        par->pos_step = {0, 0, 0};
        par->pos_time = 0.0f;

        par->velocity.x = randFloat(minVel_.x, maxVel_.x);
        par->velocity.y = randFloat(minVel_.y, maxVel_.y);
        par->velocity.z = randFloat(minVel_.z, maxVel_.z);
        par->velocity_step = {0, 0, 0};
        par->velocity_time = 0.0f;

        par->mass = randFloat(minMass_, maxMass_);
        par->mass_step = 0.0f;
        par->mass_time = 0.0f;

        par->size = randFloat(minSize_, maxSize_);
        par->size_step = 0.0f;
        par->size_time = 0.0f;

        par->color[0] = randFloat(minColor_[0], maxColor_[0]);
        par->color[1] = randFloat(minColor_[1], maxColor_[1]);
        par->color[2] = randFloat(minColor_[2], maxColor_[2]);
        par->color[3] = randFloat(minColor_[3], maxColor_[3]);
        par->color_step = {0, 0, 0, 0};
        par->color_time = 0.0f;
    }
}

void ParticleEmitter::fillInstanceData(std::vector<ParticleInstanceData>& out) {
    out.clear();
    if (!particles_) return;

    int alive = particles_->GetNumUsedElements();
    if (alive == 0) return;
    out.reserve(alive);

    for (int i = 0; i < particles_->GetTotalElements(); ++i) {
        if (!particles_->IsAlive(i)) continue;
        Particle* par = &particles_->GetAt(i);
        ParticleInstanceData inst;
        inst.posX = par->pos.x;
        inst.posY = par->pos.y;
        inst.posZ = par->pos.z;
        inst.size = par->size;
        inst.r = par->color[0];
        inst.g = par->color[1];
        inst.b = par->color[2];
        inst.a = par->color[3];
        out.push_back(inst);
    }
}

void ParticleEmitter::fillInstanceDataParallel(std::vector<ParticleInstanceData>& out, int threadCount) {
    out.clear();
    if (!particles_) return;

    int alive = particles_->GetNumUsedElements();
    if (alive == 0) return;

    if (threadCount < 2 || alive < 1000) {
        fillInstanceData(out);
        return;
    }

    std::vector<int> activeIdx;
    activeIdx.reserve(alive);
    for (int i = 0; i < particles_->GetTotalElements(); ++i) {
        if (particles_->IsAlive(i)) activeIdx.push_back(i);
    }

    out.resize(alive);
    int chunkSize = (alive + threadCount - 1) / threadCount;
    std::vector<std::future<void>> futs;

    for (int t = 0; t < threadCount; t++) {
        int cstart = t * chunkSize;
        int cend = std::min(cstart + chunkSize, alive);
        if (cstart >= cend) break;

        futs.push_back(std::async(std::launch::async, [&, cstart, cend]() {
            for (int j = cstart; j < cend; j++) {
                Particle* par = &particles_->GetAt(activeIdx[j]);
                ParticleInstanceData& inst = out[j];
                inst.posX = par->pos.x;
                inst.posY = par->pos.y;
                inst.posZ = par->pos.z;
                inst.size = par->size;
                inst.r = par->color[0];
                inst.g = par->color[1];
                inst.b = par->color[2];
                inst.a = par->color[3];
            }
        }));
    }
    for (auto& f : futs) f.get();
}

} // namespace engine3d::particles
