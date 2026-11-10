#pragma once

#include <array>
#include <initializer_list>
#include <string>
#include <utility>

#include "core/math.h"

/**
 * Definicje gry "Osadnicy" - typy budynków, jednostek, surowców i ich statystyki.
 * Wszystkie liczby balansu gry są zebrane tutaj, aby łatwo je stroić.
 */
namespace settlers {

using engine3d::Vec3;

constexpr int kFactionCount = 2;
constexpr int kPlayer = 0;
constexpr int kEnemy = 1;

// --- Świat ---
constexpr int kHeightmapSize = 256;      // rozdzielczość heightmapy
constexpr float kScaleXZ = 4.0f;         // jednostki świata na piksel heightmapy
constexpr float kScaleY = 0.5f;          // wysokość świata na jednostkę heightmapy (0..255)
constexpr float kWorldHalf = kHeightmapSize * kScaleXZ * 0.5f;   // 512
constexpr float kWaterLevel = 26.0f;     // poziom wody (morze wokół wyspy i jeziora)
constexpr float kMountainLevel = 62.0f;  // od tej wysokości teren to góry (tu stawia się kopalnie)
constexpr float kSnowLine = 98.0f;       // powyżej - ośnieżone szczyty, niedostępne dla ludzi i budynków
constexpr float kTreeGrowSeconds = 120.0f; // ile rośnie sadzonka posadzona przez leśniczego
constexpr float kNavCell = 4.0f;         // rozmiar komórki siatki nawigacji

enum class Difficulty { Easy = 0, Normal = 1, Hard = 2 };

// --- Towary ---
// Towar niesiony przez osadnika, leżący na fladze albo w magazynie. Wartości 1..kGoodTypeCount to prawdziwe
// towary; AnyFood to pseudo-towar używany TYLKO w kosztach ("dowolna żywność": chleb, ryby albo mięso).
enum class CarryType : int {
    None = 0,
    Wood,      // kłody (drwal) - surowiec dla tartaku
    Stone,     // kamień (kamieniarz, kopalnia kamienia) - budowa
    Planks,    // deski (tartak) - budowa, naprawy
    Grain,     // zboże (farma) - dla młyna
    Flour,     // mąka (młyn) - dla piekarni
    Bread,     // chleb (piekarnia) - żywność
    Fish,      // ryby (rybak) - żywność
    Meat,      // mięso (myśliwy) - żywność
    Coal,      // węgiel (kopalnia węgla) - huta, kuźnia
    Ore,       // ruda żelaza (kopalnia rudy) - huta
    Iron,      // żelazo (huta) - kuźnia
    Weapons,   // broń (kuźnia) - szkolenie żołnierzy
    AnyFood,   // tylko w kosztach: dowolna żywność
};
constexpr int kGoodTypeCount = 12;                    // prawdziwe towary: 1..12
constexpr int kResourceSlots = kGoodTypeCount + 2;    // indeksy 0..13 (0 = None, 13 = AnyFood)
[[nodiscard]] constexpr bool isFood(CarryType t) {
    return t == CarryType::Bread || t == CarryType::Fish || t == CarryType::Meat;
}
constexpr CarryType kFoods[] = {CarryType::Bread, CarryType::Fish, CarryType::Meat};

// Ilości towarów (zapas magazynu, koszt, dostawy). Indeks = CarryType.
struct Resources {
    std::array<int, kResourceSlots> v{};

    Resources() = default;
    // Resources r{{CarryType::Planks, 20}, {CarryType::Stone, 10}};
    Resources(std::initializer_list<std::pair<CarryType, int>> items) {
        for (const auto& [t, n] : items) v[static_cast<int>(t)] += n;
    }
    // Po n sztuk każdego prawdziwego towaru (testy).
    static Resources filled(int n) {
        Resources r;
        for (int t = 1; t <= kGoodTypeCount; ++t) r.v[t] = n;
        return r;
    }

    int& operator[](CarryType t) { return v[static_cast<int>(t)]; }
    int operator[](CarryType t) const { return v[static_cast<int>(t)]; }
    // Suma żywności (chleb + ryby + mięso).
    [[nodiscard]] int food() const { return (*this)[CarryType::Bread] + (*this)[CarryType::Fish] + (*this)[CarryType::Meat]; }
    // Suma towarów (AnyFood w koszcie liczony jak towar).
    [[nodiscard]] int total() const {
        int t = 0;
        for (int i = 1; i < kResourceSlots; ++i) t += v[i];
        return t;
    }
    // Czy zapas pokrywa koszt (AnyFood pokrywa żywność, która zostaje po konkretnych pozycjach).
    [[nodiscard]] bool canAfford(const Resources& cost) const {
        for (int t = 1; t <= kGoodTypeCount; ++t) {
            if (v[t] < cost.v[t]) return false;
        }
        const int foodLeft = food() - (cost[CarryType::Bread] + cost[CarryType::Fish] + cost[CarryType::Meat]);
        return foodLeft >= cost[CarryType::AnyFood];
    }
    // Odejmuje koszt; AnyFood zdejmowane z żywności, której jest najwięcej.
    void subtract(const Resources& cost) {
        for (int t = 1; t <= kGoodTypeCount; ++t) v[t] -= cost.v[t];
        for (int n = cost[CarryType::AnyFood]; n > 0; --n) {
            CarryType best = CarryType::Bread;
            for (CarryType f : kFoods) {
                if ((*this)[f] > (*this)[best]) best = f;
            }
            (*this)[best] -= 1;
        }
    }
    void add(const Resources& r) {
        for (int t = 1; t <= kGoodTypeCount; ++t) v[t] += r.v[t];
    }
};

std::string costText(const Resources& cost);
// Opis brakujących surowców, np. "5 desek, 3 kamienia" (pusty, gdy niczego nie brakuje).
std::string missingText(const Resources& cost, const Resources& have);
// Nazwa towaru: mianownik ("Deski") i dopełniacz ("desek") - do napisów "brakuje 5 desek".
const char* goodName(CarryType t);
const char* goodGenitive(CarryType t);
// Kolor znacznika towaru (HUD, karta kosztu).
std::array<float, 4> goodColor(CarryType t);

// Dostęp do pola Resources wg rodzaju towaru (krótszy zapis w starszym kodzie).
inline int& resourceRef(Resources& r, CarryType t) {
    return r[t];
}
inline int resourceOf(const Resources& r, CarryType t) {
    return r[t];
}

// --- Drogi i transport ---
constexpr int kFlagCapacity = 8;           // tyle towarów mieści flaga przed warsztatem
constexpr float kFlagMinSpacing = 7.0f;    // minimalna odległość między flagami
constexpr int kGoodsPerTrip = 2;           // pracownik warsztatu przynosi 2 towary na kurs
constexpr int kCarrierLoad = 2;            // tragarz przenosi do 2 towarów tego samego rodzaju naraz
constexpr float kRoadSpeedBonus = 1.5f;    // po ubitej drodze każdy idzie szybciej

// --- Budynki ---
enum class BuildingType {
    Castle = 0, House, Woodcutter, Quarry, Farm, Barracks, Tower, Forester, Mine, Warehouse,
    // Łańcuchy produkcji
    Sawmill, Mill, Bakery, Fisher, Hunter, CoalMine, OreMine, Smelter, Smithy,
    Count
};
constexpr int kBuildingTypeCount = static_cast<int>(BuildingType::Count);

struct BuildingDef {
    const char* name;
    const char* meshBase;       // prefiks nazwy siatki (dopisywany jest numer frakcji)
    Resources cost;
    float buildTime;            // sekundy pracy jednego budowniczego
    float maxHp;
    float halfSize;             // połowa boku kwadratowego obrysu (footprint)
    float territoryRadius;      // promień terytorium (0 = brak)
    bool needsWorker;           // czy wymaga pracownika (osadnika)
    int popBonus;               // zwiększenie limitu populacji
    float workRadius;           // zasięg pracy pracownika
    float attackRange;          // budynki obronne
    float attackDamage;
    float attackCooldown;
    float height;               // wysokość (paski życia, picking)
    const char* description;
};

const BuildingDef& buildingDef(BuildingType type);
// Co robi pracownik warsztatu (World::settlerWorkWait wybiera zachowanie wg rodzaju).
enum class WorkKind {
    None,       // budynek bez pracownika
    Gather,     // drwal / kamieniarz: idzie do drzewa / skały
    Farm,       // rolnik: pracuje na polu przy farmie
    Forester,   // leśniczy: sadzi drzewa
    Fisher,     // rybak: łowi na brzegu wody
    Hunter,     // myśliwy: poluje w okolicy
    Mine,       // górnik: pracuje we wnętrzu góry (zużywa żywność)
    Process,    // przetwórnia: zamienia towary wejściowe na wyjściowy wewnątrz budynku
};

// Produkcja warsztatu: zachowanie pracownika, towar wyjściowy i towary wejściowe.
// Cykl daje kGoodsPerTrip sztuk wyjścia i zużywa inputPerCycle() sztuk każdego wejścia.
struct ProductionDef {
    WorkKind kind = WorkKind::None;
    CarryType output = CarryType::None;
    std::array<CarryType, 2> inputs{CarryType::None, CarryType::None};
    float workTime = 5.0f;    // sekundy pracy na cykl (we wnętrzu / przy surowcu)
};
const ProductionDef& productionDef(BuildingType type);
// Ile sztuk wejścia zużywa jeden cykl (cykl daje kGoodsPerTrip = 2 sztuki wyrobu):
// żywność kopalni - 1 sztuka, tartak - 1 kłoda na 2 deski, pozostałe - 2 sztuki.
[[nodiscard]] inline int inputPerCycle(CarryType input) {
    return (input == CarryType::AnyFood || input == CarryType::Wood) ? 1 : 2;
}
// Zapas wejść, który warsztat zamawia z magazynów (na ok. 2 cykle).
constexpr int kInputBuffer = 4;

// Pojemność magazynu (zamek, magazyn); 0 = budynek nie przechowuje towarów.
int storageCapacity(BuildingType type);
// Warsztaty, których towary trzeba odbierać drogą (drwal, kamieniarz, farma, kopalnia).
bool producesGoods(BuildingType type);

// --- Jednostki ---
enum class UnitType { Settler = 0, Swordsman, Archer, Count };
constexpr int kUnitTypeCount = static_cast<int>(UnitType::Count);

struct UnitDef {
    const char* name;
    float maxHp;
    float speed;
    float damage;
    float range;                // zasięg ataku (od krawędzi celu)
    float cooldown;
    float aggroRange;           // zasięg automatycznego wykrywania wrogów
    Resources cost;
    float trainTime;
    float radius;               // promień kolizji / zaznaczenia
    float height;
    bool ranged;
};

const UnitDef& unitDef(UnitType type);

// --- Kolory frakcji ---
Vec3 factionColor(int faction);
const char* factionName(int faction);

} // namespace settlers
