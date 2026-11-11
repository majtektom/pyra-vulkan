#include "settlers_defs.h"

#include <algorithm>

namespace settlers {

namespace {

// Koszt budowy: deski + kamień (krótki zapis do tabeli budynków).
Resources C(int planks, int stone) {
    return Resources{{CarryType::Planks, planks}, {CarryType::Stone, stone}};
}

struct GoodInfo {
    const char* name;       // mianownik: "Deski"
    const char* genitive;   // dopełniacz: "desek" (brakuje 5 desek)
    const char* shortName;  // skrót do kosztów na przyciskach
    std::array<float, 4> color;
};

const GoodInfo& goodInfo(CarryType t) {
    static const GoodInfo infos[kResourceSlots] = {
        {"", "", "", {1, 1, 1, 1}},
        {"Drewno", "drewna", "Dr", {0.62f, 0.42f, 0.22f, 1.0f}},
        {"Kamień", "kamienia", "K", {0.72f, 0.72f, 0.75f, 1.0f}},
        {"Deski", "desek", "De", {0.92f, 0.72f, 0.42f, 1.0f}},
        {"Zboże", "zboża", "Zb", {0.93f, 0.82f, 0.38f, 1.0f}},
        {"Mąka", "mąki", "Mą", {0.96f, 0.95f, 0.9f, 1.0f}},
        {"Chleb", "chleba", "Ch", {0.8f, 0.55f, 0.25f, 1.0f}},
        {"Ryby", "ryb", "Ry", {0.5f, 0.75f, 0.9f, 1.0f}},
        {"Mięso", "mięsa", "Mi", {0.85f, 0.35f, 0.35f, 1.0f}},
        {"Węgiel", "węgla", "Wę", {0.2f, 0.2f, 0.22f, 1.0f}},
        {"Ruda", "rudy", "Ru", {0.65f, 0.4f, 0.3f, 1.0f}},
        {"Żelazo", "żelaza", "Że", {0.6f, 0.65f, 0.72f, 1.0f}},
        {"Broń", "broni", "Br", {0.85f, 0.88f, 0.95f, 1.0f}},
        {"Żywność", "żywności", "Ży", {0.95f, 0.85f, 0.35f, 1.0f}},
    };
    return infos[std::clamp(static_cast<int>(t), 0, kResourceSlots - 1)];
}

} // namespace

const char* goodName(CarryType t) {
    return goodInfo(t).name;
}

const char* goodGenitive(CarryType t) {
    return goodInfo(t).genitive;
}

std::array<float, 4> goodColor(CarryType t) {
    return goodInfo(t).color;
}

std::string costText(const Resources& cost) {
    std::string out;
    for (int t = 1; t < kResourceSlots; ++t) {
        if (cost.v[t] <= 0) continue;
        if (!out.empty()) out += " ";
        out += std::to_string(cost.v[t]) + goodInfo(static_cast<CarryType>(t)).shortName;
    }
    return out.empty() ? "darmo" : out;
}

std::string missingText(const Resources& cost, const Resources& have) {
    std::string out;
    auto append = [&](int missing, CarryType t) {
        if (missing <= 0) return;
        if (!out.empty()) out += ", ";
        out += std::to_string(missing) + " " + goodGenitive(t);
    };
    for (int t = 1; t <= kGoodTypeCount; ++t) append(cost.v[t] - have.v[t], static_cast<CarryType>(t));
    // Dowolna żywność: brakuje tyle, ile nie pokrywa żywność pozostała po konkretnych pozycjach.
    const int foodLeft = have.food() - (cost[CarryType::Bread] + cost[CarryType::Fish] + cost[CarryType::Meat]);
    append(cost[CarryType::AnyFood] - std::max(0, foodLeft), CarryType::AnyFood);
    return out;
}

const BuildingDef& buildingDef(BuildingType type) {
    //                         name               mesh            cost       time   hp    half  terr   worker pop  workR  atkR   dmg  cd    h
    static const std::array<BuildingDef, kBuildingTypeCount> defs = {{
        {"Zamek",            "b_castle",     C(0, 0),   1.0f,  2200, 16.0f, 175.0f, false, 14, 0.0f,   95.0f, 7.0f, 1.6f, 34.0f,
         "Siedziba rodu. Przyjmuje nowych osadników. Jego utrata kończy grę."},
        {"Dom",              "b_house",      C(20, 10), 14.0f, 320,  7.0f,  0.0f,   false, 6,  0.0f,   0.0f,  0.0f, 0.0f, 14.0f,
         "Zwiększa limit populacji o 6 (ludność najwyżej 250)."},
        {"Drwal",            "b_wood",       C(12, 0),  12.0f, 260,  7.5f,  0.0f,   true,  0,  170.0f, 0.0f,  0.0f, 0.0f, 13.0f,
         "Ścina drzewa i przynosi kłody. 1 tartak przerabia kłody 4 drwali."},
        {"Kamieniarz",       "b_quarry",     C(15, 5),  14.0f, 300,  7.5f,  0.0f,   true,  0,  170.0f, 0.0f,  0.0f, 0.0f, 13.0f,
         "Wydobywa kamień ze skał w okolicy."},
        {"Farma",            "b_farm",       C(20, 5),  16.0f, 260,  12.0f, 0.0f,   true,  0,  10.0f,  0.0f,  0.0f, 0.0f, 12.0f,
         "Uprawia zboże. 1 młyn miele zboże 3 farm."},
        {"Koszary",          "b_barracks",   C(35, 30), 22.0f, 650,  11.0f, 0.0f,   false, 0,  0.0f,   0.0f,  0.0f, 0.0f, 16.0f,
         "Szkoli żołnierzy - każdy potrzebuje broni z kuźni."},
        {"Wieża",            "b_tower",      C(20, 40), 20.0f, 750,  5.5f,  125.0f, false, 0,  0.0f,   120.0f,9.0f, 1.3f, 32.0f,
         "Poszerza terytorium i razi wrogów strzałami."},
        {"Leśniczy",         "b_forester",   C(15, 5),  12.0f, 240,  7.0f,  0.0f,   true,  0,  60.0f,  0.0f,  0.0f, 0.0f, 13.0f,
         "Sadzi drzewa - 1 leśniczy wystarcza na 3 drwali. Stawiaj obok nich."},
        {"Kopalnia kamienia", "b_mine",      C(25, 5),  18.0f, 420,  8.0f,  0.0f,   true,  0,  0.0f,   0.0f,  0.0f, 0.0f, 14.0f,
         "Kamień z wnętrza góry. Tylko w górach; górnik zjada żywność."},
        {"Magazyn",          "b_store",      C(30, 20), 16.0f, 500,  8.5f,  0.0f,   false, 0,  0.0f,   0.0f,  0.0f, 0.0f, 13.0f,
         "Przechowuje do 200 towarów. Stawiaj blisko produkcji - krótsze drogi."},
        {"Tartak",           "b_sawmill",    C(15, 5),  14.0f, 300,  8.0f,  0.0f,   true,  0,  0.0f,   0.0f,  0.0f, 0.0f, 13.0f,
         "Tnie kłody na deski (2 kłody = 4 deski). Obsłuży 4 drwali."},
        {"Młyn",             "b_mill",       C(20, 10), 16.0f, 320,  7.0f,  0.0f,   true,  0,  0.0f,   0.0f,  0.0f, 0.0f, 20.0f,
         "Miele zboże na mąkę. Obsłuży 3 farmy, mąka wystarczy 1 piekarni."},
        {"Piekarnia",        "b_bakery",     C(20, 15), 16.0f, 320,  7.0f,  0.0f,   true,  0,  0.0f,   0.0f,  0.0f, 0.0f, 13.0f,
         "Piecze chleb z mąki."},
        {"Rybak",            "b_fisher",     C(10, 0),  10.0f, 220,  6.5f,  0.0f,   true,  0,  70.0f,  0.0f,  0.0f, 0.0f, 11.0f,
         "Łowi ryby w pobliskiej wodzie. Stawiaj nad brzegiem."},
        {"Myśliwy",          "b_hunter",     C(10, 0),  10.0f, 220,  6.5f,  0.0f,   true,  0,  90.0f,  0.0f,  0.0f, 0.0f, 11.0f,
         "Poluje w okolicy i przynosi mięso."},
        {"Kopalnia węgla",   "b_coalmine",   C(25, 5),  18.0f, 420,  8.0f,  0.0f,   true,  0,  0.0f,   0.0f,  0.0f, 0.0f, 14.0f,
         "Węgiel z wnętrza góry. Tylko w górach; górnik zjada żywność."},
        {"Kopalnia rudy",    "b_oremine",    C(25, 5),  18.0f, 420,  8.0f,  0.0f,   true,  0,  0.0f,   0.0f,  0.0f, 0.0f, 14.0f,
         "Ruda żelaza z wnętrza góry. Tylko w górach; górnik zjada żywność."},
        {"Huta",             "b_smelter",    C(20, 30), 20.0f, 500,  8.0f,  0.0f,   true,  0,  0.0f,   0.0f,  0.0f, 0.0f, 16.0f,
         "Wytapia żelazo z rudy i węgla."},
        {"Kuźnia",           "b_smithy",     C(25, 25), 20.0f, 480,  8.0f,  0.0f,   true,  0,  0.0f,   0.0f,  0.0f, 0.0f, 14.0f,
         "Kuje broń z żelaza i węgla - bez broni koszary nie szkolą."},
    }};
    return defs[static_cast<int>(type)];
}

const ProductionDef& productionDef(BuildingType type) {
    using C = CarryType;
    using K = WorkKind;
    // Czasy dobrane pomiarem (autoplay: "wydajność") tak, by zgadzały się proporcje AI (ai_planner.cpp, kAiRatios):
    // tartak = 4 drwali, młyn = 3 farmy, piekarnia = młyn, huta = kopalnia rudy, kuźnia = huta.
    //                      zachowanie     wyjście      wejścia                     czas  [ilość wyrobu, domyślnie 2]
    static const std::array<ProductionDef, kBuildingTypeCount> defs = {{
        {K::None,      C::None,    {C::None, C::None},     0.0f},   // Zamek
        {K::None,      C::None,    {C::None, C::None},     0.0f},   // Dom
        {K::Gather,    C::Wood,    {C::None, C::None},    20.0f},   // Drwal (wolny: tartak przerabia kłody 4 drwali)
        {K::Gather,    C::Stone,   {C::None, C::None},     5.0f},   // Kamieniarz
        {K::Farm,      C::Grain,   {C::None, C::None},    30.0f},   // Farma (wolna: młyn miele zboże 3 farm)
        {K::None,      C::None,    {C::None, C::None},     0.0f},   // Koszary
        {K::None,      C::None,    {C::None, C::None},     0.0f},   // Wieża
        {K::Forester,  C::None,    {C::None, C::None},     4.0f},   // Leśniczy
        {K::Mine,      C::Stone,   {C::AnyFood, C::None},  7.0f},   // Kopalnia kamienia
        {K::None,      C::None,    {C::None, C::None},     0.0f},   // Magazyn
        {K::Process,   C::Planks,  {C::Wood, C::None},     2.0f, 4},   // Tartak: 2 kłody -> 4 deski (= 4 drwali)
        {K::Process,   C::Flour,   {C::Grain, C::None},    6.0f},   // Młyn
        {K::Process,   C::Bread,   {C::Flour, C::None},    6.0f},   // Piekarnia
        {K::Fisher,    C::Fish,    {C::None, C::None},     6.0f},   // Rybak
        {K::Hunter,    C::Meat,    {C::None, C::None},     7.0f},   // Myśliwy
        {K::Mine,      C::Coal,    {C::AnyFood, C::None},  7.0f},   // Kopalnia węgla
        {K::Mine,      C::Ore,     {C::AnyFood, C::None},  7.0f},   // Kopalnia rudy
        {K::Process,   C::Iron,    {C::Ore, C::Coal},      7.0f},   // Huta
        {K::Process,   C::Weapons, {C::Iron, C::Coal},     8.0f},   // Kuźnia
    }};
    return defs[static_cast<int>(type)];
}

int storageCapacity(BuildingType type) {
    if (type == BuildingType::Castle) return 200;
    if (type == BuildingType::Warehouse) return 200;
    return 0;
}

// Warsztat, którego towary trzeba odbierać drogą (ma towar wyjściowy).
bool producesGoods(BuildingType type) {
    return productionDef(type).output != CarryType::None;
}

const UnitDef& unitDef(UnitType type) {
    using C = CarryType;
    //                       name        hp     spd    dmg    range  cd    aggro  cost                                       train  rad   h     ranged
    static const std::array<UnitDef, kUnitTypeCount> defs = {{
        {"Osadnik",  55.0f,  17.0f, 0.0f,  0.0f,  1.0f, 0.0f,  Resources{{C::AnyFood, 3}},                   0.0f,  2.2f, 6.0f, false},
        {"Miecznik", 150.0f, 15.0f, 14.0f, 4.5f,  1.1f, 75.0f, Resources{{C::Weapons, 1}, {C::AnyFood, 2}},   10.0f, 2.6f, 6.8f, false},
        {"Łucznik",  85.0f,  15.5f, 9.0f,  65.0f, 1.6f, 85.0f, Resources{{C::Weapons, 1}, {C::Planks, 2}},    12.0f, 2.4f, 6.5f, true},
    }};
    return defs[static_cast<int>(type)];
}

Vec3 factionColor(int faction) {
    return faction == kPlayer ? Vec3{0.15f, 0.35f, 0.95f} : Vec3{0.85f, 0.12f, 0.1f};
}

const char* factionName(int faction) {
    return faction == kPlayer ? "Gracz" : "Wróg";
}

// Surowce, półprodukty i żywność mają limit (zapychały magazyny: żywność zużywa się powoli),
// deski i broń - bez limitu.
int defaultStockLimit(CarryType t) {
    switch (t) {
    case CarryType::Bread:
    case CarryType::Fish:
    case CarryType::Meat: return 40;
    case CarryType::Wood: return 40;
    case CarryType::Stone: return 60;
    case CarryType::Grain:
    case CarryType::Flour:
    case CarryType::Coal:
    case CarryType::Ore:
    case CarryType::Iron: return 20;
    default: return kNoStockLimit;
    }
}

StockLimits defaultStockLimits() {
    StockLimits l{};
    for (int t = 0; t < kResourceSlots; ++t) l[t] = defaultStockLimit(static_cast<CarryType>(t));
    return l;
}

} // namespace settlers
