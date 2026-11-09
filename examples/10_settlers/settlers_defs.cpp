#include "settlers_defs.h"

namespace settlers {

std::string costText(const Resources& cost) {
    std::string out;
    auto append = [&](int value, const char* suffix) {
        if (value <= 0) return;
        if (!out.empty()) out += " ";
        out += std::to_string(value) + suffix;
    };
    append(cost.wood, "D");
    append(cost.stone, "K");
    append(cost.food, "Ż");
    return out.empty() ? "darmo" : out;
}

std::string missingText(const Resources& cost, const Resources& have) {
    std::string out;
    auto append = [&](int need, int got, const char* name) {
        if (got >= need) return;
        if (!out.empty()) out += ", ";
        out += std::to_string(need - got) + " " + name;
    };
    append(cost.wood, have.wood, "drewna");
    append(cost.stone, have.stone, "kamienia");
    append(cost.food, have.food, "żywności");
    return out;
}

const BuildingDef& buildingDef(BuildingType type) {
    //                         name          mesh           cost{W,S,F}   time   hp    half  terr   worker pop  workR  atkR   dmg  cd    h
    static const std::array<BuildingDef, kBuildingTypeCount> defs = {{
        {"Zamek",       "b_castle",     {0, 0, 0},    1.0f,  2200, 16.0f, 175.0f, false, 10, 0.0f,   95.0f, 7.0f, 1.6f, 34.0f,
         "Siedziba rodu. Przyjmuje nowych osadników. Jego utrata kończy grę."},
        {"Dom",         "b_house",      {20, 10, 0},  14.0f, 320,  7.0f,  0.0f,   false, 6,  0.0f,   0.0f,  0.0f, 0.0f, 14.0f,
         "Zwiększa limit populacji o 6."},
        {"Drwal",       "b_wood",       {15, 0, 0},   12.0f, 260,  7.5f,  0.0f,   true,  0,  170.0f, 0.0f,  0.0f, 0.0f, 13.0f,
         "Pracownik ścina drzewa w okolicy i przynosi drewno."},
        {"Kamieniarz",  "b_quarry",     {20, 5, 0},   14.0f, 300,  7.5f,  0.0f,   true,  0,  170.0f, 0.0f,  0.0f, 0.0f, 13.0f,
         "Pracownik wydobywa kamień ze skał w okolicy."},
        {"Farma",       "b_farm",       {25, 5, 0},   16.0f, 260,  12.0f, 0.0f,   true,  0,  10.0f,  0.0f,  0.0f, 0.0f, 12.0f,
         "Pracownik uprawia pole i dostarcza żywność."},
        {"Koszary",     "b_barracks",   {40, 30, 0},  22.0f, 650,  11.0f, 0.0f,   false, 0,  0.0f,   0.0f,  0.0f, 0.0f, 16.0f,
         "Szkoli mieczników i łuczników."},
        {"Wieża",       "b_tower",      {25, 40, 0},  20.0f, 750,  5.5f,  125.0f, false, 0,  0.0f,   120.0f,9.0f, 1.3f, 32.0f,
         "Poszerza terytorium i razi wrogów strzałami."},
    }};
    return defs[static_cast<int>(type)];
}

const UnitDef& unitDef(UnitType type) {
    //                       name        hp     spd    dmg    range  cd    aggro  cost{W,S,F}   train  rad   h     ranged
    static const std::array<UnitDef, kUnitTypeCount> defs = {{
        {"Osadnik",  55.0f,  17.0f, 0.0f,  0.0f,  1.0f, 0.0f,  {0, 0, 3},    0.0f,  2.2f, 6.0f, false},
        {"Miecznik", 150.0f, 15.0f, 14.0f, 4.5f,  1.1f, 75.0f, {5, 5, 15},   10.0f, 2.6f, 6.8f, false},
        {"Łucznik",  85.0f,  15.5f, 9.0f,  65.0f, 1.6f, 85.0f, {15, 0, 10},  12.0f, 2.4f, 6.5f, true},
    }};
    return defs[static_cast<int>(type)];
}

Vec3 factionColor(int faction) {
    return faction == kPlayer ? Vec3{0.15f, 0.35f, 0.95f} : Vec3{0.85f, 0.12f, 0.1f};
}

const char* factionName(int faction) {
    return faction == kPlayer ? "Gracz" : "Wróg";
}

} // namespace settlers
