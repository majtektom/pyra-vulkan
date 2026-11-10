/**
 * world_ai.cpp - komputerowy przeciwnik.
 *
 * AI gra na tych samych zasadach co gracz (koszty, budowa przez osadników, tragarze, szkolenie) i bez dochodu
 * biernego - poziom trudności to zapasy startowe i tempo fal.
 *
 * Budowa NIE idzie według sztywnej listy. Co 3 sekundy planista (aiBuildNext) czyta stan gospodarki - te same
 * statystyki i ostrzeżenia, które widzi gracz - i stawia to, czego brakuje:
 *   1. sprzątanie: kamieniarz bez skał i drwal bez drzew (i sadzonek) są burzeni - i tak nic nie wydobędą,
 *   2. pilne: Dom przy limicie ludności, Magazyn przy zapełnionych magazynach / zakorkowanych flagach,
 *   3. proporcje warsztatów (tabela kAiRatios): np. 1 tartak obsługuje 4 drwali, 1 leśniczy wystarcza na 3 drwali -
 *      brakujący przetwórca jest stawiany od razu, a nowy dostawca dopiero, gdy przetwórca ma wolne moce,
 *   4. braki towarów: dostawy czekające na towar ("dostawy czekają na") + zapas poniżej celu (deski, kamień,
 *      żywność, broń). Dla brakującego towaru aiPlanFor wybiera producenta, schodząc po łańcuchu
 *      (broń -> kuźnia -> żelazo -> huta -> ruda/węgiel -> kopalnie),
 *   5. rozwój: koszary, wieże (gdy brakuje miejsca albo surowców na terytorium).
 * Producenta danego towaru nie dobudowuje się, gdy inny taki sam jest w budowie, jeszcze nic nie wyprodukował
 * albo stoi z pełną flagą (towar jest, tylko nie ma kto go odebrać) - to chroni przed nadprodukcją budynków.
 *
 * Co kilka minut AI zbiera armię i wysyła falę ataku na najbliższy budynek przeciwnika.
 * Drogi AI buduje automatycznie: każdy budynek łączy z najbliższą flagą sieci (autoConnectFlag).
 */
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
void World::updateAI(int f, float dt) {
    FactionState& fs = factions[f];
    if (fs.defeated) return;

    // Limity zapasu AI (jak gracz w oknie Gospodarka): deski i broń bez limitu zapełniałyby kolejne magazyny.
    if (fs.stockLimit[static_cast<int>(CarryType::Planks)] >= kNoStockLimit) fs.stockLimit[static_cast<int>(CarryType::Planks)] = 150;
    if (fs.stockLimit[static_cast<int>(CarryType::Weapons)] >= kNoStockLimit) fs.stockLimit[static_cast<int>(CarryType::Weapons)] = 60;

    fs.aiThinkTimer -= dt;
    if (fs.aiThinkTimer <= 0.0f) {
        fs.aiThinkTimer = 3.0f;
        aiConnectRoads(f);
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

// Stan gospodarki frakcji widziany przez AI.
World::AiEconomy World::aiEconomy(int f) const {
    AiEconomy e;
    for (const auto& b : buildings) {
        if (!b.alive || b.faction != f) continue;
        const size_t t = static_cast<size_t>(b.type);
        e.total[t]++;
        if (!b.constructed) {
            e.inProgress[t]++;
            ++e.underConstruction;
            continue;
        }
        // "Rozgrzewa się": jeszcze nic nie wyprodukował (świeżo postawiony) - nie oceniamy go jeszcze.
        if (producesGoods(b.type) && b.produced == 0) e.warmingUp[t]++;
        if (b.outputBlocked) e.blocked[t]++;
        if (producesGoods(b.type) && b.flag >= 0 && flagAvailableGoods(flags[b.flag]) >= 4) e.backlog[t]++;
        if (b.inputStarved) e.starved[t]++;
    }
    e.have = spendable(f);
    for (const auto& o : orders) {   // dostawy bez pokrycia w towarze = to, czego brakuje odbiorcom
        if (o.alive && o.faction == f && !o.funded) e.waiting[o.type] += o.amount;
    }
    for (const auto& fl : flags) {
        if (fl.alive && fl.faction == f) e.onFlags += static_cast<int>(fl.goods.size());
    }
    // Ludzie: tragarzy mniej niż połowa potrzebnych albo brak jedzenia na nowych osadników (koszt: 3 żywności).
    const TransportStats ts = transportStats(f);
    e.peopleShort = ts.carriers * 2 < ts.target || e.have.food() < 6;
    return e;
}

// Czy można dobudować kolejnego producenta typu t (żaden się nie buduje, każdy już coś dał, żaden nie stoi z pełną flagą).
bool World::aiProducerReady(const AiEconomy& e, BuildingType t) const {
    const size_t i = static_cast<size_t>(t);
    // Zaległe towary na flagach = brakuje transportu (tragarzy), a nie producentów.
    return e.inProgress[i] == 0 && e.warmingUp[i] == 0 && e.blocked[i] == 0 && e.backlog[i] == 0;
}

// Kto wyprodukuje towar g. Zwraca budynek do postawienia albo Count (nic / jeszcze nie teraz).
// Schodzi po łańcuchu: gdy przetwórcy brakuje surowca, którego nikt nie wytwarza, najpierw jego producent.
BuildingType World::aiPlanFor(int f, const AiEconomy& e, CarryType g, int depth) {
    if (depth > 4) return BuildingType::Count;
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };
    auto ready = [&](BuildingType t) { return aiProducerReady(e, t); };
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
        if (sup == 0) return aiPlanFor(f, e, supplierGood, depth + 1);
        if (proc == 0 || sup >= proc * per) return ready(processor) ? processor : BuildingType::Count;
        return aiPlanFor(f, e, supplierGood, depth + 1);
    };
    switch (g) {
    case CarryType::Wood: return ready(BuildingType::Woodcutter) ? BuildingType::Woodcutter : BuildingType::Count;
    case CarryType::Planks: return chain(BuildingType::Sawmill, CarryType::Wood);
    case CarryType::Stone: {
        // Kamieniarz, gdy na terytorium są skały poza zasięgiem obecnych kamieniarzy; inaczej kopalnia w górach.
        if (!ready(BuildingType::Quarry) || !ready(BuildingType::Mine)) return BuildingType::Count;
        Vec3 c;
        if (aiResourceCluster(f, NodeKind::Rock, BuildingType::Quarry, c) >= 3) return BuildingType::Quarry;
        // Kopalnia je żywność - bez działającego rybaka/myśliwego najpierw jedzenie.
        if (n(BuildingType::Fisher) + n(BuildingType::Hunter) - e.inProgress[static_cast<size_t>(BuildingType::Fisher)] -
                e.inProgress[static_cast<size_t>(BuildingType::Hunter)] == 0) {
            return aiPlanFor(f, e, CarryType::AnyFood, depth + 1);
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
        return aiPlanFor(f, e, CarryType::Bread, depth + 1);
    }
    case CarryType::Grain: return ready(BuildingType::Farm) ? BuildingType::Farm : BuildingType::Count;
    case CarryType::Flour: return chain(BuildingType::Mill, CarryType::Grain);
    case CarryType::Bread: return chain(BuildingType::Bakery, CarryType::Flour);
    case CarryType::Coal: return ready(BuildingType::CoalMine) ? BuildingType::CoalMine : BuildingType::Count;
    case CarryType::Ore: return ready(BuildingType::OreMine) ? BuildingType::OreMine : BuildingType::Count;
    case CarryType::Iron:
        if (n(BuildingType::CoalMine) == 0) return aiPlanFor(f, e, CarryType::Coal, depth + 1);   // huta potrzebuje też węgla
        return chain(BuildingType::Smelter, CarryType::Ore);
    case CarryType::Weapons: return chain(BuildingType::Smithy, CarryType::Iron);
    default: return BuildingType::Count;
    }
}

// Najlepsze skupisko drzew/skał na terytorium frakcji, którego nie obsługuje jeszcze żaden budynek typu "worker".
// Zwraca liczbę surowców w skupisku (promień 60) i jego środek.
int World::aiResourceCluster(int f, NodeKind kind, BuildingType worker, Vec3& center) const {
    const float coveredR = buildingDef(worker).workRadius * 0.5f;
    int best = 0;
    int sampled = 0;
    for (size_t i = 0; i < nodes.size() && sampled < 80; i += 3) {   // co trzeci węzeł - wystarczy do oceny
        const ResourceNode& nd = nodes[i];
        if (!nd.alive || nd.kind != kind || territoryOwnerAt(nd.pos) != f) continue;
        bool covered = false;
        for (const auto& b : buildings) {
            covered |= b.alive && b.faction == f && b.type == worker && dist2D(b.pos, nd.pos) < coveredR;
        }
        if (covered) continue;
        ++sampled;
        const int c = countNodesInRange(kind, nd.pos, 60.0f);
        if (c > best) {
            best = c;
            center = nd.pos;
        }
    }
    return best;
}

// Cel rozbudowy terytorium: najbliższy (od zamku) punkt poza naszym i wrogim terenem, w którym jest to, czego
// zabrakło: drzewa (drwal, leśniczy), skały (kamieniarz), góry (kopalnie). Inaczej - wolny teren najdalej od wroga.
bool World::aiExpansionTarget(int f, BuildingType wanted, Vec3& target) const {
    const Vec3 home = aiHome(f);
    auto freeLand = [&](const Vec3& p) { return territoryOwnerAt(p) < 0; };
    float best = 1e9f;
    bool found = false;
    auto consider = [&](const Vec3& p) {
        const float d = dist2D(p, home);
        if (d < best && d < 700.0f && freeLand(p)) {
            best = d;
            target = p;
            found = true;
        }
    };
    const bool trees = wanted == BuildingType::Woodcutter || wanted == BuildingType::Forester || wanted == BuildingType::Hunter;
    const bool rocks = wanted == BuildingType::Quarry;
    const bool mountains = wanted != BuildingType::Count && productionDef(wanted).kind == WorkKind::Mine;
    if (trees || rocks) {
        for (const auto& nd : nodes) {
            if (nd.alive && nd.kind == (trees ? NodeKind::Tree : NodeKind::Rock)) consider(nd.pos);
        }
    } else if (mountains) {
        for (float r = 100.0f; r < 700.0f; r += 25.0f) {
            for (int k = 0; k < 36; ++k) {
                const float a = static_cast<float>(k) * 2.0f * PI / 36.0f;
                const Vec3 p{home.x + std::cos(a) * r, 0.0f, home.z + std::sin(a) * r};
                const float h = heightAt(p.x, p.z);
                if (h >= kMountainLevel && h < kMountainLevel + 20.0f) consider(p);   // zbocze (szczyty są nieprzechodnie)
            }
        }
    }
    if (found) return true;
    // Wolny teren: kierunek od wroga (rozbudowa "do tyłu" jest bezpieczniejsza), sprawdzamy wachlarz kierunków.
    const int enemy = f == kPlayer ? kEnemy : kPlayer;
    Vec3 away = subtract(home, aiHome(enemy));
    const float len = std::sqrt(away.x * away.x + away.z * away.z);
    const float base = len > 1.0f ? std::atan2(away.z, away.x) : 0.0f;
    for (int k = 0; k < 12 && !found; ++k) {
        const float a = base + static_cast<float>((k + 1) / 2) * ((k % 2) ? 0.5f : -0.5f);
        for (float r = 150.0f; r < 500.0f && !found; r += 25.0f) {
            const Vec3 p{home.x + std::cos(a) * r, 0.0f, home.z + std::sin(a) * r};
            if (heightAt(p.x, p.z) > kWaterLevel + 2.0f && heightAt(p.x, p.z) < kMountainLevel && freeLand(p)) {
                target = p;
                found = true;
            }
        }
    }
    return found;
}

// Miejsce dla budynku: przy surowcach / dostawcach / zamku, zależnie od typu.
bool World::aiFindSpot(int f, BuildingType t, Vec3& spot) {
    const Vec3 home = aiHome(f);
    const float maxR = buildingDef(BuildingType::Castle).territoryRadius - 25.0f;
    auto centroid = [&](std::initializer_list<BuildingType> types, Vec3& c) {
        Vec3 sum{};
        int n = 0;
        for (const auto& b : buildings) {
            if (!b.alive || b.faction != f) continue;
            for (BuildingType ty : types) {
                if (b.type == ty) {
                    sum = add(sum, b.pos);
                    ++n;
                }
            }
        }
        if (n > 0) c = multiply(sum, 1.0f / static_cast<float>(n));
        return n > 0;
    };
    Vec3 c;
    switch (t) {
    case BuildingType::Woodcutter:
        // Drwal tylko przy lesie (choćby małym) - bez drzew na terytorium AI poszerza teren wieżą w stronę lasu.
        return aiResourceCluster(f, NodeKind::Tree, t, c) >= 3 && findPlacementNear(t, f, c, 8.0f, 45.0f, spot);
    case BuildingType::Quarry:
        // Kamieniarz tylko przy skałach - gdzie indziej od razu zostałby bez pracy.
        return aiResourceCluster(f, NodeKind::Rock, t, c) >= 3 && findPlacementNear(t, f, c, 8.0f, 40.0f, spot);
    case BuildingType::Forester: {
        // Przy drwalu, który nie ma leśniczego w pobliżu (leśniczy sadzi w promieniu 60).
        for (const auto& w : buildings) {
            if (!w.alive || w.faction != f || w.type != BuildingType::Woodcutter) continue;
            bool has = false;
            for (const auto& b : buildings) {
                has |= b.alive && b.faction == f && b.type == BuildingType::Forester && dist2D(b.pos, w.pos) < 100.0f;
            }
            if (!has && findPlacementNear(t, f, w.pos, 12.0f, 50.0f, spot)) return true;
        }
        break;
    }
    case BuildingType::Sawmill:
        if (centroid({BuildingType::Woodcutter}, c) && findPlacementNear(t, f, c, 10.0f, 60.0f, spot)) return true;
        break;
    case BuildingType::Mill:
    case BuildingType::Bakery:
        if (centroid({BuildingType::Farm, BuildingType::Mill}, c) && findPlacementNear(t, f, c, 10.0f, 60.0f, spot)) return true;
        break;
    case BuildingType::Smelter:
    case BuildingType::Smithy:
        if (centroid({BuildingType::CoalMine, BuildingType::OreMine, BuildingType::Smelter}, c) &&
            findPlacementNear(t, f, c, 15.0f, 70.0f, spot)) {
            return true;
        }
        break;
    case BuildingType::Warehouse: {
        // Magazyn blisko produkcji: w okolicy środka ciężkości warsztatów.
        Vec3 sum{};
        int n = 0;
        for (const auto& b : buildings) {
            if (b.alive && b.faction == f && producesGoods(b.type)) {
                sum = add(sum, b.pos);
                ++n;
            }
        }
        if (n > 0 && findPlacementNear(t, f, multiply(sum, 1.0f / static_cast<float>(n)), 10.0f, 70.0f, spot)) return true;
        break;
    }
    case BuildingType::Tower: {
        // Wieża przy granicy terytorium, w stronę tego, czego brakuje: idziemy od celu w stronę zamku
        // aż do pierwszego punktu na własnym terenie.
        Vec3 target;
        if (aiExpansionTarget(f, factions[f].aiExpandFor >= 0 ? static_cast<BuildingType>(factions[f].aiExpandFor) : BuildingType::Count, target)) {
            Vec3 dir = subtract(home, target);
            dir.y = 0.0f;
            const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            if (len > 1.0f) {
                dir = multiply(dir, 1.0f / len);
                for (float d = 0.0f; d < len; d += 15.0f) {
                    const Vec3 p = add(target, multiply(dir, d));
                    if (territoryOwnerAt(p) != f) continue;
                    const Vec3 inside = add(p, multiply(dir, 20.0f));   // kawałek w głąb, żeby budynek zmieścił się na naszym
                    if (findPlacementNear(t, f, inside, 0.0f, 35.0f, spot)) return true;
                    break;
                }
            }
        }
        return findPlacementNear(t, f, home, maxR * 0.7f, maxR + 60.0f, spot);
    }
    case BuildingType::Barracks:
        if (findPlacementNear(t, f, home, 30.0f, maxR, spot)) return true;
        return findPlacementNear(t, f, home, maxR, maxR + 120.0f, spot);   // duży budynek - szukamy dalej
    default: break;
    }
    // Kopalnie (góry), rybak (woda) i reszta: gdziekolwiek na terytorium - canPlace sprawdza warunki.
    return findPlacementNear(t, f, home, 30.0f, maxR + 60.0f, spot);
}

// Planista budowy (opis kolejności na górze pliku). Najwyżej 2 budowy naraz, jeden nowy budynek na krok.
void World::aiBuildNext(int f) {
    FactionState& fs = factions[f];
    const AiEconomy e = aiEconomy(f);
    auto n = [&](BuildingType t) { return e.total[static_cast<size_t>(t)]; };

    // 1. Sprzątanie: wyczerpany kamieniarz / drwal bez drzew i bez sadzonek - burzymy (pracownik wraca do puli).
    for (size_t i = 0; i < buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (!b.alive || b.faction != f || !b.constructed || b.resourcesInRange != 0) continue;
        const bool exhausted = b.type == BuildingType::Quarry ||
                               (b.type == BuildingType::Woodcutter && saplingsNear(b.pos, buildingDef(b.type).workRadius) == 0);
        if (!exhausted) continue;
        aiLogDecision(f, std::string("burzy ") + bname(b.type) + " - nie ma już czego wydobywać");
        demolish(static_cast<int>(i));
        return;
    }
    // Plac budowy, który od 90 s nie ma drogi do magazynu - materiały nigdy nie dotrą, a blokuje limit 2 budów.
    for (size_t i = 0; i < buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (!b.alive || b.faction != f || b.constructed || b.type == BuildingType::Castle) continue;
        if (flagConnected(b.flag) || gameTime - b.placedTime < 90.0f) continue;
        aiLogDecision(f, std::string("burzy plac ") + bname(b.type) + " - od 90 s bez drogi");
        demolish(static_cast<int>(i));
        return;
    }
    // Limit ludności: nowi osadnicy (budowniczowie, tragarze) pojawią się dopiero po Domu - Dom może więc powstać
    // ponad limit 2 budów, a innych placów nie zaczynamy (nie byłoby kim ich budować).
    const bool atPopCap = fs.population >= fs.popCap - 1 && fs.popCap < kMaxPopulation;   // przy maksimum domy nic nie dadzą
    const bool houseBuilding = e.inProgress[static_cast<size_t>(BuildingType::House)] > 0;
    if (e.underConstruction >= 2 && !(atPopCap && !houseBuilding)) return;
    if (atPopCap && houseBuilding) return;

    BuildingType next = BuildingType::Count;
    std::string why;
    auto choose = [&](BuildingType t, std::string reason) {
        if (next != BuildingType::Count || t == BuildingType::Count) return;
        next = t;
        why = std::move(reason);
    };

    // 2. Pilne: mieszkańcy i magazyny.
    if (fs.popCap - fs.population <= 2 && fs.popCap < kMaxPopulation && e.inProgress[static_cast<size_t>(BuildingType::House)] == 0) {
        choose(BuildingType::House, "limit ludności " + std::to_string(fs.population) + "/" + std::to_string(fs.popCap));
    }
    // Brak miejsca / surowców na terytorium (nie udało się postawić potrzebnego budynku) - wieża w tamtą stronę.
    if (fs.aiPlacementFailures >= 2 && e.inProgress[static_cast<size_t>(BuildingType::Tower)] == 0) {
        choose(BuildingType::Tower, std::string("brak miejsca na terytorium dla: ") +
                                        (fs.aiExpandFor >= 0 ? bname(static_cast<BuildingType>(fs.aiExpandFor)) : "budynku") +
                                        " - wieża poszerzy teren w tę stronę");
    }
    if (aiNeedsWarehouse(f, e)) {
        choose(BuildingType::Warehouse, "magazyny " + std::to_string(storageUsedOf(f)) + "/" + std::to_string(storageCapacityOf(f)) +
                                            ", na flagach " + std::to_string(e.onFlags));
    }

    // 3. Proporcje warsztatów: dostawcy bez przetwórcy (np. 5 drwali i 1 tartak).
    for (const AiRatio& r : kAiRatios) {
        const int suppliers = n(r.supplier) - e.inProgress[static_cast<size_t>(r.supplier)];
        const int needed = (suppliers + r.perProcessor - 1) / r.perProcessor;
        if (suppliers > 0 && n(r.processor) < needed && e.inProgress[static_cast<size_t>(r.processor)] == 0) {
            choose(r.processor, std::string(bname(r.processor)) + " - 1 na " + std::to_string(r.perProcessor) + " (" +
                                    bname(r.supplier) + ": " + std::to_string(suppliers) + ", " + bname(r.processor) + ": " +
                                    std::to_string(n(r.processor)) + ")");
        }
    }

    // 4. Braki towarów: czekające dostawy + zapas poniżej celu. Kolejność = ważność.
    const bool military = n(BuildingType::Barracks) > 0;
    const int foodTarget = kAiStockFood + 5 * (n(BuildingType::Mine) + n(BuildingType::CoalMine) + n(BuildingType::OreMine));
    struct Need {
        CarryType good;
        int amount;
    };
    // Brak ludzi (tragarzy / jedzenia na osadników) - najpierw żywność, potem materiały.
    if (e.peopleShort) {
        const int foodNeed = e.waiting[CarryType::AnyFood] + e.waiting.food() + std::max(0, foodTarget - e.have.food());
        if (foodNeed > 0) {
            choose(aiPlanFor(f, e, CarryType::AnyFood, 0), "brak ludzi: żywność " + std::to_string(e.have.food()) +
                                                               " - nowi osadnicy i tragarze kosztują jedzenie");
        }
    }
    const Need needs[] = {
        {CarryType::Planks, e.waiting[CarryType::Planks] + std::max(0, kAiStockPlanks - e.have[CarryType::Planks])},
        {CarryType::Stone, e.waiting[CarryType::Stone] + std::max(0, kAiStockStone - e.have[CarryType::Stone])},
        {CarryType::AnyFood, e.waiting[CarryType::AnyFood] + e.waiting.food() + std::max(0, foodTarget - e.have.food())},
        {CarryType::Wood, e.waiting[CarryType::Wood]},
        {CarryType::Coal, e.waiting[CarryType::Coal]},
        {CarryType::Ore, e.waiting[CarryType::Ore]},
        {CarryType::Iron, e.waiting[CarryType::Iron]},
        {CarryType::Grain, e.waiting[CarryType::Grain]},
        {CarryType::Flour, e.waiting[CarryType::Flour]},
        {CarryType::Weapons, military ? e.waiting[CarryType::Weapons] + std::max(0, kAiStockWeapons - e.have[CarryType::Weapons]) : 0},
    };
    for (const Need& nd : needs) {
        if (nd.amount <= 0 || next != BuildingType::Count) continue;
        choose(aiPlanFor(f, e, nd.good, 0), std::string("brak: ") + goodName(nd.good == CarryType::AnyFood ? CarryType::Bread : nd.good) +
                                                 " " + std::to_string(nd.amount) + " (w zapasie " +
                                                 std::to_string(nd.good == CarryType::AnyFood ? e.have.food() : e.have[nd.good]) + ")");
    }

    // 5. Minimalna gospodarka (kAiMinimum). Kamieniarz liczy się razem z kopalnią kamienia, rybak z myśliwym
    //    (bez wody rybaka nie da się postawić).
    for (const AiMinimum& m : kAiMinimum) {
        int have = n(m.type);
        if (m.type == BuildingType::Quarry) have += n(BuildingType::Mine);
        if (m.type == BuildingType::Fisher && n(BuildingType::Hunter) > 1) have += 1;
        if (have >= m.count) continue;
        BuildingType t = m.type;
        if (t == BuildingType::Quarry) t = aiPlanFor(f, e, CarryType::Stone, 0);   // kamieniarz albo kopalnia
        if (t == BuildingType::Count) continue;
        choose(t, std::string("podstawa gospodarki: ") + bname(m.type) + " " + std::to_string(have) + "/" + std::to_string(m.count));
    }
    // Wzrost: materiałów dość, a ludzi zaraz zabraknie - więcej domów = więcej pracowników i tragarzy.
    if (e.have[CarryType::Planks] >= 40 && e.have[CarryType::Stone] >= 25 && fs.population * 10 >= fs.popCap * 7 &&
        fs.popCap < kMaxPopulation &&
        e.inProgress[static_cast<size_t>(BuildingType::House)] == 0) {
        choose(BuildingType::House, "wzrost: materiały są, ludność " + std::to_string(fs.population) + "/" + std::to_string(fs.popCap));
    }
    int buildingsTotal = 0;
    for (size_t t = 0; t < e.total.size(); ++t) buildingsTotal += e.total[t];
    if (e.inProgress[static_cast<size_t>(BuildingType::Tower)] == 0 &&
        n(BuildingType::Tower) < buildingsTotal / 10) {
        choose(BuildingType::Tower, "rozwój terytorium (1 wieża na 10 budynków)");
    }
    if (next == BuildingType::Count) return;
    if (atPopCap && next != BuildingType::House && e.underConstruction >= 2) return;

    // Rezerwa: bez tartaku i źródła kamienia nie da się nic odbudować - dopóki ich nie ma, materiały na nie
    // są nietykalne (inaczej AI wydawało startowe deski na domy i zostawało bez desek na tartak).
    {
        const Resources& cost = buildingDef(next).cost;
        const bool stoneMaker = next == BuildingType::Quarry || next == BuildingType::Mine;
        const bool plankMaker = next == BuildingType::Sawmill || next == BuildingType::Woodcutter;
        // Kolejno: tartak, źródło kamienia, magazyn - pierwszy brakujący, na który odkładamy materiały.
        BuildingType reserved = BuildingType::Count;
        if (n(BuildingType::Sawmill) == 0 && !plankMaker) {
            reserved = BuildingType::Sawmill;
        } else if (n(BuildingType::Quarry) + n(BuildingType::Mine) == 0 && !stoneMaker) {
            reserved = aiPlanFor(f, e, CarryType::Stone, 0);
        } else if (aiNeedsWarehouse(f, e) && next != BuildingType::Warehouse) {
            // Magazyny pełne: bez nowego magazynu warsztaty staną (deski nie będą miały gdzie trafić).
            reserved = BuildingType::Warehouse;
        }
        // Wieża po las/skały na start (bez nich nie ma desek ani kamienia) - bez rezerwy.
        const bool bootstrapTower = next == BuildingType::Tower &&
                                    (fs.aiExpandFor == static_cast<int>(BuildingType::Woodcutter) ||
                                     fs.aiExpandFor == static_cast<int>(BuildingType::Quarry));
        if (reserved != BuildingType::Count && reserved != next && !bootstrapTower) {
            const Resources& keep = buildingDef(reserved).cost;
            // Rezerwa działa tylko, gdy wybrany budynek dałoby się opłacić, ale zjadłby materiał odłożony na "reserved".
            const bool affordable = e.have.canAfford(cost);
            if (affordable && (e.have[CarryType::Planks] - cost[CarryType::Planks] < keep[CarryType::Planks] ||
                               e.have[CarryType::Stone] - cost[CarryType::Stone] < keep[CarryType::Stone])) {
                why = std::string("najpierw ") + bname(reserved) + " (rezerwa materiałów; odłożone: " + bname(next) + ")";
                next = reserved;
            }
        }
    }

    if (!e.have.canAfford(buildingDef(next).cost)) {
        // Brakuje materiału na wybrany budynek - zamiast czekać, produkujemy go (deski / kamień).
        const Resources& cost = buildingDef(next).cost;
        const CarryType lack = e.have[CarryType::Planks] < cost[CarryType::Planks] ? CarryType::Planks : CarryType::Stone;
        const BuildingType alt = aiPlanFor(f, e, lack, 0);
        if (alt == BuildingType::Count || alt == next || !e.have.canAfford(buildingDef(alt).cost)) return;   // czekamy
        why = std::string("na ") + bname(next) + " brakuje: " + goodName(lack);
        next = alt;
    }

    Vec3 spot;
    bool found = aiFindSpot(f, next, spot);
    if (!found && next == BuildingType::Fisher) {   // brak wody - myśliwy
        next = BuildingType::Hunter;
        found = aiFindSpot(f, next, spot);
    }
    if (!found && next == BuildingType::Quarry) {   // brak skał - kopalnia kamienia w górach
        next = BuildingType::Mine;
        found = aiFindSpot(f, next, spot);
    }
    if (!found) {
        fs.aiPlacementFailures++;
        if (next != BuildingType::Tower) fs.aiExpandFor = static_cast<int>(next);
        aiLogDecision(f, std::string("nie ma gdzie postawić: ") + bname(next) + " (" + why + ")");
        return;
    }
    if (next == BuildingType::Tower) {
        fs.aiPlacementFailures = 0;
        fs.aiExpandFor = -1;
    } else {
        fs.aiPlacementFailures = std::max(0, fs.aiPlacementFailures - 1);
    }
    const int id = placeBuilding(next, spot, f, false, 0.0f);
    if (!autoConnectFlag(buildings[id].flag)) {   // bez drogi tragarze nie dowiozą materiałów
        demolish(id);
        fs.aiPlacementFailures++;
        aiLogDecision(f, std::string("nie da się poprowadzić drogi do: ") + bname(next) + " - rezygnuje");
        return;
    }
    aiLogDecision(f, std::string("buduje ") + bname(next) + " - " + why);
}

// Nowy Magazyn: tylko gdy warsztaty stoją, bo magazyny są pełne (albo zakorkowane flagi przy małej liczbie magazynów),
// i najwyżej 1 magazyn na 8 budynków - inaczej deski bez limitu zapełniałyby kolejne magazyny w nieskończoność.
bool World::aiNeedsWarehouse(int f, const AiEconomy& e) const {
    if (e.inProgress[static_cast<size_t>(BuildingType::Warehouse)] > 0) return false;
    int total = 0, storeFull = 0;
    for (const auto& b : buildings) {
        if (!b.alive || b.faction != f) continue;
        ++total;
        if (b.constructed && b.outputBlocked && flagBlockReason(b.flag) == FlagBlock::StorageFull) ++storeFull;
    }
    const int stores = e.total[static_cast<size_t>(BuildingType::Warehouse)];
    if (stores >= 1 + total / 8) return false;
    return storeFull > 0 || storageUsedOf(f) * 10 >= storageCapacityOf(f) * 9 || (e.onFlags > 30 && stores < 2);
}

void World::aiLogDecision(int f, const std::string& text) {
    if (aiLog) LOG_INFO("[ai] t={}s {}: {}", static_cast<int>(gameTime), factionName(f), text);
}

// Budynki bez połączenia z siecią dróg: AI dobudowuje drogę (najwyżej 2 na raz,
// żeby jeden "myślący" krok nie trwał za długo).
void World::aiConnectRoads(int f) {
    int tries = 0;
    for (const auto& b : buildings) {
        if (tries >= 2) break;
        if (!b.alive || b.faction != f || b.flag < 0 || b.type == BuildingType::Castle) continue;
        if (!b.constructed && b.type == BuildingType::Warehouse && flagConnected(b.flag)) continue;
        if (buildingLinked(b)) continue;
        ++tries;
        autoConnectFlag(b.flag);
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
