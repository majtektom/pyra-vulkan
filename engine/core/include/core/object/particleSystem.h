#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "engine/Math.hpp"
#include "engine/Types.hpp"
#include "object.h"

namespace engine3d {

enum EVENT_TYPES {
    PARTICLE_EVENT_SIZE = 0,
    PARTICLE_EVENT_MASS,
    PARTICLE_EVENT_LIFETIME,
    PARTICLE_EVENT_COLOR,
    PARTICLE_EVENT_POSITION,
    PARTICLE_EVENT_VELOCITY
};

template <class T>
class RecyclingArray {
public:
    RecyclingArray(int numElements)
        : m_iNumElements(numElements) {
        m_bAlive = new bool[numElements];
        m_aElements = new T[numElements];
        for (int q = 0; q < numElements; q++)
            m_bAlive[q] = false;
        m_iUsedElements = 0;
        m_iLastNew = 0;
    }

    virtual ~RecyclingArray() {
        delete[] m_bAlive;
        delete[] m_aElements;
    }

    T* New() {
        if (GetNumFreeElements() < 1) return nullptr;
        int i = m_iLastNew;
        for (int q = 0; q < m_iNumElements; q++) {
            if (!m_bAlive[i]) break;
            i++;
            if (i >= m_iNumElements) i = 0;
        }
        if (m_bAlive[i]) return nullptr;
        m_aElements[i] = m_NewTemplate;
        m_iUsedElements++;
        m_bAlive[i] = true;
        m_iLastNew = i;
        return &m_aElements[i];
    }

    bool Delete(int index) {
        if (index < 0 || index >= m_iNumElements || !m_bAlive[index])
            return false;
        m_bAlive[index] = false;
        m_iUsedElements--;
        return true;
    }

    int GetNumFreeElements() { return m_iNumElements - GetNumUsedElements(); }
    int GetNumUsedElements() { return m_iUsedElements; }
    int GetTotalElements() { return m_iNumElements; }
    T& GetAt(int index) { return m_aElements[index]; }
    bool IsAlive(int index) { return m_bAlive[index]; }

    void DeleteAll() {
        for (int q = 0; q < m_iNumElements; q++)
            m_bAlive[q] = false;
        m_iUsedElements = 0;
        m_iLastNew = 0;
    }

protected:
    int m_iNumElements;
    int m_iUsedElements;
    T* m_aElements;
    bool* m_bAlive;
    int m_iLastNew;
    T m_NewTemplate;
};

class CParticleSystem {
    class CParticle;
    class CListParticles;

    class CParticleEvent {
        float time;
        EVENT_TYPES type;
        float m_min_duration, m_max_duration;
        float m_min_1, m_max_1;
        float m_min_2, m_max_2;
        float m_min_3, m_max_3;
        float m_min_4, m_max_4;
    public:
        ~CParticleEvent() {}
        float GetTime() { return time; }
        void RunEvent(CParticle* p);
        friend CParticleSystem;
        friend CListParticles;
    };

    struct CParticleEvent_Func {
        bool operator()(CParticleEvent* lhs, CParticleEvent* rhs) const {
            return lhs->GetTime() < rhs->GetTime();
        }
    };

    typedef std::vector<CParticleEvent*> ListEvent;
    typedef std::vector<CParticleEvent*>::iterator ListEventIter;

    class CListParticles;

    class CParticle {
    public:
        Vec3 m_pos;
        Vec3 m_pos_step;
        float m_pos_time = 0;
        Vec3 m_velocity;
        Vec3 m_velocity_step;
        float m_velocity_time = 0;
        std::array<float, 4> m_color{1, 1, 1, 1};
        std::array<float, 4> m_color_step{0, 0, 0, 0};
        float m_color_time = 0;
        float m_size = 1;
        float m_size_step = 0;
        float m_size_time = 0;
        float m_mass = 1;
        float m_mass_step = 0;
        float m_mass_time = 0;
        float m_lifeTime = 3;
        float m_age = 0;
        float m_ageEvent = 0;
        ListEventIter m_iter_event;
        friend CListParticles;
        friend CParticleEvent;
    };

    struct RealNumber { float max, min; };
    struct IntNumber { int max, min; };
    struct Vec3Number { Vec3 min, max; };
    struct Vec4Number { std::array<float, 4> min, max; };

    class CListParticles {
        struct vs_particle {
            float x, y, z;
            float cr, cg, cb, ca;
            float Ts, Tt;
        };

        ListEvent m_listEvent;
        ListEventIter m_BeginListEvent, m_EndListEvent;
        std::string m_name;
        std::string m_texturename;
        int m_numparticles = 1;
        float newPartsExcess = 0;
        Vec3 m_gravitation{0, 0, 0};
        int m_emitrate = 1;
        float m_min_lifeTime = 3, m_max_lifeTime = 3;
        float m_min_size = 1, m_max_size = 1;
        Vec3 m_min_position{0, 0, 0}, m_max_position{0, 0, 0};
        Vec3 m_min_velocity{0, 0, 0}, m_max_velocity{0, 0, 0};
        std::array<float, 4> m_min_color{1, 1, 1, 1}, m_max_color{1, 1, 1, 1};
        float m_min_mass = 1, m_max_mass = 1;

        RecyclingArray<CParticle>* particles = nullptr;

    public:
        CListParticles();
        ~CListParticles();
        bool createParticles();
        bool destroy();
        void update(float time, const Vec3& camRight, const Vec3& camUp);
        void buildMeshData(MeshData& meshData, int& particleCount);

        void setName(const std::string& n) { m_name = n; }
        void setTextureName(const std::string& t) { m_texturename = t; }
        void setNumParticles(int n) { m_numparticles = n; }
        void setEmitRate(int r) { m_emitrate = r; }
        void setGravity(const Vec3& g) { m_gravitation = g; }
        void setPosition(const Vec3& min, const Vec3& max) { m_min_position = min; m_max_position = max; }
        void setVelocity(const Vec3& min, const Vec3& max) { m_min_velocity = min; m_max_velocity = max; }
        void setLifeTime(float min, float max) { m_min_lifeTime = min; m_max_lifeTime = max; }
        void setSize(float min, float max) { m_min_size = min; m_max_size = max; }
        void setMass(float min, float max) { m_min_mass = min; m_max_mass = max; }
        void setColor(const std::array<float, 4>& min, const std::array<float, 4>& max) { m_min_color = min; m_max_color = max; }
        void addEvent(CParticleEvent* e) { m_listEvent.push_back(e); }
        void sortEvents();
        const std::string& name() const { return m_name; }
        int numParticles() const { return m_numparticles; }
        int activeParticles() const { return particles ? particles->GetNumUsedElements() : 0; }

        friend CParticleSystem;
    };

    std::vector<CListParticles*> m_listParticles;
    Vec3 m_position{0, 0, 0};

public:
    CParticleSystem();
    ~CParticleSystem();

    void destroy();
    void update(float time, const Vec3& camRight, const Vec3& camUp);
    void buildMeshData(MeshData& meshData, int index = 0);

    CListParticles* addEmitter(const std::string& name);
    CListParticles* getEmitter(int index);
    int emitterCount() const { return static_cast<int>(m_listParticles.size()); }
    void setPosition(const Vec3& pos) { m_position = pos; }
    const Vec3& position() const { return m_position; }
};

} // namespace engine3d
