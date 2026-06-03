#pragma once

#include <string>
#include <vector>
#include "core/math.h"
#include "feature/particle/types.h"
#include "feature/particle/emitter.h"
#include "render/render_feature.h"

namespace engine3d {

class MaterialManager;
class MaterialSystem;
class MeshManager;
class Renderer;
class Scene;
class AssetRegistry;

namespace particles {

/**
 * @brief System cząsteczkowy odpowiedzialny za efekty wizualne (Particle System).
 *
 * Klasa dziedziczy z IRenderFeature i zarządza grupą emiterów cząsteczek.
 * Umożliwia ładowanie skryptów cząsteczek z plików tekstowych (np. definicje dymu, ognia, iskier)
 * i symulację ich zachowania w przestrzeni 3D.
 *
 * Cząsteczki są renderowane jako tzw. billboardy (płaskie kwadraty z teksturą), 
 * które w każdej klatce są obracane przodem do obiektywu kamery (na podstawie wektorów kamery `camRight` i `camUp`).
 */
class ParticleSystem : public IRenderFeature {
public:
    ParticleSystem();
    ~ParticleSystem();

    /**
     * @brief Ładuje definicję systemu cząsteczkowego z pliku dyskowego.
     */
    bool load(const std::string& file);
    
    /**
     * @brief Niszczy system cząsteczek i czyści powiązane zasoby.
     */
    bool destroy();
    
    /**
     * @brief Aktualizuje fizykę i stan cząsteczek dla wszystkich emiterów (wersja jednowątkowa).
     * @param time Delta time w sekundach.
     * @param camRight Wektor kierunku w prawo kamery (dla billboardowania).
     * @param camUp Wektor kierunku w górę kamery (dla billboardowania).
     */
    void update(float time, const Vec3& camRight, const Vec3& camUp);
    
    /**
     * @brief Aktualizuje fizykę cząsteczek z użyciem puli wątków procesora (Parallel Update).
     */
    void updateParallel(float time, int threadCount);
    
    // Zwraca informacje tekstowe o cząsteczkach dla celów debugowania.
    std::string infoParticles(int index);

    /**
     * @brief Inicjalizuje system cząsteczek i rejestruje wskaźniki do menedżerów zasobów.
     */
    void initialize(Scene& scene,
                    MeshManager& meshManager,
                    MaterialManager& materialManager,
                    MaterialSystem& materialSystem,
                    Renderer& renderer,
                    AssetRegistry& assetRegistry);
    
    /**
     * @brief Główny update wywoływany przez ECS w każdej klatce.
     */
    void update(float dt);

    // Konfiguracja parametrów kamery potrzebnych do zorientowania billboardów cząsteczek.
    void setCameraVectors(const Vec3& right, const Vec3& up) { camRight_ = right; camUp_ = up; }
    void setCameraPosition(const Vec3& pos) { camPos_ = pos; }
    
    // Dodawanie i pobieranie emiterów cząsteczek (ParticleEmitter).
    ParticleEmitter* addEmitter(const std::string& name);
    ParticleEmitter* getEmitter(int index);
    int emitterCount() const { return static_cast<int>(emitters_.size()); }
    
    // Pozycja w przestrzeni 3D, z której emitowane są cząsteczki.
    void setPosition(const Vec3& pos) { position_ = pos; }
    const Vec3& position() const { return position_; }

    // Nadpisane metody z interfejsu IRenderFeature:
    void init(Renderer*) override {}
    void render(VkCommandBuffer, uint32_t, const RenderView&) override {}
    bool isActive() const override { return initialized_; }
    const std::string& getName() const override { static std::string n = "ParticleSystem"; return n; }

private:
    // Pomocnicze funkcje parsujące pliki definicji cząsteczek:
    static std::string extractParen(const std::string& s);
    RealNumber processRealNumber(const std::string& str);
    IntNumber processIntNumber(const std::string& str);
    Vec3Number processVector3(const std::string& str);
    Vec4Number processVector4(const std::string& str);
    bool processEvent(ParticleEmitter* emitter, float fadeMin, float fadeMax, const std::string& line);

    std::vector<ParticleEmitter*> emitters_;
    using EmitterIter = std::vector<ParticleEmitter*>::iterator;
    EmitterIter emittersBegin_, emittersEnd_;
    Vec3 position_{0, 0, 0};
    Vec3 camRight_{1, 0, 0};
    Vec3 camUp_{0, 1, 0};
    Vec3 camPos_{0, 0, 0};
    std::vector<std::string> meshNames_;
    Scene* scene_ = nullptr;
    MeshManager* meshManager_ = nullptr;
    MaterialManager* materialManager_ = nullptr;
    MaterialSystem* materialSystem_ = nullptr;
    Renderer* renderer_ = nullptr;
    AssetRegistry* assetRegistry_ = nullptr;
    int numThreads_ = 1;
    bool initialized_ = false;
};

} // namespace particles
} // namespace engine3d
