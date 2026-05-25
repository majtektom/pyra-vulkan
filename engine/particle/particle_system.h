#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "math.h"
#include "types.h"
#include "object.h"

namespace engine3d {

class Engine;

enum EVENT_TYPES {
    PARTICLE_EVENT_SIZE = 0,
    PARTICLE_EVENT_MASS,
    PARTICLE_EVENT_LIFETIME,
    PARTICLE_EVENT_COLOR,
    PARTICLE_EVENT_POSITION,
    PARTICLE_EVENT_VELOCITY
};

/// szablon do przechowywania czastek w tablicy podanej wielkosci
template <class T>
class RecyclingArray {
public:
    RecyclingArray(int numElements);
    virtual ~RecyclingArray();
    T* New();
    bool Delete(int index);
    int GetNumFreeElements();
    int GetNumUsedElements();
    int GetTotalElements();
    T& GetAt(int index);
    bool IsAlive(int index);
    void DeleteAll();

protected:
    int m_iNumElements;
    int m_iUsedElements;
    T* m_aElements;
    bool* m_bAlive;
    int m_iLastNew;
    T m_NewTemplate;
};

/// system czasteczkowy
class CParticleSystem {
    class CParticle;
    class CListParticles;

    /// klasa przechowujaca zdarzenia
    class CParticleEvent {
        /// czas wywolania zdarzenia
        float time;
        /// typ przechowywanej wartosci
        EVENT_TYPES type;
        /// czas trwania zdarzenia gdy wiekszy od 0.0f to mamy plyna zmiane
        float m_min_duration, m_max_duration;
        /// zmienne przechowyjace dana wartosc
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

    /// funktor do porownania w sortowaniu zdarzen
    struct CParticleEvent_Func {
        bool operator()(CParticleEvent* lhs, CParticleEvent* rhs) const {
            return lhs->GetTime() < rhs->GetTime();
        }
    };

    typedef std::vector<CParticleEvent*> ListEvent;
    typedef std::vector<CParticleEvent*>::iterator ListEventIter;

    /// pojedyncza czastka
    class CParticle {
    public:
        Vec3 m_pos;                         /// polozenie czastki
        Vec3 m_pos_step;                    /// zmiana polozenia
        float m_pos_time = 0;               /// czas w jakim ma sie to polozenie zmienic
        Vec3 m_velocity;                    /// szybkosc czastki
        Vec3 m_velocity_step;
        float m_velocity_time = 0;
        std::array<float, 4> m_color{1, 1, 1, 1};   /// kolor czastki
        std::array<float, 4> m_color_step{0, 0, 0, 0};
        float m_color_time = 0;
        float m_size = 1;                   /// wielkosc czastki
        float m_size_step = 0;
        float m_size_time = 0;
        float m_mass = 1;                   /// masa czastki
        float m_mass_step = 0;
        float m_mass_time = 0;
        float m_lifeTime = 3;               /// czas zycia czastki
        float m_age = 0;                    /// wiek czastki
        float m_ageEvent = 0;               /// wiek czastki uzywany przy zdarzeniach (mozna go resetowac)
        ListEventIter m_iter_event;         /// iterator do aktualnego zdarzenia
        friend CListParticles;
        friend CParticleEvent;
    };

    /// przechowuje zmienne RealNumber
    struct RealNumber { float max, min; };
    /// przechowuje IntNumber
    struct IntNumber { int max, min; };
    /// przechowyje vektor min i max
    struct Vec3Number { Vec3 min, max; };
    struct Vec4Number { std::array<float, 4> min, max; };

    /// lista czastek
    class CListParticles {
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
        int lastActiveCount = -1;

    public:
        CListParticles();
        ~CListParticles();
        /// tworzy czasteczki na podstawie swoich ustawionych danych
        bool createParticles();
        /// niszczymy czasteczki
        bool destroy();
        /// aktualizacja czastek
        void update(float time);
        /// aktualizacja czastek rownolegle (dzieli na kawalki)
        void updateParallel(float time, int totalThreads);
        /// wypelnia dane instancji (pozycja, rozmiar, kolor)
        void fillInstanceData(std::vector<ParticleInstanceData>& out);
        /// wypelnia dane instancji rownolegle
        void fillInstanceDataParallel(std::vector<ParticleInstanceData>& out, int threadCount);

        // settery
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

        // gettery
        const std::string& name() const { return m_name; }
        int numParticles() const { return m_numparticles; }
        int activeParticles() const { return particles ? particles->GetNumUsedElements() : 0; }
        const std::string& textureName() const { return m_texturename; }

        friend CParticleSystem;
    };

    /// lista systemow czasteczkowych w naszym systemie
    std::vector<CListParticles*> m_listParticles;
    typedef std::vector<CListParticles*>::iterator ListParticleIter;
    ListParticleIter m_listBegin;
    ListParticleIter m_listEnd;
    /// pozycja ukladu czastek
    Vec3 m_position{0, 0, 0};
    Vec3 m_camRight{1, 0, 0};
    Vec3 m_camUp{0, 1, 0};

    std::vector<std::string> m_meshNames;
    int m_numThreads = 1;
    bool m_initialized = false;

    /// odczytanie vektora 3 elementowego
    Vec3Number processVector3(const std::string& str);
    /// odczytanie wektora 4 elementowego
    Vec4Number processVector4(const std::string& str);
    /// odczytywanie zmiennej typu realNumber
    RealNumber processRealNumber(const std::string& str);
    /// odczytywanie zmiennej typu IntNumber
    IntNumber processIntNumber(const std::string& str);
    /// wczytuje zdarzenie
    bool processEvent(CListParticles* nowy, float fadeMin, float fadeMax, const std::string& line);
    /// wyciaga wartosc spomiedzy nawiasami
    static std::string extractParen(const std::string& s);

public:
    CParticleSystem();
    ~CParticleSystem();
    /// wczytuje system czasteczkowy z pliku
    bool load(const std::string& file);
    /// niszczenie systemu czasteczkowego
    bool destroy();
    /// aktualizacja czastek
    void update(float time, const Vec3& camRight, const Vec3& camUp);
    /// rownolegla aktualizacja czastek
    void updateParallel(float time, int threadCount);
    /// informacje o systemie czastek
    std::string infoParticles(int ktory);

    /// inicjalizuje meshe, materialy i entity dla wszystkich emiterów w silniku
    void initialize(Engine& engine);
    /// pełna aktualizacja: wektory kamery, fizyka, budowa siatki, upload do GPU
    void update(float dt, Engine& engine);

    /// ustawia wektory kamery do billboardingu
    void setCameraVectors(const Vec3& right, const Vec3& up) { m_camRight = right; m_camUp = up; }
    CListParticles* addEmitter(const std::string& name);
    CListParticles* getEmitter(int index);
    int emitterCount() const { return static_cast<int>(m_listParticles.size()); }
    void setPosition(const Vec3& pos) { m_position = pos; }
    const Vec3& position() const { return m_position; }
};

} // namespace engine3d
