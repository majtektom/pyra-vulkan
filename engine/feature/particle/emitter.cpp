#include "feature/particle/emitter.h"
#include "feature/particle/particle_event.h"
#include "feature/particle/particle.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdlib>
#include "core/job_system.h"

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

// Krok jednej cząsteczki (żywej, jeszcze nie za starej): ruch, grawitacja, płynne zmiany parametrów i zdarzenia.
// Wspólny dla wersji jednowątkowej i równoległej - każda cząsteczka zmienia tylko siebie.
void ParticleEmitter::stepParticle(Particle* par, float time, const Vec3& grav) const {
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
         ilist != endEvents_ && par->ageEvent > (*ilist)->GetTime(); ++ilist) {
        (*ilist)->RunEvent(par);
        par->iter_event = ilist;
    }
}

// Krok cząsteczek o indeksach [begin, end). Za stare zostają oznaczone wiekiem (usuwa je removeExpired).
void ParticleEmitter::stepRange(size_t begin, size_t end, float time) {
    for (size_t i = begin; i < end; ++i) {
        if (!particles_->IsAlive(static_cast<int>(i))) continue;
        Particle* par = &particles_->GetAt(static_cast<int>(i));
        par->age += time;
        if (par->age < par->lifeTime) stepParticle(par, time, gravity_);
    }
}

// Usunięcie cząsteczek, które przeżyły swój czas (zmienia wspólną listę - zawsze na jednym wątku).
void ParticleEmitter::removeExpired() {
    for (int i = 0; i < particles_->GetTotalElements(); ++i) {
        if (particles_->IsAlive(i) && particles_->GetAt(i).age >= particles_->GetAt(i).lifeTime) particles_->Delete(i);
    }
}

// Nowe cząsteczki wg tempa emisji (ułamki przechodzą na następne klatki).
void ParticleEmitter::spawn(float time) {
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

void ParticleEmitter::update(float time) {
    if (!particles_) return;
    stepRange(0, static_cast<size_t>(particles_->GetTotalElements()), time);
    removeExpired();
    spawn(time);
}

// Ruch cząsteczek na wątkach puli silnika (JobSystem). Każdy kawałek tablicy zmienia tylko swoje cząsteczki, więc
// wątki sobie nie przeszkadzają; usuwanie martwych i tworzenie nowych (zmiana wspólnej listy) - na wątku wołającym.
void ParticleEmitter::updateParallel(float time, JobSystem* jobs) {
    if (!particles_) return;
    const int totalElements = particles_->GetTotalElements();
    if (!jobs || jobs->threadCount() < 2 || totalElements < 512) {
        update(time);
        return;
    }
    const size_t chunkSize = std::max<size_t>(256, (static_cast<size_t>(totalElements) + jobs->threadCount() - 1) / jobs->threadCount());
    jobs->parallelFor(static_cast<size_t>(totalElements), [this, time](size_t b, size_t e) { stepRange(b, e, time); }, chunkSize);
    removeExpired();
    spawn(time);
}

// Dane jednej instancji (billboard cząsteczki) dla GPU.
static ParticleInstanceData instanceOf(const Particle& p) {
    return {p.pos.x, p.pos.y, p.pos.z, p.size, p.color[0], p.color[1], p.color[2], p.color[3]};
}

void ParticleEmitter::fillInstanceData(std::vector<ParticleInstanceData>& out) {
    out.clear();
    if (!particles_) return;
    out.reserve(static_cast<size_t>(particles_->GetNumUsedElements()));
    for (int i = 0; i < particles_->GetTotalElements(); ++i) {
        if (particles_->IsAlive(i)) out.push_back(instanceOf(particles_->GetAt(i)));
    }
}

void ParticleEmitter::fillInstanceDataParallel(std::vector<ParticleInstanceData>& out, JobSystem* jobs) {
    out.clear();
    if (!particles_) return;
    const int alive = particles_->GetNumUsedElements();
    if (alive == 0) return;
    if (!jobs || jobs->threadCount() < 2 || alive < 1000) {
        fillInstanceData(out);
        return;
    }
    activeIdx_.clear();   // pamięć wektora zostaje między klatkami
    for (int i = 0; i < particles_->GetTotalElements(); ++i) {
        if (particles_->IsAlive(i)) activeIdx_.push_back(i);
    }
    out.resize(static_cast<size_t>(alive));
    const size_t chunkSize = std::max<size_t>(512, (static_cast<size_t>(alive) + jobs->threadCount() - 1) / jobs->threadCount());
    jobs->parallelFor(static_cast<size_t>(alive), [this, &out](size_t b, size_t e) {
        for (size_t j = b; j < e; ++j) out[j] = instanceOf(particles_->GetAt(activeIdx_[j]));
    }, chunkSize);
}

} // namespace engine3d::particles
