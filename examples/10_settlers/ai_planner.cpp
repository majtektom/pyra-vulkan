/**
 * ai_planner.cpp - komputerowy przeciwnik (klasa AiPlanner, ai_planner.h).
 *
 * AI gra na tych samych zasadach co gracz (koszty, budowa przez osadników, tragarze, szkolenie) i bez dochodu
 * biernego - poziom trudności to zapasy startowe i tempo fal.
 *
 * Budowa NIE idzie według sztywnej listy. Co 3 sekundy planista (buildNext) czyta stan gospodarki - te same
 * statystyki i ostrzeżenia, które widzi gracz - i stawia to, czego brakuje:
 *   1. sprzątanie: kamieniarz bez skał i drwal bez drzew (i sadzonek) są burzeni - i tak nic nie wydobędą,
 *   2. pilne: Dom przy limicie ludności, Magazyn przy zapełnionych magazynach / zakorkowanych flagach,
 *   3. proporcje warsztatów (tabela kAiRatios): np. 1 tartak obsługuje 4 drwali, 1 leśniczy wystarcza na 3 drwali -
 *      brakujący przetwórca jest stawiany od razu, a nowy dostawca dopiero, gdy przetwórca ma wolne moce,
 *   4. braki towarów: dostawy czekające na towar ("dostawy czekają na") + zapas poniżej celu (deski, kamień,
 *      żywność, broń). Dla brakującego towaru planFor wybiera producenta, schodząc po łańcuchu
 *      (broń -> kuźnia -> żelazo -> huta -> ruda/węgiel -> kopalnie),
 *   5. rozwój: koszary, wieże (gdy brakuje miejsca albo surowców na terytorium).
 * Producenta danego towaru nie dobudowuje się, gdy inny taki sam jest w budowie, jeszcze nic nie wyprodukował
 * albo stoi z pełną flagą (towar jest, tylko nie ma kto go odebrać) - to chroni przed nadprodukcją budynków.
 *
 * Co kilka minut AI zbiera armię i wysyła falę ataku na najbliższy budynek przeciwnika.
 * Drogi AI buduje automatycznie: każdy budynek łączy z najbliższą flagą sieci (autoConnectFlag).
 */
#include "ai_planner.h"

#include "world_internal.h"

namespace settlers {

namespace {

// Proporcje warsztatów: jeden "processor" obsługuje "perProcessor" dostawców "supplier".
struct AiRatio {
    BuildingType processor;
    BuildingType supplier;
    int perProcessor;
};
const AiRatio kAiRatios[] = {
    {BuildingType::Forester, BuildingType::Woodcutter, 3},   // leśniczy odsadza las 3 drwalom
    {BuildingType::Sawmill, BuildingType::Woodcutter, 4},    // tartak przerabia kłody 4 drwali
    {BuildingType::Mill, BuildingType::Farm, 3},             // młyn miele zboże 3 farm
    {BuildingType::Bakery, BuildingType::Mill, 1},           // piekarnia piecze mąkę 1 młyna
    {BuildingType::Smelter, BuildingType::OreMine, 1},       // huta przetapia rudę 1 kopalni
    {BuildingType::Smithy, BuildingType::Smelter, 1},        // kuźnia kuje żelazo 1 huty
};

// Minimalna działająca gospodarka: AI uzupełnia brakujące pozycje (w tej kolejności), gdy nie ma pilniejszych potrzeb.
// To nie jest lista budowy - pozycja jest pomijana, gdy budynek już stoi; liczby rosną dalej z potrzeb i proporcji.
struct AiMinimum {
    BuildingType type;
    int count;
};
const AiMinimum kAiMinimum[] = {
    {BuildingType::Woodcutter, 1}, {BuildingType::Sawmill, 1}, {BuildingType::Hunter, 1}, {BuildingType::Quarry, 1},
    {BuildingType::Fisher, 1},     {BuildingType::Woodcutter, 2},     {BuildingType::House, 2},   {BuildingType::Barracks, 1}, {BuildingType::CoalMine, 1},
    {BuildingType::OreMine, 1},    {BuildingType::Smelter, 1}, {BuildingType::Smithy, 1},   {BuildingType::Farm, 2},
    {BuildingType::Tower, 1},
};

// Zapas, który AI chce mieć w magazynach (poniżej - brak, który trzeba uzupełnić produkcją).
constexpr int kAiStockPlanks = 25, kAiStockStone = 20, kAiStockFood = 15, kAiStockWeapons = 6;

const char* bname(BuildingType t) {
    return buildingDef(t).name;
}

} // namespace

// Jeden krok AI frakcji f: co 3 s decyzje (budowa, szkolenie), fale ataku.
void AiPlanner::update(int f, float dt) {
    FactionState& fs = w_.factions[f];
    if (fs.defeated) return;

    // Limity zapasu AI (jak gracz w oknie Gospodarka): deski i broń bez limitu zapełniałyby kolejne magazyny.
    if (fs.stockLimit[static_cast<int>(CarryType::Planks)] >= kNoStockLimit) fs.stockLimit[static_cast<int>(CarryType::Planks)] = 150;
    if (fs.stockLimit[static_cast<int>(CarryType::Weapons)] >= kNoStockLimit) fs.stockLimit[static_cast<int>(CarryType::Weapons)] = 60;

    fs.aiThinkTimer -= dt;
    if (fs.aiThinkTimer <= 0.0f) {
        fs.aiThinkTimer = 3.0f;
        eco_[f] = economyOf(f);   // jeden przegląd świata na krok myślenia - dalsze zapytania korzystają z pamięci
        connectRoads(f);
        buildNext(f);
        trainArmy(f);
    }

    scout(f, dt);
    launchAttackWave(f, dt);
}

// Pozycja zamku frakcji (albo pozycja startowa, gdy zamku już nie ma).
Vec3 AiPlanner::homeOf(int f) const {
    const FactionState& fs = w_.factions[f];
    return (fs.castle >= 0 && w_.buildings[fs.castle].alive) ? w_.buildings[fs.castle].pos : w_.startPos[f];
}

// Stan gospodarki frakcji widziany przez AI.
bool AiPlanner::knows(int f, const Vec3& p) const {
    return !w_.aiFogOfWar || w_.exploredBy(f, p) || w_.territoryOwnerAt(p) == f;
}

AiPlanner::Economy AiPlanner::economyOf(int f) const {
    Economy e;
    for (size_t i = 0; i < w_.buildings.size(); ++i) {
        const Building& b = w_.buildings[i];
        if (!b.alive) continue;
        if (b.faction != f) {
            if (knows(f, b.pos)) e.knownEnemyBuildings.push_back(static_cast<int>(i));
            continue;
        }
        const size_t t = static_cast<size_t>(b.type);
        e.total[t]++;
        e.own[t].push_back(static_cast<int>(i));
        ++e.buildingCount;
        if (b.constructed && b.outputBlocked && w_.flagBlockReason(b.flag) == World::FlagBlock::StorageFull) ++e.storageBlocked;
        if (!b.constructed) {
            e.inProgress[t]++;
            ++e.underConstruction;
            continue;
        }
        // "Rozgrzewa się": jeszcze nic nie wyprodukował (świeżo postawiony) - nie oceniamy go jeszcze.
        if (producesGoods(b.type) && b.produced == 0) e.warmingUp[t]++;
        if (b.outputBlocked) e.blocked[t]++;
        if (producesGoods(b.type) && b.flag >= 0 && w_.flagAvailableGoods(w_.flags[b.flag]) >= 4) e.backlog[t]++;
        if (b.inputStarved) e.starved[t]++;
    }
    for (const auto& u : w_.units) e.soldiers += (u.alive && u.faction == f && u.type != UnitType::Settler) ? 1 : 0;
    e.have = w_.spendable(f);
    for (const auto& o : w_.orders) {   // dostawy bez pokrycia w towarze = to, czego brakuje odbiorcom
        if (o.alive && o.faction == f && !o.funded) e.waiting[o.type] += o.amount;
    }
    for (const auto& fl : w_.flags) {
        if (fl.alive && fl.faction == f) e.onFlags += static_cast<int>(fl.goods.size());
    }
    // Ludzie: tragarzy mniej niż połowa potrzebnych albo brak jedzenia na nowych osadników (koszt: 3 żywności).
    const TransportStats ts = w_.transportStats(f);
    e.peopleShort = ts.carriers * 2 < ts.target || e.have.food() < 6;
    return e;
}

// Czy można dobudować kolejnego producenta typu t (żaden się nie buduje, każdy już coś dał, żaden nie stoi z pełną flagą).
bool AiPlanner::producerReady(const Economy& e, BuildingType t) const {
    const size_t i = static_cast<size_t>(t);
    // Zaległe towary na flagach = brakuje transportu (tragarzy), a nie producentów.
    return e.inProgress[i] == 0 && e.warmingUp[i] == 0 && e.blocked[i] == 0 && e.backlog[i] == 0;
}

// Kto wyprodukuje towar g. Zwraca budynek do postawienia albo Count (nic / jeszcze nie teraz).
// Schodzi po łańcuchu: gdy przetwórcy brakuje surowca, którego nikt nie wytwarza, najpierw jego producent.
BuildingType AiPlanner::planFor(int f, const Economy& e, CarryType g, int depth) {
    if (depth > 4) return BuildingType::Count;
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };
    auto ready = [&](BuildingType t) { return producerReady(e, t); };
    // Łańcuch przetwórca <- dostawca (proporcje z kAiRatios): przetwórca dopiero, gdy obecni mają pełne
    // obłożenie dostawcami; inaczej kolejny dostawca (np. 5. drwal dopiero przy 2. tartaku).
    auto chain = [&](BuildingType processor, CarryType supplierGood) {
        BuildingType supplier = BuildingType::Count;
        int per = 1;
        for (const AiRatio& r : kAiRatios) {
            if (r.processor == processor) {
                supplier = r.supplier;
                per = r.perProcessor;
            }
        }
        const int sup = n(supplier), proc = n(processor);
        if (sup == 0) return planFor(f, e, supplierGood, depth + 1);
        if (proc == 0 || sup >= proc * per) return ready(processor) ? processor : BuildingType::Count;
        return planFor(f, e, supplierGood, depth + 1);
    };
    switch (g) {
    case CarryType::Wood: return ready(BuildingType::Woodcutter) ? BuildingType::Woodcutter : BuildingType::Count;
    case CarryType::Planks: return chain(BuildingType::Sawmill, CarryType::Wood);
    case CarryType::Stone: {
        // Kamieniarz, gdy na terytorium są skały poza zasięgiem obecnych kamieniarzy; inaczej kopalnia w górach.
        if (!ready(BuildingType::Quarry) || !ready(BuildingType::Mine)) return BuildingType::Count;
        Vec3 c;
        if (resourceCluster(f, NodeKind::Rock, BuildingType::Quarry, c) >= 3) return BuildingType::Quarry;
        // Kopalnia je żywność - bez działającego rybaka/myśliwego najpierw jedzenie.
        if (n(BuildingType::Fisher) + n(BuildingType::Hunter) - e.inProgress[static_cast<size_t>(BuildingType::Fisher)] -
                e.inProgress[static_cast<size_t>(BuildingType::Hunter)] == 0) {
            return planFor(f, e, CarryType::AnyFood, depth + 1);
        }
        return BuildingType::Mine;
    }
    case CarryType::AnyFood:
    case CarryType::Fish:
    case CarryType::Meat: {
        // Najpierw rybacy i myśliwi (tani, bez wejść), potem łańcuch chleba (farmy -> młyn -> piekarnia).
        if (n(BuildingType::Fisher) + n(BuildingType::Hunter) < 3) {
            if (!ready(BuildingType::Fisher) || !ready(BuildingType::Hunter)) return BuildingType::Count;
            return n(BuildingType::Fisher) <= n(BuildingType::Hunter) ? BuildingType::Fisher : BuildingType::Hunter;
        }
        return planFor(f, e, CarryType::Bread, depth + 1);
    }
    case CarryType::Grain: return ready(BuildingType::Farm) ? BuildingType::Farm : BuildingType::Count;
    case CarryType::Flour: return chain(BuildingType::Mill, CarryType::Grain);
    case CarryType::Bread: return chain(BuildingType::Bakery, CarryType::Flour);
    case CarryType::Coal: return ready(BuildingType::CoalMine) ? BuildingType::CoalMine : BuildingType::Count;
    case CarryType::Ore: return ready(BuildingType::OreMine) ? BuildingType::OreMine : BuildingType::Count;
    case CarryType::Iron:
        if (n(BuildingType::CoalMine) == 0) return planFor(f, e, CarryType::Coal, depth + 1);   // huta potrzebuje też węgla
        return chain(BuildingType::Smelter, CarryType::Ore);
    case CarryType::Weapons: return chain(BuildingType::Smithy, CarryType::Iron);
    default: return BuildingType::Count;
    }
}

// Najlepsze skupisko drzew/skał na terytorium frakcji, którego nie obsługuje jeszcze żaden budynek typu "worker".
// Zwraca liczbę surowców w skupisku (promień 60) i jego środek.
int AiPlanner::resourceCluster(int f, NodeKind kind, BuildingType worker, Vec3& center) const {
    const float coveredR = buildingDef(worker).workRadius * 0.5f;
    int best = 0;
    int sampled = 0;
    const auto& workers = eco_[f].own[static_cast<size_t>(worker)];
    for (size_t i = 0; i < w_.nodes.size() && sampled < 80; i += 3) {   // co trzeci węzeł - wystarczy do oceny
        const ResourceNode& nd = w_.nodes[i];
        if (!nd.alive || nd.kind != kind || w_.territoryOwnerAt(nd.pos) != f || !knows(f, nd.pos)) continue;
        bool covered = false;
        for (int bi : workers) covered |= distanceXZ(w_.buildings[bi].pos, nd.pos) < coveredR;
        if (covered) continue;
        ++sampled;
        const int c = w_.countNodesInRange(kind, nd.pos, 60.0f);
        if (c > best) {
            best = c;
            center = nd.pos;
        }
    }
    return best;
}

// Cel rozbudowy terytorium: najbliższy (od zamku) punkt poza naszym i wrogim terenem, w którym jest to, czego
// zabrakło: drzewa (drwal, leśniczy), skały (kamieniarz), góry (kopalnie). Inaczej - wolny teren najdalej od wroga.
bool AiPlanner::expansionTarget(int f, BuildingType wanted, Vec3& target, bool* blind) const {
    if (blind) *blind = false;
    const Vec3 home = homeOf(f);
    // Ziemia niczyja - AI ocenia tylko teren, który zna (mgła wojny AI).
    auto freeLand = [&](const Vec3& p) { return knows(f, p) && w_.territoryOwnerAt(p) < 0; };
    float best = 1e9f;
    bool found = false;
    // Tylko cele osiągalne lądem - las za morzem dawał rząd wież wzdłuż brzegu (każda "w stronę" celu).
    auto consider = [&](const Vec3& p) {
        const float d = distanceXZ(p, home);
        if (d < best && d < 700.0f && freeLand(p) && w_.landReachable(home, p)) {
            best = d;
            target = p;
            found = true;
        }
    };
    const bool trees = wanted == BuildingType::Woodcutter || wanted == BuildingType::Forester || wanted == BuildingType::Hunter;
    const bool rocks = wanted == BuildingType::Quarry;
    const bool mountains = wanted != BuildingType::Count && productionDef(wanted).kind == WorkKind::Mine;
    if (trees || rocks) {
        for (const auto& nd : w_.nodes) {
            if (nd.alive && nd.kind == (trees ? NodeKind::Tree : NodeKind::Rock)) consider(nd.pos);
        }
    } else if (mountains) {
        for (float r = 100.0f; r < 700.0f; r += 25.0f) {
            for (int k = 0; k < 36; ++k) {
                const float a = static_cast<float>(k) * 2.0f * PI / 36.0f;
                const Vec3 p{home.x + std::cos(a) * r, 0.0f, home.z + std::sin(a) * r};
                const float h = w_.heightAt(p.x, p.z);
                if (h >= kMountainLevel && h < kMountainLevel + 20.0f) consider(p);   // zbocze (szczyty są nieprzechodnie)
            }
        }
    }
    if (found) return true;
    if (blind) *blind = true;
    // Wolny teren: kierunek od ZNANYCH budynków wroga (rozbudowa "do tyłu" jest bezpieczniejsza); gdy wroga jeszcze
    // nie odkryto - w stronę środka mapy. Nieodkryty ląd też jest dobrym celem: wieża go odsłoni.
    Vec3 away = multiply(home, -1.0f);   // do środka mapy
    const auto& enemies = eco_[f].knownEnemyBuildings;
    if (!enemies.empty()) {
        Vec3 c{};
        for (int bi : enemies) c = add(c, w_.buildings[bi].pos);
        away = subtract(home, multiply(c, 1.0f / static_cast<float>(enemies.size())));
    }
    const float len = std::sqrt(away.x * away.x + away.z * away.z);
    const float base = len > 1.0f ? std::atan2(away.z, away.x) : 0.0f;
    auto landToClaim = [&](const Vec3& p) { return !knows(f, p) || w_.territoryOwnerAt(p) < 0; };
    for (int k = 0; k < 12 && !found; ++k) {
        const float a = base + static_cast<float>((k + 1) / 2) * ((k % 2) ? 0.5f : -0.5f);
        for (float r = 150.0f; r < 500.0f && !found; r += 25.0f) {
            const Vec3 p{home.x + std::cos(a) * r, 0.0f, home.z + std::sin(a) * r};
            if (w_.heightAt(p.x, p.z) > kWaterLevel + 2.0f && w_.heightAt(p.x, p.z) < kMountainLevel && landToClaim(p) &&
                w_.landReachable(home, p)) {
                target = p;
                found = true;
            }
        }
    }
    return found;
}

// Miejsce dla budynku: przy surowcach / dostawcach / zamku, zależnie od typu.
bool AiPlanner::findSpot(int f, BuildingType t, Vec3& spot) {
    const Vec3 home = homeOf(f);
    const float maxR = buildingDef(BuildingType::Castle).territoryRadius - 25.0f;
    const Economy& e = eco_[f];
    auto centroid = [&](std::initializer_list<BuildingType> types, Vec3& c) {
        Vec3 sum{};
        int n = 0;
        for (BuildingType ty : types) {
            for (int bi : e.own[static_cast<size_t>(ty)]) {
                sum = add(sum, w_.buildings[bi].pos);
                ++n;
            }
        }
        if (n > 0) c = multiply(sum, 1.0f / static_cast<float>(n));
        return n > 0;
    };
    Vec3 c;
    switch (t) {
    case BuildingType::Woodcutter:
        // Drwal tylko przy lesie (choćby małym) - bez drzew na terytorium AI poszerza teren wieżą w stronę lasu.
        return resourceCluster(f, NodeKind::Tree, t, c) >= 3 && w_.findPlacementNear(t, f, c, 8.0f, 45.0f, spot);
    case BuildingType::Quarry:
        // Kamieniarz tylko przy skałach - gdzie indziej od razu zostałby bez pracy.
        return resourceCluster(f, NodeKind::Rock, t, c) >= 3 && w_.findPlacementNear(t, f, c, 8.0f, 40.0f, spot);
    case BuildingType::Forester: {
        // Przy drwalu, który nie ma leśniczego w pobliżu (leśniczy sadzi w promieniu 60).
        for (int wi : e.own[static_cast<size_t>(BuildingType::Woodcutter)]) {
            const Building& w = w_.buildings[wi];
            bool has = false;
            for (int fi : e.own[static_cast<size_t>(BuildingType::Forester)]) has |= distanceXZ(w_.buildings[fi].pos, w.pos) < 100.0f;
            if (!has && w_.findPlacementNear(t, f, w.pos, 12.0f, 50.0f, spot)) return true;
        }
        break;
    }
    case BuildingType::Sawmill:
        if (centroid({BuildingType::Woodcutter}, c) && w_.findPlacementNear(t, f, c, 10.0f, 60.0f, spot)) return true;
        break;
    case BuildingType::Mill:
    case BuildingType::Bakery:
        if (centroid({BuildingType::Farm, BuildingType::Mill}, c) && w_.findPlacementNear(t, f, c, 10.0f, 60.0f, spot)) return true;
        break;
    case BuildingType::Smelter:
    case BuildingType::Smithy:
        if (centroid({BuildingType::CoalMine, BuildingType::OreMine, BuildingType::Smelter}, c) &&
            w_.findPlacementNear(t, f, c, 15.0f, 70.0f, spot)) {
            return true;
        }
        break;
    case BuildingType::Warehouse: {
        // Magazyn blisko produkcji: w okolicy środka ciężkości warsztatów.
        Vec3 sum{};
        int n = 0;
        for (size_t t = 0; t < e.own.size(); ++t) {
            if (!producesGoods(static_cast<BuildingType>(t))) continue;
            for (int bi : e.own[t]) {
                sum = add(sum, w_.buildings[bi].pos);
                ++n;
            }
        }
        if (n > 0 && w_.findPlacementNear(t, f, multiply(sum, 1.0f / static_cast<float>(n)), 10.0f, 70.0f, spot)) return true;
        break;
    }
    case BuildingType::Tower: {
        if (defenseTower_) {
            // Wieża obronna: przy froncie, mogą stać gęsto (byle nie jedna na drugiej) - teren nie musi przybywać.
            Vec3 front;
            int level = 0;
            if (!defenseFront(f, front, level)) return false;
            const float towerR = buildingDef(BuildingType::Tower).territoryRadius;
            auto spaced = [&](const Vec3& p) {
                for (int bi : e.own[static_cast<size_t>(BuildingType::Tower)]) {
                    if (distanceXZ(w_.buildings[bi].pos, p) < towerR * 0.3f) return false;
                }
                return true;
            };
            return w_.findPlacementNear(t, f, front, 0.0f, 50.0f, spot, spaced);
        }
        // Wieża ma sens tylko, gdy poszerza teren o ląd: min. 35% jej zasięgu to nowa, osiągalna ziemia, i nie stoi
        // przy innej wieży / zamku (wcześniej przy morzu powstawał rząd wież, z których każda dodawała tylko wodę).
        const float towerR = buildingDef(BuildingType::Tower).territoryRadius;
        auto worthIt = [&](const Vec3& p) {
            for (BuildingType ty : {BuildingType::Tower, BuildingType::Castle}) {
                for (int bi : e.own[static_cast<size_t>(ty)]) {
                    if (distanceXZ(w_.buildings[bi].pos, p) < towerR * 0.8f) return false;
                }
            }
            const float g = towerGain(f, p);
            towerBestGain_ = std::max(towerBestGain_, g);
            return g >= 0.35f;
        };
        towerBestGain_ = 0.0f;
        // W stronę tego, czego brakuje: idziemy od celu w stronę zamku aż do pierwszego punktu na własnym terenie.
        Vec3 target;
        if (expansionTarget(f, w_.factions[f].aiExpandFor >= 0 ? static_cast<BuildingType>(w_.factions[f].aiExpandFor) : BuildingType::Count, target)) {
            Vec3 dir = subtract(home, target);
            dir.y = 0.0f;
            const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            if (len > 1.0f) {
                dir = multiply(dir, 1.0f / len);
                for (float d = 0.0f; d < len; d += 15.0f) {
                    const Vec3 p = add(target, multiply(dir, d));
                    if (w_.territoryOwnerAt(p) != f) continue;
                    const Vec3 inside = add(p, multiply(dir, 20.0f));   // kawałek w głąb, żeby budynek zmieścił się na naszym
                    if (w_.findPlacementNear(t, f, inside, 0.0f, 35.0f, spot, worthIt)) return true;
                    break;
                }
            }
        }
        return w_.findPlacementNear(t, f, home, maxR * 0.7f, maxR + 60.0f, spot, worthIt);
    }
    case BuildingType::Barracks:
        if (w_.findPlacementNear(t, f, home, 30.0f, maxR, spot)) return true;
        return w_.findPlacementNear(t, f, home, maxR, maxR + 120.0f, spot);   // duży budynek - szukamy dalej
    default: break;
    }
    // Kopalnie (góry), rybak (woda) i reszta: gdziekolwiek na terytorium - canPlace sprawdza warunki.
    return w_.findPlacementNear(t, f, home, 30.0f, maxR + 60.0f, spot);
}

// Planista budowy (opis kolejności na górze pliku). Najwyżej 2 budowy naraz, jeden nowy budynek na krok.
// Reguły pytamy po kolei od najważniejszej; pierwsza, która coś wybierze, wygrywa (AiChoice::choose).
void AiPlanner::buildNext(int f) {
    FactionState& fs = w_.factions[f];
    const Economy& e = eco_[f];   // policzone w updateAI na początku kroku myślenia
    defenseTower_ = false;

    if (demolishUseless(f, e)) return;

    // Limit ludności: nowi osadnicy (budowniczowie, tragarze) pojawią się dopiero po Domu - Dom może więc powstać
    // ponad limit 2 budów, a innych placów nie zaczynamy (nie byłoby kim ich budować).
    const bool atPopCap = fs.population >= fs.popCap - 1 && fs.popCap < kMaxPopulation;   // przy maksimum domy nic nie dadzą
    const bool houseBuilding = e.inProgress[static_cast<size_t>(BuildingType::House)] > 0;
    if (e.underConstruction >= 2 && !(atPopCap && !houseBuilding)) return;
    if (atPopCap && houseBuilding) return;

    Choice choice;
    chooseUrgent(f, e, choice);         // 2. domy, wieże (poszerzenie, obrona), magazyny
    chooseByRatios(e, choice);          // 3. proporcje warsztatów
    chooseMilitary(e, choice);          // 3b. łańcuch broni i koszary
    chooseByShortages(f, e, choice);    // 4. braki towarów
    chooseBaseAndGrowth(f, e, choice);  // 5. minimum gospodarki, wzrost
    if (!choice.chosen()) return;
    if (choice.next != BuildingType::Tower) defenseTower_ = false;
    if (atPopCap && choice.next != BuildingType::House && e.underConstruction >= 2) return;

    applyMaterialReserve(f, e, choice);
    if (!redirectToMaterialProducer(f, e, choice)) return;   // czekamy na materiał
    placeChosen(f, choice);
}

// 1. Sprzątanie: wyczerpany kamieniarz / drwal bez drzew i sadzonek oraz plac budowy, który od 90 s nie ma drogi
//    do magazynu (materiały nigdy nie dotrą, a blokuje limit 2 budów) - burzymy. Zwraca true, gdy coś zburzyło.
bool AiPlanner::demolishUseless(int f, const Economy& e) {
    for (BuildingType gatherer : {BuildingType::Quarry, BuildingType::Woodcutter}) {
        for (int i : e.own[static_cast<size_t>(gatherer)]) {
            const Building& b = w_.buildings[i];
            if (!b.constructed || b.resourcesInRange != 0) continue;
            const bool exhausted = b.type == BuildingType::Quarry ||
                                   w_.saplingsNear(b.pos, buildingDef(b.type).workRadius) == 0;
            if (!exhausted) continue;
            logDecision(f, std::string("burzy ") + bname(b.type) + " - nie ma już czego wydobywać");
            w_.demolish(i);
            return true;
        }
    }
    for (const auto& list : e.own) {
        for (int i : list) {
            const Building& b = w_.buildings[i];
            if (b.constructed || b.type == BuildingType::Castle) continue;
            if (w_.flagConnected(b.flag) || w_.gameTime - b.placedTime < 90.0f) continue;
            logDecision(f, std::string("burzy plac ") + bname(b.type) + " - od 90 s bez drogi");
            w_.demolish(i);
            return true;
        }
    }
    return false;
}

// 2. Pilne: mieszkańcy, wieża poszerzająca teren, gdy nie było gdzie postawić budynku, wieże obronne, magazyny.
void AiPlanner::chooseUrgent(int f, const Economy& e, Choice& choice) {
    FactionState& fs = w_.factions[f];
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };
    const bool towerInProgress = e.inProgress[static_cast<size_t>(BuildingType::Tower)] > 0;

    if (fs.popCap - fs.population <= 2 && fs.popCap < kMaxPopulation && e.inProgress[static_cast<size_t>(BuildingType::House)] == 0) {
        choice.choose(BuildingType::House, "limit ludności " + std::to_string(fs.population) + "/" + std::to_string(fs.popCap));
    }

    // Brak miejsca / surowców na terytorium (nie udało się postawić potrzebnego budynku) - wieża w tamtą stronę.
    // Mgła wojny AI: gdy AI nie zna żadnego miejsca z tym, czego szuka, stawia najwyżej kilka wież "na ślepo"
    // (odsłaniają teren), a dalej czeka na zwiad - inaczej wieże ciągnęłyby się w nieznane bez końca.
    if (fs.aiPlacementFailures >= 2) {
        Vec3 target;
        bool blind = false;
        const BuildingType wanted = fs.aiExpandFor >= 0 ? static_cast<BuildingType>(fs.aiExpandFor) : BuildingType::Count;
        expansionTarget(f, wanted, target, &blind);
        if (blind && n(BuildingType::Tower) >= 3 + e.buildingCount / 10) {
            logDecision(f, std::string("nie zna miejsca dla: ") + (wanted != BuildingType::Count ? bname(wanted) : "budynku") +
                                 " - czeka na zwiad zamiast kolejnej wieży w nieznane");
            fs.aiPlacementFailures = 0;
        }
    }
    const bool expansionTowerAllowed = w_.gameTime >= fs.aiNoTowerUntil;   // niedawno nie było gdzie - nie próbujemy w kółko
    if (fs.aiPlacementFailures >= 2 && expansionTowerAllowed && !towerInProgress) {
        choice.choose(BuildingType::Tower, std::string("brak miejsca na terytorium dla: ") +
                                               (fs.aiExpandFor >= 0 ? bname(static_cast<BuildingType>(fs.aiExpandFor)) : "budynku") +
                                               " - wieża poszerzy teren w tę stronę");
    }

    if (!towerInProgress) chooseDefenseTower(f, e, choice);

    if (needsWarehouse(f, e)) {
        choice.choose(BuildingType::Warehouse, "magazyny " + std::to_string(w_.storageUsedOf(f)) + "/" +
                                                   std::to_string(w_.storageCapacityOf(f)) + ", na flagach " + std::to_string(e.onFlags));
    }
}

// Obrona: przy zagrożeniu (wróg blisko granicy albo niedawny atak) gęsto wieże na froncie - od strony, z której
// wróg przychodzi. Tylko przy działającej gospodarce (tartak, kilka budynków) i z limitem zależnym od wielkości
// osady i liczby żołnierzy (wieża bez załogi nie strzela).
void AiPlanner::chooseDefenseTower(int f, const Economy& e, Choice& choice) {
    Vec3 front;
    int level = 0;
    if (e.total[static_cast<size_t>(BuildingType::Sawmill)] == 0 || e.buildingCount < 8 || !defenseFront(f, front, level)) return;

    // Wieże frontu = wszystkie w stożku +-35° w stronę zagrożenia (nie tylko przy bieżącym punkcie frontu - ten
    // przesuwa się z każdą wieżą i wieże pełzłyby w stronę wroga bez końca).
    constexpr float kCos35 = 0.82f;
    const Vec3 home = homeOf(f);
    Vec3 toFront = subtract(front, home);
    toFront.y = 0.0f;
    const float frontLen = std::max(1.0f, std::sqrt(toFront.x * toFront.x + toFront.z * toFront.z));
    int frontTowers = 0;
    for (int bi : e.own[static_cast<size_t>(BuildingType::Tower)]) {
        Vec3 v = subtract(w_.buildings[bi].pos, home);
        v.y = 0.0f;
        const float len = std::sqrt(v.x * v.x + v.z * v.z);
        if (len < 1.0f) continue;
        frontTowers += (v.x * toFront.x + v.z * toFront.z) / (len * frontLen) > kCos35 ? 1 : 0;
    }
    const int maxFront = std::min(1 + e.buildingCount / 8, 5) + (level >= 2 ? 1 : 0);
    // Wieża obronna tylko, gdy stać na nią od razu - obrona nie może uruchamiać "budowy producenta materiału".
    const bool affordable = e.have.canAfford(buildingDef(BuildingType::Tower).cost);
    if (frontTowers < maxFront && frontTowers <= e.soldiers && affordable && !choice.chosen()) {
        defenseTower_ = true;   // tylko gdy to ta reguła wybiera budynek (nie wcześniejsza wieża poszerzająca)
        choice.choose(BuildingType::Tower, std::string("obrona frontu (") + (level >= 2 ? "niedawny atak" : "wróg blisko granicy") +
                                               ", wież na froncie " + std::to_string(frontTowers) + "/" + std::to_string(maxFront) + ")");
    }
}

// 3. Proporcje warsztatów: dostawcy bez przetwórcy (np. 5 drwali i 1 tartak) - kAiRatios.
void AiPlanner::chooseByRatios(const Economy& e, Choice& choice) const {
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };
    for (const AiRatio& r : kAiRatios) {
        const int suppliers = n(r.supplier) - e.inProgress[static_cast<size_t>(r.supplier)];
        const int needed = (suppliers + r.perProcessor - 1) / r.perProcessor;
        if (suppliers > 0 && n(r.processor) < needed && e.inProgress[static_cast<size_t>(r.processor)] == 0) {
            choice.choose(r.processor, std::string(bname(r.processor)) + " - 1 na " + std::to_string(r.perProcessor) + " (" +
                                           bname(r.supplier) + ": " + std::to_string(suppliers) + ", " + bname(r.processor) +
                                           ": " + std::to_string(n(r.processor)) + ")");
        }
    }
}

// 3b. Wojsko: gdy podstawa gospodarki stoi, łańcuch broni i koszary mają pierwszeństwo przed bieżącymi brakami
//     desek/kamienia/jedzenia (tych brakuje prawie zawsze - wcześniej wojsko z listy minimum nie powstawało przez
//     pół godziny, a AI nie miało kim atakować).
void AiPlanner::chooseMilitary(const Economy& e, Choice& choice) const {
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };
    const bool baseReady = n(BuildingType::Sawmill) > 0 && n(BuildingType::Quarry) + n(BuildingType::Mine) > 0 &&
                           n(BuildingType::Fisher) + n(BuildingType::Hunter) >= 2 && e.buildingCount >= 8;
    // Brak ludzi zwykle wstrzymuje wojsko (najpierw jedzenie), ale po 15 min gry już nie - inaczej koszary nie powstają nigdy.
    if (!baseReady || (e.peopleShort && w_.gameTime <= 900.0f)) return;
    for (BuildingType t : {BuildingType::Barracks, BuildingType::CoalMine, BuildingType::OreMine, BuildingType::Smelter,
                           BuildingType::Smithy}) {
        if (n(t) > 0) continue;
        choice.choose(t, std::string("wojsko: ") + bname(t) + " (łańcuch broni dla koszar)");
        return;
    }
}

// 4. Braki towarów: czekające dostawy + zapas poniżej celu. Kolejność listy = ważność.
void AiPlanner::chooseByShortages(int f, const Economy& e, Choice& choice) {
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };
    const bool military = n(BuildingType::Barracks) > 0;
    // Kopalnie zjadają jedzenie - więcej kopalń, wyższy cel zapasu żywności.
    const int foodTarget = kAiStockFood + 5 * (n(BuildingType::Mine) + n(BuildingType::CoalMine) + n(BuildingType::OreMine));
    const int foodNeed = e.waiting[CarryType::AnyFood] + e.waiting.food() + std::max(0, foodTarget - e.have.food());

    // Brak ludzi (tragarzy / jedzenia na osadników) - najpierw żywność, potem materiały.
    if (e.peopleShort && foodNeed > 0) {
        choice.choose(planFor(f, e, CarryType::AnyFood, 0), "brak ludzi: żywność " + std::to_string(e.have.food()) +
                                                                  " - nowi osadnicy i tragarze kosztują jedzenie");
    }
    struct Need {
        CarryType good;
        int amount;
    };
    const Need needs[] = {
        {CarryType::Planks, e.waiting[CarryType::Planks] + std::max(0, kAiStockPlanks - e.have[CarryType::Planks])},
        {CarryType::Stone, e.waiting[CarryType::Stone] + std::max(0, kAiStockStone - e.have[CarryType::Stone])},
        {CarryType::AnyFood, foodNeed},
        {CarryType::Wood, e.waiting[CarryType::Wood]},
        {CarryType::Coal, e.waiting[CarryType::Coal]},
        {CarryType::Ore, e.waiting[CarryType::Ore]},
        {CarryType::Iron, e.waiting[CarryType::Iron]},
        {CarryType::Grain, e.waiting[CarryType::Grain]},
        {CarryType::Flour, e.waiting[CarryType::Flour]},
        {CarryType::Weapons, military ? e.waiting[CarryType::Weapons] + std::max(0, kAiStockWeapons - e.have[CarryType::Weapons]) : 0},
    };
    for (const Need& need : needs) {
        if (need.amount <= 0 || choice.chosen()) continue;
        const bool food = need.good == CarryType::AnyFood;
        choice.choose(planFor(f, e, need.good, 0),
                      std::string("brak: ") + goodName(food ? CarryType::Bread : need.good) + " " + std::to_string(need.amount) +
                          " (w zapasie " + std::to_string(food ? e.have.food() : e.have[need.good]) + ")");
    }
}

// 5. Minimalna gospodarka (kAiMinimum), wzrost (domy, gdy materiałów dość) i wieże rozwoju terytorium.
void AiPlanner::chooseBaseAndGrowth(int f, const Economy& e, Choice& choice) {
    const FactionState& fs = w_.factions[f];
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };
    // Kamieniarz liczy się razem z kopalnią kamienia, rybak z myśliwym (bez wody rybaka nie da się postawić).
    for (const AiMinimum& m : kAiMinimum) {
        int have = n(m.type);
        if (m.type == BuildingType::Quarry) have += n(BuildingType::Mine);
        if (m.type == BuildingType::Fisher && n(BuildingType::Hunter) > 1) have += 1;
        if (have >= m.count) continue;
        const BuildingType t = m.type == BuildingType::Quarry ? planFor(f, e, CarryType::Stone, 0)   // kamieniarz albo kopalnia
                                                              : m.type;
        if (t == BuildingType::Count) continue;
        choice.choose(t, std::string("podstawa gospodarki: ") + bname(m.type) + " " + std::to_string(have) + "/" + std::to_string(m.count));
    }
    // Wzrost: materiałów dość, a ludzi zaraz zabraknie - więcej domów = więcej pracowników i tragarzy.
    if (e.have[CarryType::Planks] >= 40 && e.have[CarryType::Stone] >= 25 && fs.population * 10 >= fs.popCap * 7 &&
        fs.popCap < kMaxPopulation && e.inProgress[static_cast<size_t>(BuildingType::House)] == 0) {
        choice.choose(BuildingType::House, "wzrost: materiały są, ludność " + std::to_string(fs.population) + "/" + std::to_string(fs.popCap));
    }
    int buildingsTotal = 0;
    for (int count : e.total) buildingsTotal += count;
    if (e.inProgress[static_cast<size_t>(BuildingType::Tower)] == 0 && w_.gameTime >= fs.aiNoTowerUntil &&
        n(BuildingType::Tower) < buildingsTotal / 10) {
        choice.choose(BuildingType::Tower, "rozwój terytorium (1 wieża na 10 budynków)");
    }
}

// Rezerwa: bez tartaku i źródła kamienia nie da się nic odbudować - dopóki ich nie ma, materiały na nie są
// nietykalne (inaczej AI wydawało startowe deski na domy i zostawało bez desek na tartak).
void AiPlanner::applyMaterialReserve(int f, const Economy& e, Choice& choice) {
    const FactionState& fs = w_.factions[f];
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };
    const BuildingType next = choice.next;
    const bool stoneMaker = next == BuildingType::Quarry || next == BuildingType::Mine;
    const bool plankMaker = next == BuildingType::Sawmill || next == BuildingType::Woodcutter;
    // Kolejno: tartak, źródło kamienia, magazyn - pierwszy brakujący, na który odkładamy materiały.
    BuildingType reserved = BuildingType::Count;
    if (n(BuildingType::Sawmill) == 0 && !plankMaker) {
        reserved = BuildingType::Sawmill;
    } else if (n(BuildingType::Quarry) + n(BuildingType::Mine) == 0 && !stoneMaker) {
        reserved = planFor(f, e, CarryType::Stone, 0);
    } else if (needsWarehouse(f, e) && next != BuildingType::Warehouse) {
        // Magazyny pełne: bez nowego magazynu warsztaty staną (deski nie będą miały gdzie trafić).
        reserved = BuildingType::Warehouse;
    }
    // Wieża po las/skały na start (bez nich nie ma desek ani kamienia) - bez rezerwy.
    const bool bootstrapTower = next == BuildingType::Tower && (fs.aiExpandFor == static_cast<int>(BuildingType::Woodcutter) ||
                                                                fs.aiExpandFor == static_cast<int>(BuildingType::Quarry));
    if (reserved == BuildingType::Count || reserved == next || bootstrapTower) return;

    // Rezerwa działa tylko, gdy wybrany budynek dałoby się opłacić, ale zjadłby materiał odłożony na "reserved".
    const Resources& cost = buildingDef(next).cost;
    const Resources& keep = buildingDef(reserved).cost;
    if (e.have.canAfford(cost) && (e.have[CarryType::Planks] - cost[CarryType::Planks] < keep[CarryType::Planks] ||
                                   e.have[CarryType::Stone] - cost[CarryType::Stone] < keep[CarryType::Stone])) {
        choice.why = std::string("najpierw ") + bname(reserved) + " (rezerwa materiałów; odłożone: " + bname(next) + ")";
        choice.next = reserved;
    }
}

// Brakuje materiału na wybrany budynek - zamiast czekać, produkujemy go (deski / kamień).
// Zwraca false, gdy lepiej poczekać (nic nie budujemy w tym kroku).
bool AiPlanner::redirectToMaterialProducer(int f, const Economy& e, Choice& choice) {
    const Resources& cost = buildingDef(choice.next).cost;
    if (e.have.canAfford(cost)) return true;
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };
    const CarryType lack = e.have[CarryType::Planks] < cost[CarryType::Planks] ? CarryType::Planks : CarryType::Stone;
    const BuildingType producer = planFor(f, e, lack, 0);
    if (producer == BuildingType::Count || producer == choice.next || !e.have.canAfford(buildingDef(producer).cost)) return false;
    // Producentów tego materiału jest już co najmniej dwóch - brak to kwestia czasu/transportu, nie liczby
    // producentów. Bez tego AI stawiało kopalnię za kopalnią "na wieżę brakuje kamienia" (6 kopalń zjadało
    // jedzenie i deski, osada stawała).
    const int producers = lack == CarryType::Stone ? n(BuildingType::Quarry) + n(BuildingType::Mine)
                                                   : n(BuildingType::Sawmill) + n(BuildingType::Woodcutter) / 2;
    if (producers >= 2) return false;
    choice.why = std::string("na ") + bname(choice.next) + " brakuje: " + goodName(lack);
    choice.next = producer;
    return true;
}

// Szuka miejsca dla wybranego budynku (z zamiennikami: rybak -> myśliwy, kamieniarz -> kopalnia), stawia go
// i łączy drogą. Porażki zapamiętuje w FactionState - po dwóch AI stawia wieżę poszerzającą teren.
void AiPlanner::placeChosen(int f, Choice choice) {
    FactionState& fs = w_.factions[f];
    BuildingType next = choice.next;
    std::string& why = choice.why;
    Vec3 spot;
    bool found = findSpot(f, next, spot);
    if (!found && next == BuildingType::Fisher) {   // brak wody - myśliwy
        next = BuildingType::Hunter;
        found = findSpot(f, next, spot);
    }
    if (!found && next == BuildingType::Quarry) {   // brak skał - kopalnia kamienia w górach
        next = BuildingType::Mine;
        found = findSpot(f, next, spot);
    }
    if (!found && next == BuildingType::Tower && defenseTower_) {
        logDecision(f, "nie ma miejsca na wieżę obronną przy froncie (" + why + ")");
        return;
    }
    if (!found && next == BuildingType::Tower) {
        // Nie ma miejsca, w którym wieża dodałaby ląd - przez minutę bez wież poszerzających (bez prób co 3 s,
        // które blokowałyby inne budowy i zalewały log).
        fs.aiPlacementFailures = 0;
        fs.aiNoTowerUntil = w_.gameTime + 60.0f;
        logDecision(f, std::string("żadna wieża nie poszerzy już lądu (najlepsze miejsce: ") +
                             std::to_string(static_cast<int>(towerBestGain_ * 100.0f)) + "% nowego lądu; " + why +
                             ") - rezygnuje z wieży");
        return;
    }
    if (!found) {
        fs.aiPlacementFailures++;
        if (next != BuildingType::Tower) fs.aiExpandFor = static_cast<int>(next);
        logDecision(f, std::string("nie ma gdzie postawić: ") + bname(next) + " (" + why + ")");
        return;
    }
    if (next == BuildingType::Tower) {
        if (!defenseTower_) why += " (nowy ląd " + std::to_string(static_cast<int>(towerGain(f, spot) * 100.0f)) + "% zasięgu)";
        fs.aiPlacementFailures = 0;
        fs.aiExpandFor = -1;
    } else {
        fs.aiPlacementFailures = std::max(0, fs.aiPlacementFailures - 1);
    }
    const int id = w_.placeBuilding(next, spot, f, false, 0.0f);
    if (!w_.autoConnectFlag(w_.buildings[id].flag)) {   // bez drogi tragarze nie dowiozą materiałów
        w_.demolish(id);
        fs.aiPlacementFailures++;
        logDecision(f, std::string("nie da się poprowadzić drogi do: ") + bname(next) + " - rezygnuje");
        return;
    }
    logDecision(f, std::string("buduje ") + bname(next) + " - " + why);
}

// Nowy Magazyn: tylko gdy warsztaty stoją, bo magazyny są pełne (albo zakorkowane flagi przy małej liczbie magazynów),
// i najwyżej 1 magazyn na 8 budynków - inaczej deski bez limitu zapełniałyby kolejne magazyny w nieskończoność.
bool AiPlanner::needsWarehouse(int f, const Economy& e) const {
    if (e.inProgress[static_cast<size_t>(BuildingType::Warehouse)] > 0) return false;
    const int total = e.buildingCount, storeFull = e.storageBlocked;
    const int stores = e.total[static_cast<size_t>(BuildingType::Warehouse)];
    if (stores >= 1 + total / 8) return false;
    return storeFull > 0 || w_.storageUsedOf(f) * 10 >= w_.storageCapacityOf(f) * 9 || (e.onFlags > 30 && stores < 2);
}

void AiPlanner::logDecision(int f, const std::string& text) {
    if (w_.aiLog) LOG_INFO("[ai] t={}s {}: {}", static_cast<int>(w_.gameTime), factionName(f), text);
}

// Budynki bez połączenia z siecią dróg: AI dobudowuje drogę (najwyżej 2 na raz,
// żeby jeden "myślący" krok nie trwał za długo).
void AiPlanner::connectRoads(int f) {
    int tries = 0;
    for (const auto& b : w_.buildings) {
        if (tries >= 2) break;
        if (!b.alive || b.faction != f || b.flag < 0 || b.type == BuildingType::Castle) continue;
        if (!b.constructed && b.type == BuildingType::Warehouse && w_.flagConnected(b.flag)) continue;
        if (w_.buildingLinked(b)) continue;
        ++tries;
        w_.autoConnectFlag(b.flag);
    }
}

// Każde koszary szkolą wojsko (kolejka do 2): dwóch mieczników na jednego łucznika.
void AiPlanner::trainArmy(int f) {
    FactionState& fs = w_.factions[f];
    for (int i : eco_[f].own[static_cast<size_t>(BuildingType::Barracks)]) {
        const Building& b = w_.buildings[i];
        if (!b.alive || !b.constructed || b.trainQueue.size() >= 2) continue;
        const UnitType type = (fs.soldiersTrained % 3 == 2) ? UnitType::Archer : UnitType::Swordsman;
        w_.queueTraining(i, type);
    }
}

// Fala ataku: gdy minie czas i armia jest dość liczna, wszyscy oprócz 2 obrońców
// ruszają na najbliższy budynek przeciwnika. Kolejne fale wymagają większej armii.
void AiPlanner::launchAttackWave(int f, float dt) {
    FactionState& fs = w_.factions[f];
    fs.waveTimer -= dt;
    if (fs.waveTimer > 0.0f) return;

    // Wolni żołnierze (nie walczą i nie są w marszu bojowym).
    std::vector<int> army;
    for (size_t i = 0; i < w_.units.size(); ++i) {
        const Unit& u = w_.units[i];
        if (u.alive && u.faction == f && u.type != UnitType::Settler && static_cast<int>(i) != fs.aiScout &&
            (u.state == UnitState::Idle || (u.state == UnitState::Moving && !u.attackMove))) {
            army.push_back(static_cast<int>(i));
        }
    }
    // Pierwsza fala mała (3 / 4 / 5 wg poziomu), kolejne większe - najwyżej o 4. Obrońców: 1 przy małej armii, 2 przy większej.
    const int minWave = (w_.difficulty == Difficulty::Easy ? 3 : (w_.difficulty == Difficulty::Normal ? 4 : 5)) + std::min(fs.waveNumber, 4);
    const int defenders = army.size() >= 8 ? 2 : 1;
    if (static_cast<int>(army.size()) < minWave + defenders) {
        fs.waveTimer = 20.0f;   // za mało wojska - sprawdzimy za 20 s
        return;
    }

    // Żołnierze najbliżej zamku zostają na obronie.
    const Vec3 home = homeOf(f);
    std::sort(army.begin(), army.end(), [&](int a, int b) { return distanceXZ(w_.units[a].pos, home) < distanceXZ(w_.units[b].pos, home); });
    army.erase(army.begin(), army.begin() + defenders);

    // Cel: ZNANY budynek przeciwnika (na terenie odkrytym przez AI) najbliższy naszemu zamkowi.
    // Nie znamy żadnego - zwiadowca szuka dalej, fala poczeka.
    int target = -1;
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i < w_.buildings.size(); ++i) {
        const Building& b = w_.buildings[i];
        if (!b.alive || b.faction == f || !knows(f, b.pos)) continue;
        const float d = distanceXZ(b.pos, home);
        if (d < best) {
            best = d;
            target = static_cast<int>(i);
        }
    }
    if (target < 0) {
        logDecision(f, "armia gotowa, ale nie zna jeszcze żadnego budynku wroga - czeka na zwiad");
        fs.waveTimer = 20.0f;
        return;
    }
    if (target >= 0) {
        w_.commandAttackBuilding(army, target);
        fs.waveNumber++;
        logDecision(f, "fala ataku nr " + std::to_string(fs.waveNumber) + ": " + std::to_string(army.size()) + " żołnierzy na " +
                             bname(w_.buildings[target].type));
        w_.pushMessage("Armia wroga wyrusza do ataku! (" + std::to_string(army.size()) + " żołnierzy)",
                    {1.0f, 0.35f, 0.3f, 1.0f});
    }
    fs.waveTimer = w_.difficulty == Difficulty::Easy ? 260.0f : (w_.difficulty == Difficulty::Normal ? 180.0f : 120.0f);
}

} // namespace settlers

namespace settlers {

// Najbliższy (od zamku) nieodkryty ląd: pierścienie co 40 jednostek, 24 kierunki. Woda i szczyty odpadają.
bool AiPlanner::unexploredTarget(int f, const Vec3& from, Vec3& target) const {
    const Vec3 home = homeOf(f);
    const auto& failed = scoutFailed_[static_cast<size_t>(f)];
    for (float r = 120.0f; r < 2.0f * kWorldHalf; r += 40.0f) {
        for (int k = 0; k < 24; ++k) {
            const float a = static_cast<float>(k) * 2.0f * PI / 24.0f + r * 0.013f;   // przesunięcie: pierścienie nie w jednej linii
            const Vec3 p{home.x + std::cos(a) * r, 0.0f, home.z + std::sin(a) * r};
            if (std::abs(p.x) > kWorldHalf - 10.0f || std::abs(p.z) > kWorldHalf - 10.0f) continue;
            const float h = w_.heightAt(p.x, p.z);
            if (h < kWaterLevel + 2.0f || h > kMountainLevel + 15.0f) continue;
            if (w_.exploredBy(f, p)) continue;
            if (!w_.nav_.sameRegion(from, p)) continue;   // za wodą / w zamkniętej dolinie - pieszo się nie dojdzie
            bool tried = false;
            for (const Vec3& q : failed) tried |= distanceXZ(q, p) < 60.0f;
            if (tried) continue;
            target = p;
            return true;
        }
    }
    return false;
}

// Zwiad (mgła wojny AI): dopóki AI nie zna wroga albo zna mniej niż 40% mapy, jeden żołnierz (najchętniej łucznik -
// widzi najdalej) chodzi na kolejne nieodkryte miejsca. Bez mgły AI zwiad jest zbędny.
void AiPlanner::scout(int f, float dt) {
    if (!w_.aiFogOfWar) return;
    FactionState& fs = w_.factions[f];
    fs.aiScoutTimer -= dt;
    if (fs.aiScoutTimer > 0.0f) return;
    fs.aiScoutTimer = 6.0f;
    const bool enemyKnown = !eco_[f].knownEnemyBuildings.empty();
    if (enemyKnown && w_.exploredFraction(f) >= 0.4f) {
        fs.aiScout.reset();   // zwiad skończony - żołnierz wraca do armii
        return;
    }
    // Zwiadowca żyje i jeszcze idzie - nie przeszkadzamy.
    if (fs.aiScout >= 0 && w_.units[fs.aiScout].alive && w_.units[fs.aiScout].state == UnitState::Moving) return;
    // Stanął, a cel wciąż nieodkryty - nie doszedł (zablokowana droga); następnym razem inny cel.
    auto& failed = scoutFailed_[static_cast<size_t>(f)];
    if (fs.aiScout >= 0 && w_.units[fs.aiScout].alive && !w_.exploredBy(f, scoutGoal_[static_cast<size_t>(f)]) &&
        distanceXZ(scoutGoal_[static_cast<size_t>(f)], Vec3{}) > 0.0f) {
        failed.push_back(scoutGoal_[static_cast<size_t>(f)]);
        if (failed.size() > 64) failed.erase(failed.begin());
    }
    if (fs.aiScout < 0 || !w_.units[fs.aiScout].alive) {
        int best = -1;
        for (size_t i = 0; i < w_.units.size(); ++i) {
            const Unit& u = w_.units[i];
            if (!u.alive || u.faction != f || u.type == UnitType::Settler || u.state != UnitState::Idle) continue;
            if (best < 0 || (u.type == UnitType::Archer && w_.units[best].type != UnitType::Archer)) best = static_cast<int>(i);
        }
        if (best < 0) return;   // nie ma wolnego żołnierza
        fs.aiScout = best;
        logDecision(f, std::string("wysyła zwiadowcę (") + unitDef(w_.units[best].type).name + ") - " +
                             (enemyKnown ? "mało znanej mapy" : "nie wie, gdzie jest wróg"));
    }
    Vec3 target;
    if (!unexploredTarget(f, w_.units[fs.aiScout].pos, target)) {
        fs.aiScout.reset();
        return;
    }
    scoutGoal_[static_cast<size_t>(f)] = target;
    w_.commandMove({fs.aiScout.get()}, target, false);
}

} // namespace settlers

namespace settlers {

// Próbki w kole zasięgu wieży (3 pierścienie x 16 kierunków): ląd nad wodą, nie nasz, osiągalny z zamku.
// Teren nieodkryty też się liczy - wieża go odsłoni (kształt terenu AI może znać, mgła ukrywa obiekty).
float AiPlanner::towerGain(int f, const Vec3& p) const {
    const float R = buildingDef(BuildingType::Tower).territoryRadius;
    const Vec3 home = homeOf(f);
    int total = 0, gain = 0;
    for (float k : {0.4f, 0.7f, 0.95f}) {
        for (int a = 0; a < 16; ++a) {
            const float ang = static_cast<float>(a) * 2.0f * PI / 16.0f;
            const Vec3 q{p.x + std::cos(ang) * R * k, 0.0f, p.z + std::sin(ang) * R * k};
            ++total;
            if (std::abs(q.x) > kWorldHalf || std::abs(q.z) > kWorldHalf) continue;
            if (w_.heightAt(q.x, q.z) < kWaterLevel + 1.0f || w_.territoryOwnerAt(q) == f) continue;
            if (w_.landReachable(home, q)) ++gain;
        }
    }
    return total > 0 ? static_cast<float>(gain) / static_cast<float>(total) : 0.0f;
}

} // namespace settlers

namespace settlers {

bool AiPlanner::defenseFront(int f, Vec3& front, int& level) const {
    const Vec3 home = homeOf(f);
    const FactionState& fs = w_.factions[f];
    Vec3 threat;
    level = 0;
    if (w_.gameTime - fs.lastAttackTime < 180.0f) {
        threat = fs.lastAttackPos;   // stąd przyszedł ostatni atak
        level = 2;
    } else {
        // Najbliższy znany budynek wroga - jeśli blisko, stamtąd najpewniej przyjdzie atak.
        float best = 650.0f;
        for (int bi : eco_[f].knownEnemyBuildings) {
            const float d = distanceXZ(w_.buildings[bi].pos, home);
            if (d < best) {
                best = d;
                threat = w_.buildings[bi].pos;
                level = 1;
            }
        }
    }
    if (level == 0) return false;
    // Od zagrożenia w stronę zamku do pierwszego punktu na naszym terenie, potem kawałek w głąb.
    Vec3 dir = subtract(home, threat);
    dir.y = 0.0f;
    const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (len < 1.0f) {
        front = home;
        return true;
    }
    dir = multiply(dir, 1.0f / len);
    for (float d = 0.0f; d < len; d += 10.0f) {
        const Vec3 p = add(threat, multiply(dir, d));
        if (w_.territoryOwnerAt(p) == f) {
            front = add(p, multiply(dir, 20.0f));
            return true;
        }
    }
    front = home;   // atak w środku naszego terenu - bronimy zamku
    return true;
}

} // namespace settlers
