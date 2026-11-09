#pragma once

#include <array>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "camera/camera_picking.h"
#include "feature/ai/nav_grid.h"
#include "settlers_assets.h"
#include "settlers_defs.h"

namespace engine3d {
class Engine;
class Scene;
class Entity;
class Terrain;
}

namespace settlers {

using engine3d::Entity;
using engine3d::Ray;

// --- Jednostki ---
enum class UnitState {
    Idle,           // bezczynność (osadnik kręci się koło zamku, żołnierz stoi na straży)
    Moving,         // marsz do punktu
    GoBuild,        // osadnik idzie na plac budowy
    Building,       // osadnik buduje
    GoToWork,       // osadnik idzie do budynku, w którym będzie pracował
    WorkWait,       // pracownik czeka w budynku na kolejny cykl
    GoGather,       // pracownik idzie do surowca (drzewo / skała / pole)
    Gathering,      // pracownik zbiera surowiec
    ReturnHome,     // pracownik niesie surowiec do budynku
    Attack,         // żołnierz atakuje cel
};

struct Unit {
    bool alive = false;
    UnitType type = UnitType::Settler;
    int faction = 0;
    Entity* entity = nullptr;
    Entity* ring = nullptr;

    Vec3 pos{};
    float yaw = 0.0f;
    float hp = 1.0f;
    UnitState state = UnitState::Idle;

    // Ruch
    std::vector<Vec3> path;
    size_t pathIndex = 0;
    Vec3 goal{};
    bool attackMove = false;     // marsz z atakowaniem napotkanych wrogów
    float repathTimer = 0.0f;
    float stuckTimer = 0.0f;

    // Praca
    int building = -1;           // plac budowy lub miejsce pracy
    int resourceNode = -1;
    CarryType carry = CarryType::None;
    Vec3 workSpot{};

    // Walka
    int targetUnit = -1;
    int targetBuilding = -1;
    float attackCooldown = 0.0f;
    Vec3 guardPos{};

    float timer = 0.0f;
    float animTime = 0.0f;
    float hitFlash = 0.0f;
    float sfxTimer = 0.0f;       // odstęp między dźwiękami pracy (siekiera, kilof, młotek)
    Vec3 progressPos{};          // wykrywanie braku postępu w marszu (tłok)
    float progressTimer = 0.0f;
    float ghostTimer = 0.0f;     // > 0: jednostka przeciska się przez tłum (ignoruje rozpychanie)
    Vec3 stallPos{};             // wykrywanie zawieszenia: pozycja odniesienia
    float stallTime = 0.0f;      // ile sekund jednostka "w drodze" nie zmienia położenia
    int stallCount = 0;          // ile razy wykryto zawieszenie (debug)
    float lastDamagedTime = -100.0f;
    bool selected = false;
    bool moving = false;         // czy w tej klatce się przemieszczał (animacja)
    std::string meshName;
};

// --- Budynki ---
struct Building {
    bool alive = false;
    BuildingType type = BuildingType::House;
    int faction = 0;
    Entity* entity = nullptr;
    Entity* scaffold = nullptr;
    Entity* ring = nullptr;
    Vec3 pos{};
    float rotY = 0.0f;
    float hp = 1.0f;
    bool constructed = false;
    float progress = 0.0f;       // 0..1
    int worker = -1;
    std::deque<UnitType> trainQueue;
    float trainTimer = 0.0f;
    float attackCooldown = 0.0f;
    float lastDamagedTime = -100.0f;
    bool selected = false;
    bool lacksResources = false;     // drwal/kamieniarz: brak dostępnego surowca w zasięgu (stan bieżący)
    int resourcesInRange = -1;       // liczba drzew/skał w zasięgu pracy (-1 = jeszcze nie liczono)
    float lowResourcesMsgTime = -100.0f;
    float resourceCheckTimer = 0.0f;
    float repairCredit = 0.0f;       // PŻ, które można jeszcze naprawić z opłaconego drewna
    bool repairBlocked = false;      // naprawa wstrzymana - brak drewna
    float repairMsgTime = -100.0f;
    int accessFailures = 0;          // ile razy osadnicy zrezygnowali z dojścia
    bool unreachable = false;        // osadnicy nie mogą dojść do budynku
    float unreachableTime = -100.0f;
    int produced = 0;
    float settlerTimer = 0.0f;   // zamek: rekrutacja osadników

    [[nodiscard]] Vec3 door() const;
};

// --- Surowce na mapie ---
enum class NodeKind { Tree, Rock };

struct ResourceNode {
    bool alive = false;
    NodeKind kind = NodeKind::Tree;
    Entity* entity = nullptr;
    Vec3 pos{};
    int amount = 0;
    int maxAmount = 0;
    int reserved = 0;            // liczba pracowników zmierzających do surowca
    float baseScale = 1.0f;
    float radius = 3.0f;
    float fallTimer = -1.0f;     // animacja przewracania drzewa
    float blockedUntil = -1.0f;  // surowiec chwilowo nieosiągalny (pracownik nie mógł dojść)
    float shake = 0.0f;
};

struct Projectile {
    bool alive = false;
    Entity* entity = nullptr;
    Vec3 start{};
    Vec3 end{};
    float t = 0.0f;
    float duration = 1.0f;
    float arcHeight = 8.0f;
    int faction = 0;
    int targetUnit = -1;
    int targetBuilding = -1;
    int shooter = -1;
    float damage = 0.0f;
};

struct Effect {
    bool alive = false;
    Entity* entity = nullptr;
    float t = 0.0f;
    float duration = 1.0f;
    float baseScale = 1.0f;
    int kind = 0;                // 0 = znacznik rozkazu, 1 = gruz
};

struct FactionState {
    Resources res;
    int popCap = 0;
    int population = 0;
    bool defeated = false;
    int castle = -1;
    float jobTimer = 0.0f;
    float underAttackMsgTime = -100.0f;
    float popCapMsgTime = -100.0f;
    // AI
    float aiThinkTimer = 0.0f;
    int placementFailures = 0;
    float waveTimer = 0.0f;
    int waveNumber = 0;
    size_t buildOrderIndex = 0;
    float incomeAccumulator = 0.0f;
    int soldiersTrained = 0;
    int unitsLost = 0;
    int enemiesKilled = 0;
};

// Zdarzenie dźwiękowe zgłoszone przez symulację - odtwarza je aplikacja (świat nie zależy od audio).
struct SoundEvent {
    std::string name;
    Vec3 pos{};
    float volume = 1.0f;
    bool positional = true;     // false = dźwięk interfejsu (bez położenia)
};

struct GameMessage {
    std::string text;
    std::array<float, 4> color{1, 1, 1, 1};
    float time = 0.0f;
};

/**
 * Świat gry: teren, nawigacja, jednostki, budynki, surowce, walka i AI przeciwnika.
 */
class World {
public:
    World(engine3d::Engine& engine, const GameMaterials& materials);
    ~World();

    bool create(engine3d::Scene* scene, uint32_t seed, Difficulty difficulty);
    void update(float dt);

    // --- Zapytania ---
    [[nodiscard]] float heightAt(float x, float z) const;
    bool pickGround(const Ray& ray, Vec3& outHit) const;
    int pickUnit(const Ray& ray) const;
    int pickBuilding(const Ray& ray) const;
    [[nodiscard]] bool isInTerritory(int faction, const Vec3& p, float margin = 0.0f) const;
    bool canPlace(BuildingType type, const Vec3& pos, int faction, std::string* reason = nullptr, float rotY = 0.0f) const;
    [[nodiscard]] Vec3 snapToGrid(const Vec3& p) const;

    // --- Rozkazy ---
    int placeBuilding(BuildingType type, const Vec3& pos, int faction, bool instant = false, float rotY = 0.0f);
    void commandMove(const std::vector<int>& ids, const Vec3& point, bool attackMove);
    void commandAttackUnit(const std::vector<int>& ids, int target);
    void commandAttackBuilding(const std::vector<int>& ids, int target);
    bool queueTraining(int building, UnitType type, std::string* reason = nullptr);
    void demolish(int building);
    void spawnMarker(const Vec3& p);
    void setSelected(int unit, bool selected);
    void setBuildingSelected(int building, bool selected);

    // --- Dane ---
    std::vector<Unit> units;
    std::vector<Building> buildings;
    std::vector<ResourceNode> nodes;
    std::vector<Projectile> projectiles;
    std::vector<Effect> effects;
    std::array<FactionState, kFactionCount> factions{};
    std::deque<GameMessage> messages;
    std::vector<SoundEvent> soundEvents;
    std::array<float, kFactionCount> lastCombatTime{-100.0f, -100.0f};   // ostatnia walka z udziałem frakcji

    // --- Debug ---
    struct DebugStats {
        int pathRequests = 0;        // wywołania A* od początku gry
        int directPaths = 0;         // ścieżki w linii prostej (bez A*)
        int stallsDetected = 0;      // wykryte zawieszenia jednostek
        int stallsRecovered = 0;     // zawieszenia rozwiązane automatycznie
    } debugStats;
    static constexpr float kStallSeconds = 6.0f;
    [[nodiscard]] static bool isTravelState(UnitState s);
    [[nodiscard]] static const char* stateName(UnitState s);
    std::string describeUnit(int id) const;

    float gameTime = 0.0f;
    int winner = -1;
    bool aiEnabled = true;
    bool aiControlsPlayer = false;   // tryb autotestu: AI gra również za gracza
    Difficulty difficulty = Difficulty::Normal;
    Vec3 startPos[kFactionCount]{};

    [[nodiscard]] engine3d::Terrain* terrain() const { return terrain_.get(); }
    [[nodiscard]] const engine3d::NavGrid& nav() const { return nav_; }
    // Kolor terenu do minimapy (rozdzielczość mapSize x mapSize).
    [[nodiscard]] const std::vector<Vec3>& minimapColors() const { return minimapColors_; }
    [[nodiscard]] int minimapSize() const { return minimapSize_; }
    void pushMessage(const std::string& text, std::array<float, 4> color = {1, 1, 1, 1});
    void emitSound(const char* name, const Vec3& pos, float volume = 1.0f, bool positional = true);

private:
    // Tworzenie świata
    void generateHeightmap(uint32_t seed);
    void createTerrainEntity();
    void buildNavGrid();
    void placeResources();
    void buildMinimap();

    // Encje
    Entity* makeEntity(const std::string& prefix, const std::string& mesh);
    void destroyEntity(Entity*& e);
    int spawnUnit(UnitType type, int faction, const Vec3& pos);
    int addNode(NodeKind kind, const Vec3& pos, int variant, float scale);
    void killUnit(int id, int killerFaction);
    void destroyBuilding(int id, int killerFaction);
    void completeBuilding(int id);

    // Symulacja
    void updateFactions(float dt);
    void dispatchJobs(int faction);
    void updateUnit(int id, float dt);
    void updateSettler(Unit& u, int id, float dt);
    void updateSoldier(Unit& u, int id, float dt);
    void updateBuildings(float dt);
    void updateProjectiles(float dt);
    void updateEffects(float dt);
    void updateNodes(float dt);
    void separateUnits(float dt);
    void syncVisuals(float dt);
    void updateAI(int faction, float dt);
public:
    // Ile osadników aktualnie naprawia budynek (do panelu informacji).
    [[nodiscard]] int repairersOf(int building) const;
    // Dla testów: tworzy jednostkę w podanym miejscu.
    int debugSpawnUnit(UnitType type, int faction, const Vec3& pos) { return spawnUnit(type, faction, pos); }
    [[nodiscard]] bool needsRepairPublic(const Building& b) const { return needsRepair(b); }
private:
    void checkVictory();
    void rebuildTerritory();

    // Pomocnicze
    bool followPath(Unit& u, float dt, float reachDist);
    void setPath(Unit& u, const Vec3& target);
    int findNearestNode(NodeKind kind, const Vec3& from, float radius) const;
    int countNodesInRange(NodeKind kind, const Vec3& from, float radius) const;
    void markNodeUnreachable(Unit& u);
    void reportAccessFailure(int building);
    void markUnreachable(int building);
    void checkWorkResources(Building& b);
    void recoverStall(Unit& u, int id);
    [[nodiscard]] bool buildingReachableFrom(const Building& b, const Vec3& from) const;
    [[nodiscard]] bool isWorkerOnFarm(const Unit& u, int id) const;
    [[nodiscard]] bool needsRepair(const Building& b) const;
    void updateHealing(Unit& u, float dt);
    int findIdleSettler(int faction, const Vec3& near) const;
    int findEnemyUnitNear(int faction, const Vec3& p, float radius) const;
    int findEnemyBuildingNear(int faction, const Vec3& p, float radius) const;
    void damageUnit(int target, float amount, int attackerFaction, int attackerUnit);
    void damageBuilding(int target, float amount, int attackerFaction, int attackerUnit);
    void alertDefenders(int faction, const Vec3& p, int attackerUnit);
    void fireArrow(const Vec3& from, int faction, int targetUnit, int targetBuilding, float damage, int shooter = -1);
    [[nodiscard]] float distanceToBuilding(const Vec3& p, const Building& b) const;
    [[nodiscard]] bool buildingAreaFree(BuildingType type, const Vec3& pos) const;
    void setUnitMesh(Unit& u, const std::string& mesh);
    void becomeIdleSettler(Unit& u);
    bool findPlacementNear(BuildingType type, int faction, const Vec3& center, float minR, float maxR, Vec3& out);
    float randf(float a, float b);

    engine3d::Engine& engine_;
    GameMaterials materials_;
    engine3d::Scene* scene_ = nullptr;
    std::shared_ptr<engine3d::Terrain> terrain_;
    std::vector<float> heightmap_;
    engine3d::NavGrid nav_;
    std::mt19937 rng_;
    uint64_t entityCounter_ = 0;
    Entity* territoryEntity_[kFactionCount]{};
    bool territoryDirty_ = true;
    bool regionsDirty_ = true;       // przeszkody się zmieniły - przelicz obszary dostępności
    int territoryMeshVersion_ = 0;
    std::vector<Vec3> minimapColors_;
    int minimapSize_ = 64;
    uint32_t seed_ = 0;
};

} // namespace settlers
