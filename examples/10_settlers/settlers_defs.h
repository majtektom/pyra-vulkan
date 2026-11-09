#pragma once

#include <array>
#include <string>

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
constexpr float kWaterLevel = 26.0f;     // poziom wody w jednostkach świata
constexpr float kNavCell = 4.0f;         // rozmiar komórki siatki nawigacji

enum class Difficulty { Easy = 0, Normal = 1, Hard = 2 };

// --- Surowce ---
struct Resources {
    int wood = 0;
    int stone = 0;
    int food = 0;

    [[nodiscard]] bool canAfford(const Resources& cost) const {
        return wood >= cost.wood && stone >= cost.stone && food >= cost.food;
    }
    void subtract(const Resources& cost) {
        wood -= cost.wood;
        stone -= cost.stone;
        food -= cost.food;
    }
    void add(const Resources& r) {
        wood += r.wood;
        stone += r.stone;
        food += r.food;
    }
};

std::string costText(const Resources& cost);
// Opis brakujących surowców, np. "5 drewna, 3 kamienia" (pusty, gdy niczego nie brakuje).
std::string missingText(const Resources& cost, const Resources& have);

enum class CarryType { None = 0, Wood = 1, Stone = 2, Food = 3 };

// --- Budynki ---
enum class BuildingType { Castle = 0, House, Woodcutter, Quarry, Farm, Barracks, Tower, Count };
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
