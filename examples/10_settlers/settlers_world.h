#pragma once
/**
 * settlers_world.h - symulacja świata gry "Osadnicy".
 *
 * Klasa World przechowuje i aktualizuje cały stan gry: teren, siatkę nawigacji, jednostki,
 * budynki, surowce, pociski, gospodarkę obu frakcji i AI przeciwnika. Nie obsługuje
 * wejścia gracza ani interfejsu - to robi SettlersApp (settlers_app.h), który wydaje
 * światu rozkazy (commandMove, placeBuilding...) i czyta jego stan (units, buildings...).
 *
 * IDENTYFIKATORY
 * --------------
 * Jednostki, budynki i surowce leżą w wektorach (units, buildings, nodes), a ich indeks jest
 * identyfikatorem. Martwych obiektów nie usuwamy z wektora - mają alive = false. Dzięki temu
 * odwołania typu "cel ataku = jednostka nr 17" pozostają poprawne; zawsze sprawdzaj alive.
 *
 * GRAFIKA
 * -------
 * Każdy obiekt ma encję 3D w scenie silnika (Entity + MeshComponent). Symulacja zmienia
 * pola obiektów (pos, yaw...), a syncVisuals przepisuje je raz na krok do encji.
 *
 * PLIKI IMPLEMENTACJI (wszystkie zawierają metody tej samej klasy World)
 * --------------------------------------------------------------------
 *   world_create.cpp    - generowanie świata
 *   world_entities.cpp  - tworzenie i usuwanie obiektów
 *   world_queries.cpp   - zapytania (picking, gdzie można budować, wyszukiwanie)
 *   world_movement.cpp  - ścieżki i ruch
 *   world_update.cpp    - główny krok symulacji
 *   world_settlers.cpp  - automat stanów osadnika
 *   world_combat.cpp    - walka i rozkazy wojskowe
 *   world_ai.cpp        - komputerowy przeciwnik
 */

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

// ======================================================================================
// Jednostki
// ======================================================================================

// Stan jednostki - steruje tym, co robi w każdej klatce (automat stanów).
enum class UnitState {
    Idle,           // bezczynność (osadnik kręci się koło zamku, żołnierz stoi na posterunku)
    Moving,         // marsz do punktu (żołnierz)
    GoBuild,        // osadnik idzie na plac budowy / do naprawy
    Building,       // osadnik buduje lub naprawia
    GoToWork,       // osadnik idzie do warsztatu, w którym będzie pracował
    WorkWait,       // pracownik czeka przy warsztacie na kolejny cykl
    GoGather,       // pracownik idzie do surowca (drzewo / skała / pole)
    Gathering,      // pracownik zbiera surowiec
    ReturnHome,     // pracownik niesie surowiec do warsztatu
    Attack,         // żołnierz atakuje cel
};

struct Unit {
    bool alive = false;
    UnitType type = UnitType::Settler;
    int faction = 0;
    Entity* entity = nullptr;    // model postaci
    Entity* ring = nullptr;      // pierścień zaznaczenia pod stopami

    Vec3 pos{};
    float yaw = 0.0f;            // kierunek patrzenia (obrót wokół Y)
    float hp = 1.0f;
    UnitState state = UnitState::Idle;

    // --- Ruch ---
    std::vector<Vec3> path;      // punkty trasy (y nieistotne)
    size_t pathIndex = 0;        // następny punkt trasy
    Vec3 goal{};                 // cel ostatniego setPath
    bool attackMove = false;     // marsz bojowy: atakuje napotkanych wrogów po drodze
    float repathTimer = 0.0f;    // pościg: kiedy odświeżyć ścieżkę
    float stuckTimer = 0.0f;     // jak długo nie udaje się dojść do celu

    // --- Praca (osadnicy) ---
    int building = -1;           // plac budowy lub warsztat
    int resourceNode = -1;       // drzewo / skała, do której idzie lub którą obrabia
    CarryType carry = CarryType::None;
    Vec3 workSpot{};             // rolnik: miejsce na polu

    // --- Walka (żołnierze) ---
    int targetUnit = -1;
    int targetBuilding = -1;
    float attackCooldown = 0.0f; // czas do kolejnego ciosu / strzału
    Vec3 guardPos{};             // posterunek - tu wraca po walce

    // --- Liczniki czasu i animacja ---
    float timer = 0.0f;          // wielozadaniowy licznik (czas pracy, odpoczynku, wykrywania wrogów)
    float animTime = 0.0f;
    float hitFlash = 0.0f;       // krótki podskok po otrzymaniu obrażeń
    float sfxTimer = 0.0f;       // odstęp między dźwiękami pracy (siekiera, kilof, młotek)
    float lastDamagedTime = -100.0f;
    bool moving = false;         // czy w tej klatce się przemieszczał (animacja kroku)
    bool selected = false;
    std::string meshName;

    // --- Wykrywanie zablokowania ---
    Vec3 progressPos{};          // tłok: czy jednostka posuwa się naprzód
    float progressTimer = 0.0f;
    float ghostTimer = 0.0f;     // > 0: przeciska się przez tłum (ignoruje rozpychanie)
    Vec3 stallPos{};             // zawieszenie: pozycja odniesienia
    float stallTime = 0.0f;      // ile sekund jednostka "w drodze" nie zmienia położenia
    int stallCount = 0;          // ile razy wykryto zawieszenie (debug)
};

// ======================================================================================
// Budynki
// ======================================================================================

struct Building {
    bool alive = false;
    BuildingType type = BuildingType::House;
    int faction = 0;
    Entity* entity = nullptr;    // model budynku
    Entity* scaffold = nullptr;  // rusztowanie (tylko w trakcie budowy)
    Entity* ring = nullptr;      // pierścień zaznaczenia
    Vec3 pos{};
    float rotY = 0.0f;           // obrót (wielokrotność 90°) - decyduje, gdzie są drzwi
    float hp = 1.0f;
    bool constructed = false;
    float progress = 0.0f;       // postęp budowy 0..1
    int worker = -1;             // pracownik warsztatu (indeks jednostki)
    int produced = 0;            // ile razy pracownik dostarczył surowiec
    float lastDamagedTime = -100.0f;
    bool selected = false;

    // Koszary
    std::deque<UnitType> trainQueue;
    float trainTimer = 0.0f;
    // Wieża / zamek
    float attackCooldown = 0.0f;
    // Zamek
    float settlerTimer = 0.0f;   // odliczanie do przyjęcia kolejnego osadnika

    // Drwal / kamieniarz: surowce w zasięgu
    bool lacksResources = false;     // brak dostępnego surowca w zasięgu (stan bieżący)
    int resourcesInRange = -1;       // liczba drzew/skał w zasięgu pracy (-1 = jeszcze nie liczono)
    float lowResourcesMsgTime = -100.0f;
    float resourceCheckTimer = 0.0f;

    // Naprawa
    float repairCredit = 0.0f;       // PŻ, które można jeszcze naprawić z opłaconego drewna
    bool repairBlocked = false;      // naprawa wstrzymana - brak drewna
    float repairMsgTime = -100.0f;

    // Dostępność
    int accessFailures = 0;          // ile razy osadnicy zrezygnowali z dojścia
    bool unreachable = false;        // osadnicy nie mogą dojść do budynku
    float unreachableTime = -100.0f;

    // Punkt przed wejściem (z przodu budynku, uwzględnia obrót).
    [[nodiscard]] Vec3 door() const;
};

// ======================================================================================
// Pozostałe obiekty świata
// ======================================================================================

enum class NodeKind { Tree, Rock };

// Surowiec na mapie: drzewo albo skała.
struct ResourceNode {
    bool alive = false;
    NodeKind kind = NodeKind::Tree;
    Entity* entity = nullptr;
    Vec3 pos{};
    int amount = 0;              // ile porcji surowca zostało
    int maxAmount = 0;
    int reserved = 0;            // liczba pracowników zmierzających do surowca
    float baseScale = 1.0f;
    float radius = 3.0f;
    float fallTimer = -1.0f;     // >= 0: animacja przewracania ściętego drzewa
    float blockedUntil = -1.0f;  // surowiec chwilowo nieosiągalny (pracownik nie mógł dojść)
    float shake = 0.0f;          // drganie drzewa przy ścinaniu
};

// Strzała lecąca po łuku od strzelca do celu.
struct Projectile {
    bool alive = false;
    Entity* entity = nullptr;
    Vec3 start{};
    Vec3 end{};
    float t = 0.0f;              // postęp lotu 0..1
    float duration = 1.0f;
    float arcHeight = 8.0f;
    int faction = 0;
    int targetUnit = -1;
    int targetBuilding = -1;
    int shooter = -1;            // kto strzelił (zaatakowany może odpowiedzieć)
    float damage = 0.0f;
};

// Krótkotrwały efekt wizualny.
struct Effect {
    bool alive = false;
    Entity* entity = nullptr;
    float t = 0.0f;
    float duration = 1.0f;
    float baseScale = 1.0f;
    int kind = 0;                // 0 = znacznik rozkazu, 1 = gruzowisko
};

// Stan jednej frakcji (gracz / wróg): gospodarka, statystyki, pamięć AI.
struct FactionState {
    Resources res;
    int popCap = 0;              // limit ludności (zamek + domy)
    int population = 0;
    bool defeated = false;
    int castle = -1;
    float jobTimer = 0.0f;       // odliczanie do kolejnego przydziału prac
    float underAttackMsgTime = -100.0f;
    float popCapMsgTime = -100.0f;
    int unitsLost = 0;
    int enemiesKilled = 0;
    // Pamięć AI
    float aiThinkTimer = 0.0f;
    int placementFailures = 0;
    float waveTimer = 0.0f;
    int waveNumber = 0;
    size_t buildOrderIndex = 0;
    float incomeAccumulator = 0.0f;
    int soldiersTrained = 0;
};

// Zdarzenie dźwiękowe zgłoszone przez symulację - odtwarza je aplikacja (świat nie zależy od audio).
struct SoundEvent {
    std::string name;
    Vec3 pos{};
    float volume = 1.0f;
    bool positional = true;     // false = dźwięk interfejsu (bez położenia)
};

// Komunikat dla gracza ("Budowa ukończona: Dom").
struct GameMessage {
    std::string text;
    std::array<float, 4> color{1, 1, 1, 1};
    float time = 0.0f;          // czas gry, w którym się pojawił
};

// ======================================================================================
// Świat
// ======================================================================================

class World {
public:
    World(engine3d::Engine& engine, const GameMaterials& materials);
    ~World();

    // Tworzy nowy świat (teren, surowce, bazy) w podanej scenie.            [world_create.cpp]
    bool create(engine3d::Scene* scene, uint32_t seed, Difficulty difficulty);
    // Jeden krok symulacji o dt sekund.                                     [world_update.cpp]
    void update(float dt);

    // ----------------------------------------------------------------------------------
    // Zapytania                                                             [world_queries.cpp]
    // ----------------------------------------------------------------------------------

    [[nodiscard]] float heightAt(float x, float z) const;
    // Punkt terenu (lub wody) trafiony promieniem z kamery.
    bool pickGround(const Ray& ray, Vec3& outHit) const;
    // Najbliższa jednostka / budynek trafiony promieniem (-1 = brak).
    int pickUnit(const Ray& ray) const;
    int pickBuilding(const Ray& ray) const;
    // Czy punkt leży na terytorium frakcji (margin > 0 wymaga zapasu od granicy).
    [[nodiscard]] bool isInTerritory(int faction, const Vec3& p, float margin = 0.0f) const;
    // Czy można postawić budynek; w reason powód odmowy ("Teren zbyt nierówny"...).
    bool canPlace(BuildingType type, const Vec3& pos, int faction, std::string* reason = nullptr, float rotY = 0.0f) const;
    // Wyrównanie punktu do siatki nawigacji (budynki stoją równo z komórkami).
    [[nodiscard]] Vec3 snapToGrid(const Vec3& p) const;
    // Ile osadników aktualnie naprawia budynek (do panelu informacji).
    [[nodiscard]] int repairersOf(int building) const;
    [[nodiscard]] bool needsRepairPublic(const Building& b) const { return needsRepair(b); }

    // ----------------------------------------------------------------------------------
    // Rozkazy (wywoływane przez gracza lub AI)
    // ----------------------------------------------------------------------------------

    // Stawia plac budowy (instant = od razu gotowy budynek, bez kosztów).  [world_entities.cpp]
    int placeBuilding(BuildingType type, const Vec3& pos, int faction, bool instant = false, float rotY = 0.0f);
    // Burzy budynek gracza (zwrot 50% kosztów).                          [world_entities.cpp]
    void demolish(int building);
    // Marsz wojska w szyku (attackMove = atakuj napotkanych wrogów).       [world_combat.cpp]
    void commandMove(const std::vector<int>& ids, const Vec3& point, bool attackMove);
    void commandAttackUnit(const std::vector<int>& ids, int target);
    void commandAttackBuilding(const std::vector<int>& ids, int target);
    // Kolejkuje szkolenie w koszarach (koszt pobierany od razu).           [world_combat.cpp]
    bool queueTraining(int building, UnitType type, std::string* reason = nullptr);
    // Zielony pierścień w miejscu rozkazu.                                 [world_entities.cpp]
    void spawnMarker(const Vec3& p);
    void setSelected(int unit, bool selected);
    void setBuildingSelected(int building, bool selected);

    // Komunikat dla gracza i zdarzenie dźwiękowe dla aplikacji.
    void pushMessage(const std::string& text, std::array<float, 4> color = {1, 1, 1, 1});
    void emitSound(const char* name, const Vec3& pos, float volume = 1.0f, bool positional = true);

    // ----------------------------------------------------------------------------------
    // Dane świata (czytane przez aplikację)
    // ----------------------------------------------------------------------------------

    std::vector<Unit> units;
    std::vector<Building> buildings;
    std::vector<ResourceNode> nodes;
    std::vector<Projectile> projectiles;
    std::vector<Effect> effects;
    std::array<FactionState, kFactionCount> factions{};
    std::deque<GameMessage> messages;
    std::vector<SoundEvent> soundEvents;   // aplikacja odtwarza je i czyści co klatkę
    std::array<float, kFactionCount> lastCombatTime{-100.0f, -100.0f};   // ostatnia walka z udziałem frakcji

    float gameTime = 0.0f;
    int winner = -1;                 // -1 = gra trwa, inaczej indeks zwycięskiej frakcji
    bool aiEnabled = true;           // AI przeciwnika
    bool aiControlsPlayer = false;   // AI gra też za gracza (pokaz, autotest)
    Difficulty difficulty = Difficulty::Normal;
    Vec3 startPos[kFactionCount]{};

    [[nodiscard]] engine3d::Terrain* terrain() const { return terrain_.get(); }
    [[nodiscard]] const engine3d::NavGrid& nav() const { return nav_; }
    // Kolor terenu do minimapy (rozdzielczość minimapSize x minimapSize).
    [[nodiscard]] const std::vector<Vec3>& minimapColors() const { return minimapColors_; }
    [[nodiscard]] int minimapSize() const { return minimapSize_; }

    // ----------------------------------------------------------------------------------
    // Debug                                                                [world_queries.cpp]
    // ----------------------------------------------------------------------------------

    struct DebugStats {
        int pathRequests = 0;        // wywołania A* od początku gry
        int directPaths = 0;         // ścieżki w linii prostej (bez A*)
        int stallsDetected = 0;      // wykryte zawieszenia jednostek
        int stallsRecovered = 0;     // zawieszenia rozwiązane automatycznie
    } debugStats;
    static constexpr float kStallSeconds = 6.0f;   // po tylu sekundach bez ruchu jednostka "wisi"
    // Czy stan oznacza, że jednostka powinna się przemieszczać.
    [[nodiscard]] static bool isTravelState(UnitState s);
    [[nodiscard]] static const char* stateName(UnitState s);
    // Pełny opis jednostki (pozycja, cel, komórki nawigacji, ścieżka) do logów.
    std::string describeUnit(int id) const;
    // Dla testów: tworzy jednostkę w podanym miejscu.
    int debugSpawnUnit(UnitType type, int faction, const Vec3& pos) { return spawnUnit(type, faction, pos); }

private:
    // ----------------------------------------------------------------------------------
    // Tworzenie świata                                                      [world_create.cpp]
    // ----------------------------------------------------------------------------------

    // Heightmapa z szumu: wzgórza, góry, pierścień gór na krawędzi, płaskowyże pod bazy.
    void generateHeightmap(uint32_t seed);
    // Encja terenu z warstwami tekstur (maski liczone z wysokości i nachylenia).
    void createTerrainEntity();
    // Siatka nawigacji: woda, strome zbocza i krawędź mapy są nieprzechodnie.
    void buildNavGrid();
    // Lasy (z szumu), lasy i skały przy bazach, skały na wzgórzach.
    void placeResources();
    // Kolory terenu dla minimapy.
    void buildMinimap();

    // ----------------------------------------------------------------------------------
    // Obiekty                                                               [world_entities.cpp]
    // ----------------------------------------------------------------------------------

    // Encja 3D z siatką i wspólnym materiałem (unikalna nazwa: prefix_N).
    Entity* makeEntity(const std::string& prefix, const std::string& mesh);
    void destroyEntity(Entity*& e);
    int spawnUnit(UnitType type, int faction, const Vec3& pos);
    int addNode(NodeKind kind, const Vec3& pos, int variant, float scale);
    void killUnit(int id, int killerFaction);
    void destroyBuilding(int id, int killerFaction);
    // Koniec budowy: pełny model, usunięcie rusztowania, ewentualnie nowe terytorium.
    void completeBuilding(int id);
    // Zmienia model postaci (np. osadnik z drewnem na ramieniu).
    void setUnitMesh(Unit& u, const std::string& mesh);
    // Osadnik porzuca zadanie (zwalnia rezerwacje, przestaje być pracownikiem) i wraca do Idle.
    void becomeIdleSettler(Unit& u);
    float randf(float a, float b);

    // ----------------------------------------------------------------------------------
    // Krok symulacji                                                        [world_update.cpp]
    // ----------------------------------------------------------------------------------

    // Populacja, limit ludności, przyjmowanie osadników przez zamek, przydział prac.
    void updateFactions(float dt);
    // Przydziela wolnych osadników: budowy -> warsztaty bez pracownika -> naprawy.
    void dispatchJobs(int faction);
    // Wspólna część dla każdej jednostki (wykrywanie zawieszeń, leczenie) + rozdzielenie na typ.
    void updateUnit(int id, float dt);
    // Leczenie poza walką (szybsze przy zamku i koszarach).
    void updateHealing(Unit& u, float dt);
    // Koszary (szkolenie), wieże i zamek (strzały), kontrola surowców warsztatów.
    void updateBuildings(float dt);
    void updateEffects(float dt);
    // Przewracanie ściętych drzew, zmniejszanie się skał.
    void updateNodes(float dt);
    // Przepisuje pozycje/obroty/animacje obiektów do encji 3D.
    void syncVisuals(float dt);
    // Słupki granic terytorium obu frakcji (jedna siatka na frakcję).
    void rebuildTerritory();
    void checkVictory();
    // Drwal/kamieniarz: liczy surowce w zasięgu, ostrzega o ich końcu.
    void checkWorkResources(Building& b);
    // Surowiec nieosiągalny dla pracownika - pomijany przez 90 s.
    void markNodeUnreachable(Unit& u);
    // Osadnik nie doszedł do budynku; po 2 porażkach budynek dostaje znacznik "brak dojścia".
    void reportAccessFailure(int building);
    void markUnreachable(int building);

    // ----------------------------------------------------------------------------------
    // Osadnicy (automat stanów)                                             [world_settlers.cpp]
    // ----------------------------------------------------------------------------------

    void updateSettler(Unit& u, int id, float dt);
    void settlerIdle(Unit& u, float dt);
    void settlerGoBuild(Unit& u, float dt);
    void settlerBuild(Unit& u, float dt);
    void settlerRepair(Unit& u, Building& b, float dt);
    void settlerGoToWork(Unit& u, int id, float dt);
    void settlerWorkWait(Unit& u, int id, float dt);
    void settlerGoGather(Unit& u, int id, float dt);
    void settlerGather(Unit& u, int id, float dt);
    void settlerReturnHome(Unit& u, int id, float dt);
    // Budynek przypisany osadnikowi (budowa/praca), o ile nadal istnieje.
    Building* assignedBuilding(Unit& u);
    // Warsztat, w którym osadnik jest pracownikiem (ukończony i z tym pracownikiem).
    Building* workplaceOf(Unit& u, int id);
    // Oddanie niesionego surowca do magazynu frakcji; pracownik wraca do WorkWait.
    void deliverCarriedResource(Unit& u, Building& b);
    void faceTowards(Unit& u, const Vec3& target, float dt);
    // Dźwięk pracy co losowy odstęp [minGap, maxGap] sekund.
    void playWorkSound(Unit& u, const char* sound, const Vec3& where, float minGap, float maxGap, float volume, float dt);
    // Czy jednostka nie ma już punktów ścieżki do przejścia.
    static bool pathFinished(const Unit& u);

    // ----------------------------------------------------------------------------------
    // Walka                                                                  [world_combat.cpp]
    // ----------------------------------------------------------------------------------

    void updateSoldier(Unit& u, int id, float dt);
    // Szuka wroga w zasięgu wzroku (jednostki mają pierwszeństwo przed budynkami).
    void soldierAcquireTarget(Unit& u);
    void soldierAttack(Unit& u, int id, float dt);
    void soldierStrike(Unit& u, int id, const Vec3& targetPos, float dt);
    void soldierChase(Unit& u, const Vec3& targetPos, float dt);
    void damageUnit(int target, float amount, int attackerFaction, int attackerUnit);
    void damageBuilding(int target, float amount, int attackerFaction, int attackerUnit);
    // Wolni żołnierze frakcji w promieniu 140 ruszają na napastnika.
    void alertDefenders(int faction, const Vec3& p, int attackerUnit);
    void fireArrow(const Vec3& from, int faction, int targetUnit, int targetBuilding, float damage, int shooter = -1);
    void updateProjectiles(float dt);

    // ----------------------------------------------------------------------------------
    // AI przeciwnika                                                          [world_ai.cpp]
    // ----------------------------------------------------------------------------------

    void updateAI(int faction, float dt);
    void aiCollectIncome(int faction, float dt);
    void aiBuildNext(int faction);
    void aiTrainArmy(int faction);
    void aiLaunchAttackWave(int faction, float dt);
    [[nodiscard]] Vec3 aiHome(int faction) const;

    // ----------------------------------------------------------------------------------
    // Ruch                                                                  [world_movement.cpp]
    // ----------------------------------------------------------------------------------

    // Wyznacza ścieżkę do celu (linia prosta, a gdy przeszkoda - A*).
    void setPath(Unit& u, const Vec3& target);
    // Przesuwa jednostkę wzdłuż ścieżki; true = dotarła (z dokładnością reachDist).
    bool followPath(Unit& u, float dt, float reachDist);
    // Rozpychanie jednostek, ustępowanie drogi, wychodzenie z wnętrza budynków.
    void separateUnits(float dt);
    // Odblokowanie jednostki, która od kStallSeconds stoi w miejscu.
    void recoverStall(Unit& u, int id);

    // ----------------------------------------------------------------------------------
    // Wyszukiwanie i warunki                                                [world_queries.cpp]
    // ----------------------------------------------------------------------------------

    int findNearestNode(NodeKind kind, const Vec3& from, float radius) const;
    int countNodesInRange(NodeKind kind, const Vec3& from, float radius) const;
    int findIdleSettler(int faction, const Vec3& near) const;
    int findEnemyUnitNear(int faction, const Vec3& p, float radius) const;
    int findEnemyBuildingNear(int faction, const Vec3& p, float radius) const;
    // Losowe próby znalezienia miejsca na budynek w pierścieniu [minR, maxR] wokół center.
    bool findPlacementNear(BuildingType type, int faction, const Vec3& center, float minR, float maxR, Vec3& out);
    // Odległość od punktu do ściany budynku (0 = wewnątrz obrysu).
    [[nodiscard]] float distanceToBuilding(const Vec3& p, const Building& b) const;
    [[nodiscard]] bool buildingAreaFree(BuildingType type, const Vec3& pos) const;
    // Czy z punktu da się dojść do któregoś boku budynku (ten sam obszar nawigacji).
    [[nodiscard]] bool buildingReachableFrom(const Building& b, const Vec3& from) const;
    // Rolnik pracujący na polu stoi wewnątrz obrysu farmy (to dozwolone).
    [[nodiscard]] bool isWorkerOnFarm(const Unit& u, int id) const;
    [[nodiscard]] bool needsRepair(const Building& b) const;

    // ----------------------------------------------------------------------------------
    // Dane wewnętrzne
    // ----------------------------------------------------------------------------------

    engine3d::Engine& engine_;
    GameMaterials materials_;
    engine3d::Scene* scene_ = nullptr;
    std::shared_ptr<engine3d::Terrain> terrain_;
    std::vector<float> heightmap_;               // wysokości 0..255 (x kScaleY = jednostki świata)
    engine3d::NavGrid nav_;
    std::mt19937 rng_;                           // generator losowy (ziarno = seed mapy)
    uint64_t entityCounter_ = 0;                 // do unikalnych nazw encji
    Entity* territoryEntity_[kFactionCount]{};   // słupki granic terytorium
    bool territoryDirty_ = true;                 // terytorium się zmieniło - przebuduj słupki
    bool regionsDirty_ = true;                   // przeszkody się zmieniły - przelicz obszary dostępności
    int territoryMeshVersion_ = 0;
    std::vector<Vec3> minimapColors_;
    int minimapSize_ = 64;
    uint32_t seed_ = 0;
};

} // namespace settlers
