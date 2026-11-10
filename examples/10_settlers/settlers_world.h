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
 *   world_roads.cpp     - drogi, flagi, tragarze i magazyny (transport towarów)
 *   world_save.cpp      - zapis i wczytanie stanu gry (JSON: ziarno mapy + listy obiektów)
 *   world_fog.cpp       - mgła wojny i zwiad (widoczność gracza, tekstura dla renderera)
 *
 * GOSPODARKA (world_roads.cpp)
 * ----------------------------
 * Pracownik warsztatu odkłada wytworzone towary na fladze przed budynkiem (najwyżej kFlagCapacity).
 * Drogi łączą flagi budynków w sieć (i przyspieszają marsz). Wolni TRAGARZE (wspólna pula, liczbę
 * ustawia gracz) dostają zlecenia:
 *   - odbiór: towar z flagi warsztatu niesiony całą trasą do najbliższego magazynu z miejscem,
 *   - dostawa: materiały z magazynu na plac budowy albo koszt szkolenia do koszar.
 * Surowce są dostępne dopiero w magazynie - FactionState::res to suma zapasów wszystkich magazynów.
 * Gdy flaga przed warsztatem jest pełna (brak drogi lub tragarzy), warsztat wstrzymuje produkcję.
 */

#include <nlohmann/json_fwd.hpp>
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
    GoToRoad,       // tragarz idzie (bez ładunku) po towar albo wraca do magazynu
    CarrierWait,    // tragarz czeka przy magazynie na zlecenie
    CarrierWalk,    // tragarz niesie towar do celu zlecenia
    GoGarrison,     // żołnierz idzie do wieży, którą ma obsadzić
    Garrison,       // żołnierz w załodze wieży (niewidoczny, wieża strzela)
};
constexpr int kUnitStateCount = static_cast<int>(UnitState::Garrison) + 1;

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

    // --- Transport (tragarze) ---
    int atFlag = -1;             // flaga, przy której stoi (albo do której właśnie idzie)
    int jobFlag = -1;            // zlecenie odbioru: flaga, z której zabiera towar
    int jobOrder = -1;           // zlecenie dostawy: indeks w World::orders
    int jobDest = -1;            // budynek docelowy (magazyn przy odbiorze, odbiorca przy dostawie)
    int carryCount = 0;          // ile towarów niesie / ma podnieść (1-2, wszystkie typu carry)

    // --- Walka (żołnierze) ---
    int targetUnit = -1;
    int targetBuilding = -1;
    float attackCooldown = 0.0f; // czas do kolejnego ciosu / strzału
    Vec3 guardPos{};             // posterunek - tu wraca po walce
    int homeTower = -1;          // wieża, w której załodze jest (lub do której idzie / wraca po wypadzie)

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
    float placedTime = 0.0f;     // kiedy postawiono (plac budowy) - AI burzy place, które długo stoją bez drogi
    float constructedTime = 0.0f;   // kiedy ukończono (pomiar wydajności warsztatów)
    bool selected = false;

    // Koszary
    std::deque<UnitType> trainQueue;
    float trainTimer = 0.0f;
    bool autoTrain = false;          // automatyczna rekrutacja (kolejka uzupełniana sama)
    int swordShare = 70;             // udział mieczników w %, reszta to łucznicy
    int autoSwords = 0, autoArchers = 0;   // ilu zamówiła rekrutacja automatyczna (pilnowanie proporcji)
    float autoTrainTimer = 0.0f;
    float autoTrainMsgTime = -100.0f;
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

    // Transport i magazyn
    int flag = -1;                   // flaga przed wejściem (tu pracownik odkłada towary)
    Resources stock;                 // zapasy (tylko zamek i magazyn)
    int incoming = 0;                // magazyn: towary w drodze do niego (zarezerwowane miejsce)
    Resources delivered;             // dowiezione materiały (plac budowy, koszary)
    bool trainPaid = false;          // koszary: koszt bieżącego szkolenia już pobrany z dostaw
    bool outputBlocked = false;      // warsztat: flaga pełna - produkcja wstrzymana
    bool inputStarved = false;       // warsztat: brak towarów wejściowych (żywność, surowiec)
    float inputMsgTime = -100.0f;
    float inputRequestTimer = 0.0f;
    float outputMsgTime = -100.0f;

    // Punkt przed wejściem (z przodu budynku, uwzględnia obrót).
    [[nodiscard]] Vec3 door() const;
};

// ======================================================================================
// Drogi i flagi (transport towarów)
// ======================================================================================

// Flaga: węzeł sieci dróg. Leży przed wejściem budynku albo na drodze (skrzyżowanie, podział drogi).
struct Flag {
    bool alive = false;
    int faction = 0;
    Vec3 pos{};
    int building = -1;               // budynek, którego to flaga (-1 = zwykła flaga na drodze)
    std::vector<CarryType> goods;    // towary czekające na odbiór (najwyżej kFlagCapacity)
    std::array<int, kGoodTypeCount + 1> reserved{};   // ile towarów danego rodzaju zarezerwowali tragarze
    std::vector<int> roads;          // odcinki dróg zaczynające się / kończące na tej fladze
    bool connected = false;          // drogi prowadzą do jakiegoś magazynu (nawet pełnego)
    bool castleLinked = false;       // drogi prowadzą do zamku (dla flagi Magazynu: czy jest włączony w sieć)
    Entity* entity = nullptr;
    std::array<Entity*, kFlagCapacity> goodEntities{};   // modele leżących towarów (tworzone w miarę potrzeby)
    bool selected = false;
};

// Odcinek drogi między dwiema flagami. Obsługuje go jeden tragarz.
struct Road {
    bool alive = false;
    int faction = 0;
    int flagA = -1, flagB = -1;
    std::vector<Vec3> points;        // trasa od flagA do flagB (punkty co ok. 2 jednostki)
    float length = 0.0f;
    Entity* entity = nullptr;        // ubity trakt na terenie
};

// Zlecenie dostawy: towar z magazynu do odbiorcy (plac budowy, koszary).
// "Opłacone" (funded) = towar został już zdjęty z zapasu magazynu source.
struct TransportOrder {
    bool alive = false;
    int faction = 0;
    CarryType type = CarryType::Wood;
    int amount = 1;                  // 1-2 sztuki
    int source = -1;                 // magazyn, z którego tragarz zabierze towar
    int sourceFlag = -1;             // albo flaga, na której towar leży (zarezerwowany w Flag::reserved)
    int dest = -1;                   // odbiorca
    int carrier = -1;                // tragarz realizujący zlecenie
    bool funded = false;
    bool picked = false;             // tragarz już niesie towar
};

// Podsumowanie transportu frakcji (panel gospodarki, logi).
struct TransportStats {
    int carriers = 0;                // wszyscy tragarze
    int busy = 0;                    // w trakcie zlecenia
    int target = 0;                  // docelowa liczba tragarzy
    int goodsWaiting = 0;            // towary na flagach czekające na odbiór
    int ordersWaiting = 0;           // dostawy bez tragarza
    int ordersUnfunded = 0;          // dostawy czekające na towar w magazynie
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
    float growth = 1.0f;         // < 1: sadzonka posadzona przez leśniczego (rośnie, nie można jej ściąć)
    int variant = 0;             // wariant modelu (do odtworzenia sadzonki po wczytaniu zapisu)
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
    float garrisonTimer = 0.0f;  // odliczanie do obsadzania pustych wież
    float underAttackMsgTime = -100.0f;
    float popCapMsgTime = -100.0f;
    int unitsLost = 0;
    int enemiesKilled = 0;
    // Pamięć AI
    float aiThinkTimer = 0.0f;
    int aiPlacementFailures = 0;   // nieudane próby postawienia z rzędu (-> wieża poszerzająca teren)
    int aiExpandFor = -1;          // BuildingType, dla którego zabrakło miejsca/surowców (-1 = brak)
    float waveTimer = 0.0f;
    int waveNumber = 0;
    int soldiersTrained = 0;
    // Tragarze: docelowa liczba (gracz ustawia w oknie gospodarki) albo dobierana automatycznie.
    int carrierTarget = 6;
    bool carrierAuto = true;
    // Limity zapasu w magazynach dla każdego towaru (indeks = CarryType).
    StockLimits stockLimit = defaultStockLimits();
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
    // Zapis i wczytanie stanu gry (JSON).                                   [world_save.cpp]
    // Zapis: ziarno mapy + listy budynków, jednostek, flag, dróg, zleceń, stan frakcji i zmiany w surowcach.
    // Wczytanie: ląd i surowce z ziarna, potem odtworzenie obiektów (te same indeksy - odwołania się zgadzają).
    void saveState(nlohmann::json& out) const;
    bool loadState(engine3d::Scene* scene, const nlohmann::json& in, std::string* error = nullptr);
    [[nodiscard]] uint32_t seed() const { return seed_; }
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
    // Właściciel terenu w punkcie (-1 = ziemia niczyja). Gdy zasięgi budynków wojskowych się
    // nakładają, teren należy do budynku, w którego zasięgu punkt leży "najgłębiej" (promień - odległość),
    // więc nowa wieża przy granicy odbiera wrogowi część jego ziemi.
    [[nodiscard]] int territoryOwnerAt(const Vec3& p) const;
    // Czy punkt leży na terytorium frakcji (margin > 0 wymaga zapasu od granicy,
    // margin < 0 - wystarczy, że terytorium jest w promieniu |margin| od punktu).
    [[nodiscard]] bool isInTerritory(int faction, const Vec3& p, float margin = 0.0f) const;
    // Czy można postawić budynek; w reason powód odmowy ("Teren zbyt nierówny"...).
    bool canPlace(BuildingType type, const Vec3& pos, int faction, std::string* reason = nullptr, float rotY = 0.0f) const;
    // Wyrównanie punktu do siatki nawigacji (budynki stoją równo z komórkami).
    [[nodiscard]] Vec3 snapToGrid(const Vec3& p) const;
    // Ile osadników aktualnie naprawia budynek (do panelu informacji).
    [[nodiscard]] int repairersOf(int building) const;
    [[nodiscard]] bool needsRepairPublic(const Building& b) const { return needsRepair(b); }

    // ----------------------------------------------------------------------------------
    // Drogi, flagi, magazyny                                                [world_roads.cpp]
    // ----------------------------------------------------------------------------------

    // Wyznacza trasę nowej drogi od flagi do punktu (bez budowania): punkty co ~2 jednostki.
    // Cel blisko istniejącej flagi jest do niej "przyciągany". false + reason = nie da się.
    bool planRoad(int faction, int fromFlag, const Vec3& to, std::vector<Vec3>& outPoints, std::string* reason = nullptr) const;
    // Buduje drogę od flagi do punktu: po drodze stawia flagi co kRoadSegmentMax i na przecięciach
    // z innymi drogami. Zwraca flagę końcową (-1 = nie udało się).
    int buildRoad(int faction, int fromFlag, const Vec3& to, std::string* reason = nullptr);
    // Łączy flagę drogą z najbliższą flagą sieci prowadzącej do magazynu (AI i przycisk gracza).
    bool autoConnectFlag(int flag, std::string* reason = nullptr);
    // Usuwa flagę razem z dochodzącymi drogami (flaga budynku zostaje - usuwane są tylko drogi).
    void removeFlag(int flag);
    void removeRoad(int road);
    // Flaga frakcji najbliższa punktowi w promieniu radius (-1 = brak).
    [[nodiscard]] int flagNear(int faction, const Vec3& p, float radius) const;
    // Czy można postawić tu nową flagę.
    [[nodiscard]] bool canPlaceFlag(int faction, const Vec3& p, std::string* reason = nullptr) const;
    // Czy flaga ma połączenie z magazynem (z wolnym miejscem albo bez).
    [[nodiscard]] bool flagConnected(int flag) const;
    // Czy budynek jest włączony w sieć dróg: warsztat - droga do magazynu, Magazyn - droga do zamku.
    [[nodiscard]] bool buildingLinked(const Building& b) const;
    void setFlagSelected(int flag, bool selected);
    // Zapłata z magazynów (najpierw zamek); false = za mało surowców.
    bool spend(int faction, const Resources& cost);
    // Dodanie surowców do magazynów z wolnym miejscem (nadmiar przepada); zwraca, ile się zmieściło.
    int deposit(int faction, const Resources& r);
    // Łączna pojemność i zapełnienie magazynów frakcji.
    [[nodiscard]] int storageCapacityOf(int faction) const;
    [[nodiscard]] int storageUsedOf(int faction) const;
    // Dla testów: ustawia zapasy zamku (bez limitu pojemności).
    void debugSetStock(int faction, const Resources& r);
    // Przelicza FactionState::res jako sumę zapasów magazynów.
    void refreshFactionResources();
    // Liczba tragarzy, zlecenia, towary czekające na odbiór.
    [[nodiscard]] TransportStats transportStats(int faction) const;
    // Docelowa liczba tragarzy (w trybie Auto liczona z liczby warsztatów i zleceń).
    [[nodiscard]] int carrierTargetOf(int faction) const;
    // Zamawia materiały dla budynku (plac budowy, koszary): zdejmuje je z magazynów i tworzy zlecenia dostaw.
    void orderMaterials(int building, const Resources& r);
    // Czy przy fladze są towary czekające na odbiór (nie licząc zarezerwowanych).
    [[nodiscard]] int flagAvailableGoods(const Flag& fl) const;
    // Towary do wydania: zapas magazynów + wolne towary na flagach połączonych drogami (tragarz dowiezie je
    // prosto do odbiorcy). Tym sprawdzamy "czy stać nas" na budynek i szkolenie.
    [[nodiscard]] Resources spendable(int faction) const;
    // Testy planisty AI.
    [[nodiscard]] int countNodesInRangePublic(NodeKind kind, const Vec3& p, float r) const { return countNodesInRange(kind, p, r); }
    BuildingType aiPlanForPublic(int faction, CarryType good) { return aiPlanFor(faction, aiEconomy(faction), good, 0); }
    // Odległości po drogach od flagi (diagnostyka, testy).
    void networkDistancesPublic(int fromFlag, std::vector<float>& dist, std::vector<int>& prevRoad) const {
        networkDistances(fromFlag, dist, prevRoad);
    }
    // Zapas "do limitu": towary w magazynach + niesione właśnie do magazynów.
    [[nodiscard]] Resources storeBound(int faction) const;
    // Ile towarów z flagi tragarze mogą teraz zanieść do magazynu (limity zapasu nieprzekroczone).
    [[nodiscard]] int pickupableGoods(const Flag& fl, const Resources& bound) const;
    // Dlaczego towary czekają na fladze warsztatu (znaczniki i komunikaty dla gracza).
    enum class FlagBlock { None, NoRoad, StorageFull, StockLimit, Carriers };
    [[nodiscard]] FlagBlock flagBlockReason(int flag) const;

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
    // Obsadzenie własnej wieży (do garrisonCapacity żołnierzy; reszta zostaje).           [world_combat.cpp]
    int commandGarrison(const std::vector<int>& ids, int tower);
    // Załoga wychodzi z wieży i staje przed nią (pusta wieża zaraz dostanie jednego żołnierza z powrotem).
    void releaseGarrison(int tower);
    // Załoga wieży: żołnierze w środku / przypisani (w środku + idący + na wypadzie).
    [[nodiscard]] int garrisonOf(int tower) const;
    [[nodiscard]] int garrisonAssigned(int tower) const;
    [[nodiscard]] static int garrisonCapacity(BuildingType type) { return type == BuildingType::Tower ? 3 : 0; }
    // Czy wieżę można zdobyć (ukończona wieża - żołnierze jej nie burzą, tylko szturmują).
    [[nodiscard]] bool isCapturable(const Building& b) const {
        return b.alive && b.constructed && garrisonCapacity(b.type) > 0;
    }
    // Kolejkuje szkolenie w koszarach (koszt pobierany od razu).           [world_combat.cpp]
    bool queueTraining(int building, UnitType type, std::string* reason = nullptr);
    // Rekrutacja automatyczna koszar: uzupełnia kolejkę (do 2) wg proporcji mieczników i łuczników.
    void autoRecruit(int building);
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
    std::vector<Flag> flags;
    std::vector<Road> roads;
    std::vector<TransportOrder> orders;
    std::array<FactionState, kFactionCount> factions{};
    std::deque<GameMessage> messages;
    std::vector<SoundEvent> soundEvents;   // aplikacja odtwarza je i czyści co klatkę
    std::array<float, kFactionCount> lastCombatTime{-100.0f, -100.0f};   // ostatnia walka z udziałem frakcji

    float gameTime = 0.0f;
    int winner = -1;                 // -1 = gra trwa, inaczej indeks zwycięskiej frakcji
    bool aiEnabled = true;           // AI przeciwnika
    bool aiControlsPlayer = false;   // AI gra też za gracza (pokaz, autotest)
    bool aiLog = false;              // decyzje AI do logu ([ai] ...) - autoplay
    // Mgła wojny (world_fog.cpp): teren i jednostki wroga widać tylko tam, gdzie dotarli ludzie gracza.
    bool fogOfWar = false;
    static constexpr int kFowSize = 256;   // siatka widoczności kFowSize^2 (= Renderer::kFogOfWarSize)
    [[nodiscard]] bool visibleToPlayer(const Vec3& p) const;   // w zasięgu wzroku teraz
    [[nodiscard]] bool exploredByPlayer(const Vec3& p) const;  // kiedyś widziane
    // Tekstura widoczności dla renderera (0 / 128 / 255) i jej wersja (zmienia się, gdy dane się zmienią).
    [[nodiscard]] const std::vector<uint8_t>& fogTexture() const { return fowTexture_; }
    [[nodiscard]] uint64_t fogVersion() const { return fowVersion_; }
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
    void debugKillUnit(int id) { killUnit(id, -1); }

private:
    std::vector<std::pair<int64_t, int>> sepKeys_;   // separateUnits: (komórka siatki, jednostka), posortowane
    float separateTimer_ = 0.0f;   // odliczanie do kolejnego rozpychania jednostek
    size_t nodesGenerated_ = 0;
    // Mgła wojny (world_fog.cpp)
    std::vector<uint8_t> fowExplored_, fowVisible_, fowTexture_;
    uint64_t fowVersion_ = 0;
    float fowTimer_ = 0.0f;
    int fowNewlyExplored_ = 0;
    [[nodiscard]] int fowCellOf(float x, float z) const;
    void revealCircle(const Vec3& center, float radius);
    void updateFogOfWar(float dt);
    [[nodiscard]] std::string fogExploredRle() const;
    void setFogExploredRle(const std::string& rle);    // ile surowców wygenerowano z ziarna (dalsze to sadzonki leśniczych)
    void createLand(engine3d::Scene* scene, uint32_t seed, Difficulty difficulty);
    void createStartingBases(Difficulty difficulty);
    void finishCreate();
    // ----------------------------------------------------------------------------------
    // Tworzenie świata                                                      [world_create.cpp]
    // ----------------------------------------------------------------------------------

    // Heightmapa z szumu: wzgórza, góry, pierścień gór na krawędzi, płaskowyże pod bazy.
    void generateHeightmap(uint32_t seed);
    // Encja terenu z warstwami tekstur (maski liczone z wysokości i nachylenia).
    void createTerrainEntity();
    // Siatka nawigacji: woda, strome zbocza i krawędź mapy są nieprzechodnie.
    // (Mapa dróg roadCells_ ma tę samą rozdzielczość co siatka nawigacji.)
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
    // Przydziela wolnych osadników: pierwsi tragarze -> budowy -> warsztaty -> pozostali tragarze -> naprawy.
    void dispatchJobs(int faction);
    int takeSettlerForJob(int faction, const Vec3& near);
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
    // Słupki granic terytorium obu frakcji (jedna siatka na frakcję); budynki, które znalazły
    // się na terenie przejętym przez wroga, płoną.
    void rebuildTerritory();
    void burnBuildingsOnLostLand();
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
    // Zużywa towary wejściowe na cykl pracy (kopalnia: żywność, przetwórnia: surowiec). false = brak.
    bool consumeInputs(Building& b);
    // Zamawia brakujące towary wejściowe warsztatu z magazynów (do kInputBuffer sztuk każdego).
    void requestInputs(int building);
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
    // Szturm wieży: wywołanie obrońcy z załogi albo (pusta wieża) wejście i przejęcie.
    void soldierAssaultTower(Unit& u, int id, float dt);
    void captureTower(int tower, int unitId);
    // Marsz do wieży i wejście do załogi.
    void soldierGoGarrison(Unit& u, float dt);
    // Puste wieże frakcji dostają po jednym wolnym żołnierzu (co 2 s).
    void autoGarrison(int faction);
    void damageUnit(int target, float amount, int attackerFaction, int attackerUnit);
    void damageBuilding(int target, float amount, int attackerFaction, int attackerUnit);
    // Wolni żołnierze frakcji w promieniu 140 ruszają na napastnika.
    void alertDefenders(int faction, const Vec3& p, int attackerUnit);
    void fireArrow(const Vec3& from, int faction, int targetUnit, int targetBuilding, float damage, int shooter = -1);
    void updateProjectiles(float dt);

    // ----------------------------------------------------------------------------------
    // Transport                                                              [world_roads.cpp]
    // ----------------------------------------------------------------------------------

    int createFlag(int faction, const Vec3& p, int building = -1);
    // Nowy odcinek drogi a-b po podanych punktach (z encją 3D).
    int createRoad(int faction, int a, int b, const std::vector<Vec3>& points);
    // Dzieli odcinek w punkcie points[index]: powstaje tam flaga, odcinek dostaje drugą połowę.
    int splitRoad(int road, size_t index);
    void buildRoadMesh(int road);
    // Mapa komórek nawigacji zajętych przez drogi (do wykrywania skrzyżowań i blokowania budowy).
    void rebuildRoadCells();
    [[nodiscard]] int roadAtCell(float x, float z) const;
    // Spójność sieci: które flagi mają drogę do magazynu (connected) i do zamku (castleLinked).
    void updateRouting();
    // Towary na flagach magazynów trafiają do środka; zlecenia dostaw bez pokrycia dostają towar.
    void updateFlags(float dt);
    // Pracownik odkłada towary na fladze warsztatu; false = flaga pełna (produkcja wstrzymana).
    bool depositAtWorkshopFlag(Unit& u, Building& b);
    // Dijkstra po sieci dróg od flagi: odległości do wszystkich flag i odcinek, którym do nich doszliśmy.
    void networkDistances(int fromFlag, std::vector<float>& dist, std::vector<int>& prevRoad) const;
    // Trasa po drogach od flagi do flagi (punkty dróg w kolejności marszu); false = brak połączenia.
    bool networkPath(int fromFlag, int toFlag, std::vector<Vec3>& out) const;
    // Tragarze: nabór wolnych osadników do docelowej liczby albo zwalnianie nadmiaru.
    void manageCarriers(int faction, int limit);
    // Przydział zleceń (odbiory i dostawy) wolnym tragarzom.
    void assignTransportJobs(int faction);
    // Opłaca zlecenie towarem z najbliższego magazynu albo leżącym na fladze (false = brak towaru).
    bool fundOrder(TransportOrder& o);
    // Cofa opłatę zlecenia: towar wraca do magazynu / zwalnia rezerwację na fladze.
    void unfundOrder(TransportOrder& o);
    // Automat tragarza.
    void updateCarrier(Unit& u, int id, float dt);
    // Tragarz przerywa zlecenie: zwalnia rezerwacje, niesiony towar zostawia na fladze.
    void releaseCarrierJob(Unit& u);
    // Odkłada niesiony towar na fladze (gdy cel zniknął) - inny tragarz zaniesie go do magazynu.
    void dropCarriedGoods(Unit& u, int flag);
    [[nodiscard]] int otherEnd(const Road& r, int flag) const { return r.flagA == flag ? r.flagB : r.flagA; }
    [[nodiscard]] static bool isCarrierState(UnitState s) {
        return s == UnitState::GoToRoad || s == UnitState::CarrierWait || s == UnitState::CarrierWalk;
    }
    // Modele towarów leżących przy flagach.
    void syncFlagVisuals();
    // Flagi i drogi na terenie przejętym przez wroga są niszczone.
    void removeRoadsOnLostLand();

    // ----------------------------------------------------------------------------------
    // AI przeciwnika                                                          [world_ai.cpp]
    // ----------------------------------------------------------------------------------

    void updateAI(int faction, float dt);
    void aiBuildNext(int faction);
    void aiTrainArmy(int faction);
    // Jaki budynek wyprodukuje brakujący materiał budowlany (Count = żaden / już powstaje).
    // Planista budowy AI (world_ai.cpp): stan gospodarki, wybór producenta towaru, miejsce na budynek.
    struct AiEconomy {
        std::array<int, kBuildingTypeCount> total{}, inProgress{}, warmingUp{}, blocked{}, starved{};
        std::array<int, kBuildingTypeCount> backlog{};   // producenci z zaległymi towarami na fladze (>= 4)
        int underConstruction = 0;
        int onFlags = 0;
        bool peopleShort = false;   // za mało tragarzy / brak jedzenia na nowych osadników
        Resources have;      // magazyny + wolne towary na flagach
        Resources waiting;   // dostawy czekające na towar (brak w magazynach)
    };
    [[nodiscard]] AiEconomy aiEconomy(int faction) const;
    [[nodiscard]] bool aiProducerReady(const AiEconomy& e, BuildingType t) const;
    BuildingType aiPlanFor(int faction, const AiEconomy& e, CarryType good, int depth);
    int aiResourceCluster(int faction, NodeKind kind, BuildingType worker, Vec3& center) const;
    bool aiFindSpot(int faction, BuildingType type, Vec3& spot);
    // Punkt poza terytorium, w stronę którego warto postawić wieżę (las / skały / góry / wolny teren).
    bool aiExpansionTarget(int faction, BuildingType wanted, Vec3& target) const;
    void aiLogDecision(int faction, const std::string& text);
    [[nodiscard]] bool aiNeedsWarehouse(int faction, const AiEconomy& e) const;
    // Łączy drogami warsztaty i magazyny, które nie mają połączenia z magazynem.
    void aiConnectRoads(int faction);
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
    // Liczba sadzonek (rosnących drzew) w promieniu - informacja dla gracza przy drwalu/leśniczym.
    int countSaplingsInRange(const Vec3& from, float radius) const;
    // Leśniczy: losowe wolne miejsce na sadzonkę w zasięgu chaty (false = brak miejsca).
    bool findPlantingSpot(const Building& forester, Vec3& out);
    // Rybak: miejsce na brzegu wody; myśliwy: miejsce w okolicy.
    bool findFishingSpot(const Building& fisher, Vec3& out);
    bool findHuntingSpot(const Building& hunter, Vec3& out);
    [[nodiscard]] bool waterNear(const Vec3& p, float radius) const;
public:
    // Ile sadzonek rośnie w okolicy (panel informacji budynku).
    [[nodiscard]] int saplingsNear(const Vec3& p, float radius) const { return countSaplingsInRange(p, radius); }
private:
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
    std::vector<int> roadCells_;                 // komórka nawigacji -> indeks drogi + 1 (0 = brak drogi)
    bool routingDirty_ = true;                   // sieć dróg lub magazyny się zmieniły - przelicz spójność
    float transportTimer_ = 0.0f;                // odliczanie do kolejnego przydziału zleceń
    std::vector<Vec3> minimapColors_;
    int minimapSize_ = 64;
    uint32_t seed_ = 0;
};

} // namespace settlers
