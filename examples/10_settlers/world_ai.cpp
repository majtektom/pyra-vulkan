/**
 * world_ai.cpp - komputerowy przeciwnik.
 *
 * AI korzysta z tych samych reguł co gracz (koszty, budowa przez osadników, szkolenie).
 * Co 3 sekundy "myśli": stawia kolejny budynek z listy i zleca szkolenie wojska.
 * Co kilka minut zbiera armię i wysyła falę ataku na najbliższy budynek przeciwnika.
 * Za prostotę decyzji AI dostaje niewielki dochód bierny zależny od poziomu trudności.
 */
#include "world_internal.h"

namespace settlers {

namespace {

// Kolejność budowy komputerowego przeciwnika.
const BuildingType kAiBuildOrder[] = {
    BuildingType::Woodcutter, BuildingType::House, BuildingType::Quarry, BuildingType::Woodcutter,
    BuildingType::Farm, BuildingType::Quarry, BuildingType::Barracks, BuildingType::House,
    BuildingType::Woodcutter, BuildingType::Tower, BuildingType::House, BuildingType::Farm,
    BuildingType::Barracks, BuildingType::Quarry, BuildingType::Tower, BuildingType::House,
};
const BuildingType kAiLateCycle[] = {
    BuildingType::House, BuildingType::Woodcutter, BuildingType::Tower, BuildingType::House, BuildingType::Barracks,
    BuildingType::Quarry, BuildingType::Farm,
};

} // namespace

// Jeden krok AI frakcji f: dochód, co 3 s decyzje (budowa, szkolenie), fale ataku.
void World::updateAI(int f, float dt) {
    FactionState& fs = factions[f];
    if (fs.defeated) return;

    aiCollectIncome(f, dt);

    fs.aiThinkTimer -= dt;
    if (fs.aiThinkTimer <= 0.0f) {
        fs.aiThinkTimer = 3.0f;
        aiBuildNext(f);
        aiTrainArmy(f);
    }

    aiLaunchAttackWave(f, dt);
}

// Pozycja zamku frakcji (albo pozycja startowa, gdy zamku już nie ma).
Vec3 World::aiHome(int f) const {
    const FactionState& fs = factions[f];
    return (fs.castle >= 0 && buildings[fs.castle].alive) ? buildings[fs.castle].pos : startPos[f];
}

// Dochód bierny rekompensuje prostotę decyzji AI: +1 każdego surowca co 1/income sekund.
void World::aiCollectIncome(int f, float dt) {
    FactionState& fs = factions[f];
    const float income = difficulty == Difficulty::Easy ? 0.12f : (difficulty == Difficulty::Normal ? 0.3f : 0.6f);
    fs.incomeAccumulator += dt * income;
    while (fs.incomeAccumulator >= 1.0f) {
        fs.incomeAccumulator -= 1.0f;
        fs.res.add({1, 1, 1});
    }
}

// Stawia kolejny budynek z listy kAiBuildOrder (najwyżej 2 budowy naraz).
// Gdy brakuje miejsca dla ludności, najpierw stawia Dom.
void World::aiBuildNext(int f) {
    FactionState& fs = factions[f];

    int underConstruction = 0;
    bool houseInProgress = false;
    for (const auto& b : buildings) {
        if (!b.alive || b.faction != f || b.constructed) continue;
        ++underConstruction;
        if (b.type == BuildingType::House) houseInProgress = true;
    }
    if (underConstruction >= 2) return;

    // Co budować: pilny Dom, kolejna pozycja z listy albo (po jej wyczerpaniu) cykl późnej gry.
    const size_t orderLen = std::size(kAiBuildOrder);
    const bool needHouse = fs.popCap - fs.population <= 2 && !houseInProgress;
    BuildingType next;
    if (needHouse) {
        next = BuildingType::House;
    } else if (fs.buildOrderIndex < orderLen) {
        next = kAiBuildOrder[fs.buildOrderIndex];
    } else {
        next = kAiLateCycle[(fs.buildOrderIndex - orderLen) % std::size(kAiLateCycle)];
    }
    if (!fs.res.canAfford(buildingDef(next).cost)) return;   // czekamy na surowce

    // Gdzie: wieże na obrzeżach terytorium (poszerzają je), reszta bliżej zamku.
    const float maxR = buildingDef(BuildingType::Castle).territoryRadius - 25.0f;
    const bool tower = next == BuildingType::Tower;
    const float minR = tower ? maxR * 0.7f : 30.0f;
    Vec3 spot;
    if (findPlacementNear(next, f, aiHome(f), minR, tower ? maxR + 60.0f : maxR, spot)) {
        placeBuilding(next, spot, f, false, 0.0f);
        // Pilny Dom spoza listy nie przesuwa listy budowy (chyba że lista i tak wskazywała Dom).
        const bool listSaysHouse = fs.buildOrderIndex < orderLen && kAiBuildOrder[fs.buildOrderIndex] == BuildingType::House;
        if (!(next == BuildingType::House && fs.popCap - fs.population <= 2 && !listSaysHouse)) {
            fs.buildOrderIndex++;
        }
        fs.placementFailures = 0;
    } else if (++fs.placementFailures > 4) {
        fs.buildOrderIndex++;   // nie ma gdzie postawić - pomijamy tę pozycję listy
        fs.placementFailures = 0;
    }
}

// Każde koszary szkolą wojsko (kolejka do 2): dwóch mieczników na jednego łucznika.
void World::aiTrainArmy(int f) {
    FactionState& fs = factions[f];
    for (size_t i = 0; i < buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (!b.alive || b.faction != f || b.type != BuildingType::Barracks || !b.constructed) continue;
        if (b.trainQueue.size() >= 2) continue;
        const UnitType type = (fs.soldiersTrained % 3 == 2) ? UnitType::Archer : UnitType::Swordsman;
        queueTraining(static_cast<int>(i), type);
    }
}

// Fala ataku: gdy minie czas i armia jest dość liczna, wszyscy oprócz 2 obrońców
// ruszają na najbliższy budynek przeciwnika. Kolejne fale wymagają większej armii.
void World::aiLaunchAttackWave(int f, float dt) {
    FactionState& fs = factions[f];
    fs.waveTimer -= dt;
    if (fs.waveTimer > 0.0f) return;

    // Wolni żołnierze (nie walczą i nie są w marszu bojowym).
    std::vector<int> army;
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (u.alive && u.faction == f && u.type != UnitType::Settler &&
            (u.state == UnitState::Idle || (u.state == UnitState::Moving && !u.attackMove))) {
            army.push_back(static_cast<int>(i));
        }
    }
    const int minWave = (difficulty == Difficulty::Easy ? 4 : (difficulty == Difficulty::Normal ? 5 : 6)) + fs.waveNumber;
    if (static_cast<int>(army.size()) < minWave + 2) {
        fs.waveTimer = 20.0f;   // za mało wojska - sprawdzimy za 20 s
        return;
    }

    // Dwóch żołnierzy najbliżej zamku zostaje na obronie.
    const Vec3 home = aiHome(f);
    std::sort(army.begin(), army.end(), [&](int a, int b) { return dist2D(units[a].pos, home) < dist2D(units[b].pos, home); });
    army.erase(army.begin(), army.begin() + 2);

    // Cel: budynek przeciwnika najbliższy naszemu zamkowi.
    int target = -1;
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i < buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (!b.alive || b.faction == f) continue;
        const float d = dist2D(b.pos, home);
        if (d < best) {
            best = d;
            target = static_cast<int>(i);
        }
    }
    if (target >= 0) {
        commandAttackBuilding(army, target);
        fs.waveNumber++;
        pushMessage("Armia wroga wyrusza do ataku! (" + std::to_string(army.size()) + " żołnierzy)",
                    {1.0f, 0.35f, 0.3f, 1.0f});
    }
    fs.waveTimer = difficulty == Difficulty::Easy ? 260.0f : (difficulty == Difficulty::Normal ? 180.0f : 120.0f);
}

} // namespace settlers
