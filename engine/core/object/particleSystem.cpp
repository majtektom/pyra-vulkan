#include "particleSystem.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>

namespace engine3d {

template<class T>
static void deleteAll(T& t) {
    for (auto i = t.begin(); i != t.end(); ++i) delete (*i);
    t.clear();
}

static float randFloat(float min, float max) {
    if (min >= max) return min;
    return min + static_cast<float>(std::rand()) / RAND_MAX * (max - min);
}

// ####################################################################
// RecyclingArray implementations
// ####################################################################

template <class T>
RecyclingArray<T>::RecyclingArray(int numElements)
    : m_iNumElements(numElements) {
    m_bAlive = new bool[numElements];
    m_aElements = new T[numElements];
    for (int q = 0; q < numElements; q++)
        m_bAlive[q] = false;
    m_iUsedElements = 0;
    m_iLastNew = 0;
}

template <class T>
RecyclingArray<T>::~RecyclingArray() {
    delete[] m_bAlive;
    delete[] m_aElements;
}

template <class T>
T* RecyclingArray<T>::New() {
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

template <class T>
bool RecyclingArray<T>::Delete(int index) {
    if (index < 0 || index >= m_iNumElements || !m_bAlive[index])
        return false;
    m_bAlive[index] = false;
    m_iUsedElements--;
    return true;
}

template <class T>
int RecyclingArray<T>::GetNumFreeElements() { return m_iNumElements - GetNumUsedElements(); }
template <class T>
int RecyclingArray<T>::GetNumUsedElements() { return m_iUsedElements; }
template <class T>
int RecyclingArray<T>::GetTotalElements() { return m_iNumElements; }
template <class T>
T& RecyclingArray<T>::GetAt(int index) { return m_aElements[index]; }
template <class T>
bool RecyclingArray<T>::IsAlive(int index) { return m_bAlive[index]; }
template <class T>
void RecyclingArray<T>::DeleteAll() {
    for (int q = 0; q < m_iNumElements; q++) m_bAlive[q] = false;
    m_iUsedElements = 0;
    m_iLastNew = 0;
}

template class RecyclingArray<CParticleSystem::CParticle>;

// ####################################################################
// Pomocnicze: usuwanie komentarzy, dzielenie napisow
// ####################################################################

/// Usuwa komentarze // i /* */ z pojedynczej linii
static std::string stripLineComments(std::string line) {
    // usun /* */ w jednej linii
    size_t start = line.find("/*");
    while (start != std::string::npos) {
        size_t end = line.find("*/", start + 2);
        if (end != std::string::npos) {
            line.erase(start, end - start + 2);
        } else {
            line.erase(start);
            break;
        }
        start = line.find("/*");
    }
    // usun //
    size_t sl = line.find("//");
    if (sl != std::string::npos) line = line.substr(0, sl);
    return line;
}

/// Dzieli string po przecinkach z uwzglednieniem zagniezdzonych nawiasow
static std::vector<std::string> splitByCommaOutsideParen(const std::string& s) {
    std::vector<std::string> parts;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (s[i] == ',' && depth == 0) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(s.substr(start));
    return parts;
}

// ####################################################################
// CParticleEvent - obsluga zdarzen czasteczek
// ####################################################################

/// Uruchamia zdarzenie na czastce - zmienia jej wlasciwosci
/// Jesli duration > 0 to zmiana jest plyna (step), jesli 0 to skokowa
void CParticleSystem::CParticleEvent::RunEvent(CParticle* p) {
    float d = randFloat(m_min_duration, m_max_duration);
    if (d > 0.001f) {
        switch (type) {
        case PARTICLE_EVENT_SIZE:
            p->m_size_step = (randFloat(m_min_1, m_max_1) - p->m_size) / d;
            p->m_size_time = d;
            break;
        case PARTICLE_EVENT_MASS:
            p->m_mass_step = (randFloat(m_min_1, m_max_1) - p->m_mass) / d;
            p->m_mass_time = d;
            break;
        case PARTICLE_EVENT_COLOR: {
            std::array<float, 4> target;
            target[0] = randFloat(m_min_1, m_max_1);
            target[1] = randFloat(m_min_2, m_max_2);
            target[2] = randFloat(m_min_3, m_max_3);
            target[3] = randFloat(m_min_4, m_max_4);
            p->m_color_step[0] = (target[0] - p->m_color[0]) / d;
            p->m_color_step[1] = (target[1] - p->m_color[1]) / d;
            p->m_color_step[2] = (target[2] - p->m_color[2]) / d;
            p->m_color_step[3] = (target[3] - p->m_color[3]) / d;
            p->m_color_time = d;
            break;
        }
        case PARTICLE_EVENT_POSITION: {
            Vec3 target;
            target.x = randFloat(m_min_1, m_max_1);
            target.y = randFloat(m_min_2, m_max_2);
            target.z = randFloat(m_min_3, m_max_3);
            p->m_pos_step.x = (target.x - p->m_pos.x) / d;
            p->m_pos_step.y = (target.y - p->m_pos.y) / d;
            p->m_pos_step.z = (target.z - p->m_pos.z) / d;
            p->m_pos_time = d;
            break;
        }
        case PARTICLE_EVENT_VELOCITY: {
            Vec3 target;
            target.x = randFloat(m_min_1, m_max_1);
            target.y = randFloat(m_min_2, m_max_2);
            target.z = randFloat(m_min_3, m_max_3);
            p->m_velocity_step.x = (target.x - p->m_velocity.x) / d;
            p->m_velocity_step.y = (target.y - p->m_velocity.y) / d;
            p->m_velocity_step.z = (target.z - p->m_velocity.z) / d;
            p->m_velocity_time = d;
            break;
        }
        default: break;
        }
    } else {
        switch (type) {
        case PARTICLE_EVENT_SIZE:   p->m_size = randFloat(m_min_1, m_max_1); break;
        case PARTICLE_EVENT_MASS:   p->m_mass = randFloat(m_min_1, m_max_1); break;
        case PARTICLE_EVENT_LIFETIME: p->m_lifeTime = randFloat(m_min_1, m_max_1); break;
        case PARTICLE_EVENT_COLOR:
            p->m_color[0] = randFloat(m_min_1, m_max_1);
            p->m_color[1] = randFloat(m_min_2, m_max_2);
            p->m_color[2] = randFloat(m_min_3, m_max_3);
            p->m_color[3] = randFloat(m_min_4, m_max_4);
            break;
        case PARTICLE_EVENT_POSITION:
            p->m_pos.x = randFloat(m_min_1, m_max_1);
            p->m_pos.y = randFloat(m_min_2, m_max_2);
            p->m_pos.z = randFloat(m_min_3, m_max_3);
            break;
        case PARTICLE_EVENT_VELOCITY:
            p->m_velocity.x = randFloat(m_min_1, m_max_1);
            p->m_velocity.y = randFloat(m_min_2, m_max_2);
            p->m_velocity.z = randFloat(m_min_3, m_max_3);
            break;
        default: break;
        }
    }
}

// ####################################################################
// CListParticles - zarzadza pojedyncza lista czastek (emitter)
// ####################################################################

CParticleSystem::CListParticles::CListParticles() {
    m_numparticles = 1;
    m_emitrate = 1;
    m_min_lifeTime = m_max_lifeTime = 3.0f;
    m_max_mass = m_min_mass = 1.0f;
    m_min_size = m_max_size = 1.0f;
    particles = nullptr;
}

CParticleSystem::CListParticles::~CListParticles() { destroy(); }

bool CParticleSystem::CListParticles::createParticles() {
    newPartsExcess = 0;
    particles = new RecyclingArray<CParticle>(m_numparticles);
    sortEvents();
    return true;
}

bool CParticleSystem::CListParticles::destroy() {
    delete particles;
    particles = nullptr;
    deleteAll(m_listEvent);
    return true;
}

void CParticleSystem::CListParticles::sortEvents() {
    CParticleEvent_Func cf;
    std::sort(m_listEvent.begin(), m_listEvent.end(), cf);
    m_BeginListEvent = m_listEvent.begin();
    m_EndListEvent = m_listEvent.end();
}

/// Aktualizuje wszystkie czastki - fizyka, zdarzenia, emisja nowych
void CParticleSystem::CListParticles::update(float time) {
    if (!particles) return;

    CParticle* par;
    for (int i = 0; i < particles->GetTotalElements(); ++i) {
        if (!particles->IsAlive(i)) continue;
        par = &particles->GetAt(i);

        par->m_age += time;
        if (par->m_age < par->m_lifeTime) {
            par->m_ageEvent += time;

            par->m_pos.x += par->m_velocity.x * time;
            par->m_pos.y += par->m_velocity.y * time;
            par->m_pos.z += par->m_velocity.z * time;
            par->m_pos.x += m_gravitation.x * par->m_mass * time;
            par->m_pos.y += m_gravitation.y * par->m_mass * time;
            par->m_pos.z += m_gravitation.z * par->m_mass * time;

            if (par->m_color_time > 0.0f) {
                par->m_color_time -= time;
                par->m_color[0] += par->m_color_step[0] * time;
                par->m_color[1] += par->m_color_step[1] * time;
                par->m_color[2] += par->m_color_step[2] * time;
                par->m_color[3] += par->m_color_step[3] * time;
            }
            if (par->m_mass_time > 0.0f) {
                par->m_mass_time -= time;
                par->m_mass += par->m_mass_step * time;
            }
            if (par->m_pos_time > 0.0f) {
                par->m_pos_time -= time;
                par->m_pos.x += par->m_pos_step.x * time;
                par->m_pos.y += par->m_pos_step.y * time;
                par->m_pos.z += par->m_pos_step.z * time;
            }
            if (par->m_size_time > 0.0f) {
                par->m_size_time -= time;
                par->m_size += par->m_size_step * time;
            }
            if (par->m_velocity_time > 0.0f) {
                par->m_velocity_time -= time;
                par->m_velocity.x += par->m_velocity_step.x * time;
                par->m_velocity.y += par->m_velocity_step.y * time;
                par->m_velocity.z += par->m_velocity_step.z * time;
            }

            for (ListEventIter ilist = par->m_iter_event;
                 ilist != m_EndListEvent && par->m_ageEvent > (*ilist)->GetTime(); ++ilist) {
                (*ilist)->RunEvent(par);
                par->m_iter_event = ilist;
            }
        } else {
            particles->Delete(i);
        }
    }

    int newparticles = static_cast<int>(m_emitrate * time);
    newPartsExcess += (m_emitrate * time) - newparticles;
    if (newPartsExcess > 1.0f) {
        newparticles += static_cast<int>(newPartsExcess);
        newPartsExcess -= static_cast<int>(newPartsExcess);
    }

    for (int j = 0; j < newparticles; ++j) {
        par = particles->New();
        if (!par) break;

        par->m_age = par->m_ageEvent = 0.0f;
        par->m_iter_event = m_BeginListEvent;
        par->m_lifeTime = randFloat(m_min_lifeTime, m_max_lifeTime);

        par->m_pos.x = randFloat(m_min_position.x, m_max_position.x);
        par->m_pos.y = randFloat(m_min_position.y, m_max_position.y);
        par->m_pos.z = randFloat(m_min_position.z, m_max_position.z);
        par->m_pos_step = {0, 0, 0};
        par->m_pos_time = 0.0f;

        par->m_velocity.x = randFloat(m_min_velocity.x, m_max_velocity.x);
        par->m_velocity.y = randFloat(m_min_velocity.y, m_max_velocity.y);
        par->m_velocity.z = randFloat(m_min_velocity.z, m_max_velocity.z);
        par->m_velocity_step = {0, 0, 0};
        par->m_velocity_time = 0.0f;

        par->m_mass = randFloat(m_min_mass, m_max_mass);
        par->m_mass_step = 0.0f;
        par->m_mass_time = 0.0f;

        par->m_size = randFloat(m_min_size, m_max_size);
        par->m_size_step = 0.0f;
        par->m_size_time = 0.0f;

        par->m_color[0] = randFloat(m_min_color[0], m_max_color[0]);
        par->m_color[1] = randFloat(m_min_color[1], m_max_color[1]);
        par->m_color[2] = randFloat(m_min_color[2], m_max_color[2]);
        par->m_color[3] = randFloat(m_min_color[3], m_max_color[3]);
        par->m_color_step = {0, 0, 0, 0};
        par->m_color_time = 0.0f;
    }
}

/// Buduje siatke z aktywnych czastek (billboard quady)
void CParticleSystem::CListParticles::buildMeshData(MeshData& meshData, int& particleCount,
                                                     const Vec3& camRight, const Vec3& camUp) {
    if (!particles) { particleCount = 0; return; }
    int aliveCount = particles->GetNumUsedElements();
    particleCount = aliveCount;
    if (aliveCount == 0) return;

    float rx = camRight.x, ry = camRight.y, rz = camRight.z;
    float ux = camUp.x, uy = camUp.y, uz = camUp.z;
    int vi = 0, ii = 0;

    for (int i = 0; i < particles->GetTotalElements(); ++i) {
        if (!particles->IsAlive(i)) continue;
        CParticle* par = &particles->GetAt(i);
        float s = par->m_size;
        float px = par->m_pos.x, py = par->m_pos.y, pz = par->m_pos.z;
        float cr = par->m_color[0], cg = par->m_color[1], cb = par->m_color[2];

        // billboard quad: 4 wierzcholki bezposrednio
        float rsx = rx * s, rsy = ry * s, rsz = rz * s;
        float usx = ux * s, usy = uy * s, usz = uz * s;

        Vertex& v0 = meshData.vertices[vi + 0];
        v0.position[0] = px - rsx - usx; v0.position[1] = py - rsy - usy; v0.position[2] = pz - rsz - usz;
        v0.normal[0] = 0; v0.normal[1] = 0; v0.normal[2] = 1;
        v0.color[0] = cr; v0.color[1] = cg; v0.color[2] = cb;
        v0.texCoord[0] = 0; v0.texCoord[1] = 0;

        Vertex& v1 = meshData.vertices[vi + 1];
        v1.position[0] = px + rsx - usx; v1.position[1] = py + rsy - usy; v1.position[2] = pz + rsz - usz;
        v1.normal[0] = 0; v1.normal[1] = 0; v1.normal[2] = 1;
        v1.color[0] = cr; v1.color[1] = cg; v1.color[2] = cb;
        v1.texCoord[0] = 1; v1.texCoord[1] = 0;

        Vertex& v2 = meshData.vertices[vi + 2];
        v2.position[0] = px + rsx + usx; v2.position[1] = py + rsy + usy; v2.position[2] = pz + rsz + usz;
        v2.normal[0] = 0; v2.normal[1] = 0; v2.normal[2] = 1;
        v2.color[0] = cr; v2.color[1] = cg; v2.color[2] = cb;
        v2.texCoord[0] = 1; v2.texCoord[1] = 1;

        Vertex& v3 = meshData.vertices[vi + 3];
        v3.position[0] = px - rsx + usx; v3.position[1] = py - rsy + usy; v3.position[2] = pz - rsz + usz;
        v3.normal[0] = 0; v3.normal[1] = 0; v3.normal[2] = 1;
        v3.color[0] = cr; v3.color[1] = cg; v3.color[2] = cb;
        v3.texCoord[0] = 0; v3.texCoord[1] = 1;

        meshData.indices[ii + 0] = static_cast<uint32_t>(vi + 0);
        meshData.indices[ii + 1] = static_cast<uint32_t>(vi + 1);
        meshData.indices[ii + 2] = static_cast<uint32_t>(vi + 2);
        meshData.indices[ii + 3] = static_cast<uint32_t>(vi + 2);
        meshData.indices[ii + 4] = static_cast<uint32_t>(vi + 3);
        meshData.indices[ii + 5] = static_cast<uint32_t>(vi + 0);

        vi += 4; ii += 6;
    }
}

/// Rownolegla wersja buildMeshData - dzieli tablice na kawalki
void CParticleSystem::CListParticles::buildMeshDataParallel(MeshData& meshData, int& particleCount,
                                                              const Vec3& camRight, const Vec3& camUp,
                                                              int threadCount) {
    if (!particles) { particleCount = 0; return; }
    int aliveCount = particles->GetNumUsedElements();
    particleCount = aliveCount;
    if (aliveCount == 0) return;

    int totalVerts = aliveCount * 4;
    int totalIndices = aliveCount * 6;
    if (meshData.vertices.capacity() < static_cast<size_t>(totalVerts))
        meshData.vertices.reserve(totalVerts);
    if (meshData.indices.capacity() < static_cast<size_t>(totalIndices))
        meshData.indices.reserve(totalIndices);
    meshData.vertices.resize(totalVerts);
    meshData.indices.resize(totalIndices);

    float rx = camRight.x, ry = camRight.y, rz = camRight.z;
    float ux = camUp.x, uy = camUp.y, uz = camUp.z;

    if (threadCount < 2 || aliveCount < 1000) {
        buildMeshData(meshData, particleCount, camRight, camUp);
        return;
    }

    // najpierw zlicz aktywne indeksy
    std::vector<int> activeIdx;
    activeIdx.reserve(aliveCount);
    for (int i = 0; i < particles->GetTotalElements(); ++i) {
        if (particles->IsAlive(i)) activeIdx.push_back(i);
    }

    int chunkSize = (aliveCount + threadCount - 1) / threadCount;
    std::vector<std::future<void>> futs;

    for (int t = 0; t < threadCount; t++) {
        int cstart = t * chunkSize;
        int cend = std::min(cstart + chunkSize, aliveCount);
        if (cstart >= cend) break;

        futs.push_back(std::async(std::launch::async, [&, t, cstart, cend, rx, ry, rz, ux, uy, uz]() {
            int baseParticle = t * chunkSize;
            int vi = baseParticle * 4;
            int ii = baseParticle * 6;

            for (int j = cstart; j < cend; j++) {
                int pi = activeIdx[j];
                CParticle* par = &particles->GetAt(pi);
                float s = par->m_size;
                float px = par->m_pos.x, py = par->m_pos.y, pz = par->m_pos.z;
                float cr = par->m_color[0], cg = par->m_color[1], cb = par->m_color[2];

                float rsx = rx * s, rsy = ry * s, rsz = rz * s;
                float usx = ux * s, usy = uy * s, usz = uz * s;

                Vertex& v0 = meshData.vertices[vi + 0];
                v0.position[0] = px - rsx - usx; v0.position[1] = py - rsy - usy; v0.position[2] = pz - rsz - usz;
                v0.normal[0] = 0; v0.normal[1] = 0; v0.normal[2] = 1;
                v0.color[0] = cr; v0.color[1] = cg; v0.color[2] = cb;
                v0.texCoord[0] = 0; v0.texCoord[1] = 0;

                Vertex& v1 = meshData.vertices[vi + 1];
                v1.position[0] = px + rsx - usx; v1.position[1] = py + rsy - usy; v1.position[2] = pz + rsz - usz;
                v1.normal[0] = 0; v1.normal[1] = 0; v1.normal[2] = 1;
                v1.color[0] = cr; v1.color[1] = cg; v1.color[2] = cb;
                v1.texCoord[0] = 1; v1.texCoord[1] = 0;

                Vertex& v2 = meshData.vertices[vi + 2];
                v2.position[0] = px + rsx + usx; v2.position[1] = py + rsy + usy; v2.position[2] = pz + rsz + usz;
                v2.normal[0] = 0; v2.normal[1] = 0; v2.normal[2] = 1;
                v2.color[0] = cr; v2.color[1] = cg; v2.color[2] = cb;
                v2.texCoord[0] = 1; v2.texCoord[1] = 1;

                Vertex& v3 = meshData.vertices[vi + 3];
                v3.position[0] = px - rsx + usx; v3.position[1] = py - rsy + usy; v3.position[2] = pz - rsz + usz;
                v3.normal[0] = 0; v3.normal[1] = 0; v3.normal[2] = 1;
                v3.color[0] = cr; v3.color[1] = cg; v3.color[2] = cb;
                v3.texCoord[0] = 0; v3.texCoord[1] = 1;

                meshData.indices[ii + 0] = static_cast<uint32_t>(vi + 0);
                meshData.indices[ii + 1] = static_cast<uint32_t>(vi + 1);
                meshData.indices[ii + 2] = static_cast<uint32_t>(vi + 2);
                meshData.indices[ii + 3] = static_cast<uint32_t>(vi + 2);
                meshData.indices[ii + 4] = static_cast<uint32_t>(vi + 3);
                meshData.indices[ii + 5] = static_cast<uint32_t>(vi + 0);

                vi += 4; ii += 6;
            }
        }));
    }
    for (auto& f : futs) f.get();
}

// ####################################################################
// Parsowanie wartosci
// ####################################################################

/// Wyciaga zawartosc miedzy pierwszym '(' a ostatnim ')'
std::string CParticleSystem::extractParen(const std::string& s) {
    size_t start = s.find('(');
    size_t end = s.rfind(')');
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return "";
    return s.substr(start + 1, end - start - 1);
}

CParticleSystem::RealNumber CParticleSystem::processRealNumber(const std::string& str) {
    RealNumber n{0, 0};
    std::string s = str;
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    if (s.find("RealRandom") == 0) {
        std::string inside = extractParen(s);
        std::stringstream ss(inside);
        float a, b; char comma;
        ss >> a >> comma >> b;
        n.min = a; n.max = b;
    } else {
        n.min = n.max = std::stof(s);
    }
    return n;
}

CParticleSystem::IntNumber CParticleSystem::processIntNumber(const std::string& str) {
    IntNumber n{0, 0};
    std::string s = str;
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    if (s.find("IntRandom") == 0) {
        std::string inside = extractParen(s);
        std::stringstream ss(inside);
        int a, b; char comma;
        ss >> a >> comma >> b;
        n.min = a; n.max = b;
    } else {
        n.min = n.max = std::stoi(s);
    }
    return n;
}

CParticleSystem::Vec3Number CParticleSystem::processVector3(const std::string& str) {
    Vec3Number v;
    v.min = v.max = {0, 0, 0};
    std::string inside = extractParen(str);
    auto parts = splitByCommaOutsideParen(inside);
    for (size_t c = 0; c < parts.size() && c < 3; c++) {
        RealNumber r = processRealNumber(parts[c]);
        if (c == 0) { v.min.x = r.min; v.max.x = r.max; }
        else if (c == 1) { v.min.y = r.min; v.max.y = r.max; }
        else if (c == 2) { v.min.z = r.min; v.max.z = r.max; }
    }
    return v;
}

CParticleSystem::Vec4Number CParticleSystem::processVector4(const std::string& str) {
    Vec4Number v;
    v.min = v.max = {0, 0, 0, 0};
    std::string inside = extractParen(str);
    auto parts = splitByCommaOutsideParen(inside);
    for (size_t c = 0; c < parts.size() && c < 4; c++) {
        RealNumber r = processRealNumber(parts[c]);
        if (c == 0) { v.min[0] = r.min; v.max[0] = r.max; }
        else if (c == 1) { v.min[1] = r.min; v.max[1] = r.max; }
        else if (c == 2) { v.min[2] = r.min; v.max[2] = r.max; }
        else if (c == 3) { v.min[3] = r.min; v.max[3] = r.max; }
    }
    return v;
}

// ####################################################################
// CParticleSystem - glowna klasa
// ####################################################################

CParticleSystem::CParticleSystem() {}
CParticleSystem::~CParticleSystem() { destroy(); }

bool CParticleSystem::destroy() {
    deleteAll(m_listParticles);
    m_listBegin = m_listParticles.begin();
    m_listEnd = m_listParticles.end();
    return true;
}

std::string CParticleSystem::infoParticles(int ktory) {
    if (m_listParticles.size() > static_cast<size_t>(ktory)) {
        CListParticles* par = *(m_listParticles.begin() + ktory);
        return par->m_name + ", maks=" + std::to_string(par->m_numparticles)
            + ", aktywne=" + std::to_string(par->particles ? par->particles->GetNumUsedElements() : 0);
    }
    return "";
}

void CParticleSystem::update(float time, const Vec3&, const Vec3&) {
    for (auto* e : m_listParticles) e->update(time);
}

void CParticleSystem::buildMeshData(MeshData& meshData, int index) {
    if (index < 0 || index >= static_cast<int>(m_listParticles.size())) return;
    int count = 0;
    m_listParticles[index]->buildMeshData(meshData, count, m_camRight, m_camUp);
}

void CParticleSystem::buildMeshDataParallel(MeshData& meshData, int index, int threadCount) {
    if (index < 0 || index >= static_cast<int>(m_listParticles.size())) return;
    int count = 0;
    m_listParticles[index]->buildMeshDataParallel(meshData, count, m_camRight, m_camUp, threadCount);
}

CParticleSystem::CListParticles* CParticleSystem::addEmitter(const std::string& name) {
    auto* e = new CListParticles();
    e->setName(name);
    m_listParticles.push_back(e);
    return e;
}

CParticleSystem::CListParticles* CParticleSystem::getEmitter(int index) {
    if (index < 0 || index >= static_cast<int>(m_listParticles.size())) return nullptr;
    return m_listParticles[index];
}

// ####################################################################
// Wczytywanie z pliku .particle
// ####################################################################

bool CParticleSystem::processEvent(CListParticles* nowy, float fadeMin, float fadeMax, const std::string& line) {
    CParticleEvent* e = nullptr;
    std::string s = line;
    s.erase(0, s.find_first_not_of(" \t"));

    // wyciagnij czas (pierwsza liczba)
    std::stringstream ss(s);
    float time;
    ss >> time;

    // reszta: varName=VALUE
    std::string rest;
    std::getline(ss, rest);
    rest.erase(0, rest.find_first_not_of(" \t"));

    size_t eqPos = rest.find('=');
    if (eqPos == std::string::npos) return false;
    std::string varName = rest.substr(0, eqPos);
    std::string varValue = rest.substr(eqPos + 1);
    varValue.erase(0, varValue.find_first_not_of(" \t"));

    if (varName.empty()) return false;

         if (varName == "size")     { auto n = processRealNumber(varValue); e = new CParticleEvent(); e->type = PARTICLE_EVENT_SIZE; e->m_min_1 = n.min; e->m_max_1 = n.max; }
    else if (varName == "lifeTime") { auto n = processRealNumber(varValue); e = new CParticleEvent(); e->type = PARTICLE_EVENT_LIFETIME; e->m_min_1 = n.min; e->m_max_1 = n.max; }
    else if (varName == "mass")     { auto n = processRealNumber(varValue); e = new CParticleEvent(); e->type = PARTICLE_EVENT_MASS; e->m_min_1 = n.min; e->m_max_1 = n.max; }
    else if (varName == "position") { auto n = processVector3(varValue); e = new CParticleEvent(); e->type = PARTICLE_EVENT_POSITION;
        e->m_min_1 = n.min.x; e->m_min_2 = n.min.y; e->m_min_3 = n.min.z;
        e->m_max_1 = n.max.x; e->m_max_2 = n.max.y; e->m_max_3 = n.max.z; }
    else if (varName == "velocity") { auto n = processVector3(varValue); e = new CParticleEvent(); e->type = PARTICLE_EVENT_VELOCITY;
        e->m_min_1 = n.min.x; e->m_min_2 = n.min.y; e->m_min_3 = n.min.z;
        e->m_max_1 = n.max.x; e->m_max_2 = n.max.y; e->m_max_3 = n.max.z; }
    else if (varName == "color")    { auto n = processVector4(varValue); e = new CParticleEvent(); e->type = PARTICLE_EVENT_COLOR;
        e->m_min_1 = n.min[0]; e->m_min_2 = n.min[1]; e->m_min_3 = n.min[2]; e->m_min_4 = n.min[3];
        e->m_max_1 = n.max[0]; e->m_max_2 = n.max[1]; e->m_max_3 = n.max[2]; e->m_max_4 = n.max[3]; }

    if (!e) return false;
    e->time = time;
    e->m_min_duration = fadeMin;
    e->m_max_duration = fadeMax;
    nowy->addEvent(e);
    return true;
}

/// Wczytuje system czasteczkowy z pliku .particle
bool CParticleSystem::load(const std::string& file) {
    std::ifstream f(file);
    if (!f.is_open()) {
        std::cerr << "[ParticleSystem] Cannot open file: " << file << std::endl;
        return false;
    }

    // czytaj linie, usuwaj komentarze
    auto nextLine = [&](std::string& out) -> bool {
        while (std::getline(f, out)) {
            out = stripLineComments(out);
            out.erase(0, out.find_first_not_of(" \t\r\n"));
            out.erase(out.find_last_not_of(" \t\r\n") + 1);
            if (!out.empty()) return true;
        }
        return false;
    };

    std::string line;
    // naglowek: ParticleSystem 1.0 "nazwa"
    if (!nextLine(line)) return false;
    std::stringstream header(line);
    std::string magic;
    float version;
    std::string sysName;
    header >> magic >> version >> sysName;
    if (magic != "ParticleSystem") {
        std::cerr << "[ParticleSystem] Not a particle system file" << std::endl;
        return false;
    }

    destroy();

    while (nextLine(line)) {
        std::stringstream ss(line);
        std::string keyword;
        ss >> keyword;

        if (keyword != "particles") continue;

        std::string name;
        ss >> name;
        if (!name.empty() && name[0] == '"') {
            name = name.substr(1);
            if (!name.empty() && name.back() == '"') name.pop_back();
        }

        // oczekuj "{"
        if (!nextLine(line) || line != "{") {
            std::cerr << "[ParticleSystem] Expected '{' after particles name" << std::endl;
            return false;
        }

        CListParticles* nowy = new CListParticles();
        nowy->m_name = name;

        float fadeMin = 0, fadeMax = 0;
        bool inFade = false;

        // czytaj wnetrze bloku
        while (nextLine(line)) {
            if (line == "}") break;

            std::stringstream ls(line);
            std::string varName;
            ls >> varName;

            // wyciagnij wartosc - najpierw sprawdz czy w linii jest '='
            std::string lineVal;
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                lineVal = line.substr(eqPos + 1);
                lineVal.erase(0, lineVal.find_first_not_of(" \t"));
            }

            // jesli varName zawiera "=" (brak spacji przed =), rozdziel
            size_t eqInVar = varName.find('=');
            if (eqInVar != std::string::npos) {
                if (lineVal.empty()) lineVal = varName.substr(eqInVar + 1);
                varName = varName.substr(0, eqInVar);
            }

            // helper: wez wartosc z lineVal lub z samej linii
            auto getVal = [&]() -> std::string {
                if (!lineVal.empty()) return lineVal;
                size_t e = line.find('=');
                if (e == std::string::npos) return "";
                std::string v = line.substr(e + 1);
                v.erase(0, v.find_first_not_of(" \t"));
                return v;
            };

            if (varName == "fade") {
                // fade <czas> at <czas> zdarzenie=wartosc
                std::string rest = line.substr(line.find("fade") + 4);
                rest.erase(0, rest.find_first_not_of(" \t"));
                std::stringstream fs(rest);
                float fadeTime;
                fs >> fadeTime;
                fadeMin = fadeMax = fadeTime;
                inFade = true;
                // sprawdz czy w tej samej linii jest "at"
                size_t atPos = rest.find("at");
                if (atPos != std::string::npos) {
                    processEvent(nowy, fadeMin, fadeMax, rest.substr(atPos + 2));
                    inFade = false;
                }
            } else if (varName == "at") {
                processEvent(nowy, inFade ? fadeMin : 0, inFade ? fadeMax : 0,
                            line.substr(line.find("at") + 2));
                inFade = false;
            }             else if (varName == "numparticles") { nowy->m_numparticles = processIntNumber(getVal()).min; }
            else if (varName == "emitrate")   { nowy->m_emitrate = processIntNumber(getVal()).min; }
            else if (varName == "gravity")    { nowy->m_gravitation = processVector3(getVal()).min; }
            else if (varName == "texture")    { std::string v = getVal();
                v.erase(0, v.find_first_not_of(" \t\""));
                v.erase(v.find_last_not_of(" \t\"") + 1);
                nowy->m_texturename = v; }
            else if (varName == "size")       { auto n = processRealNumber(getVal()); nowy->m_min_size = n.min; nowy->m_max_size = n.max; }
            else if (varName == "position")   { auto n = processVector3(getVal()); nowy->m_min_position = n.min; nowy->m_max_position = n.max; }
            else if (varName == "velocity")   { auto n = processVector3(getVal()); nowy->m_min_velocity = n.min; nowy->m_max_velocity = n.max; }
            else if (varName == "lifeTime")   { auto n = processRealNumber(getVal()); nowy->m_min_lifeTime = n.min; nowy->m_max_lifeTime = n.max; }
            else if (varName == "mass")       { auto n = processRealNumber(getVal()); nowy->m_min_mass = n.min; nowy->m_max_mass = n.max; }
            else if (varName == "color")      { auto n = processVector4(getVal()); nowy->m_min_color = n.min; nowy->m_max_color = n.max; }
        }

        if (!nowy->createParticles()) {
            std::cerr << "[ParticleSystem] Error creating particles" << std::endl;
            delete nowy;
            return false;
        }
        m_listParticles.push_back(nowy);
    }

    m_listBegin = m_listParticles.begin();
    m_listEnd = m_listParticles.end();
    return true;
}

} // namespace engine3d
