/**
 * transport.cpp - przewóz towarów (klasa Transport, transport.h): trasy po drogach, opłacanie zleceń, tragarze.
 *
 * Jak płyną towary - opis na górze world_roads.cpp (flagi, drogi, magazyny zostają w World).
 */
#include "transport.h"

#include <queue>

#include "world_internal.h"

namespace settlers {

const Transport::NetDistances& Transport::networkFrom(int from) const {
    NetDistances& e = netCache_[from];
    if (e.version != roadGraphVersion_ || e.dist.size() != w_.flags.size()) {
        networkDistances(from, e.dist, e.prev);
        e.version = roadGraphVersion_;
    }
    return e;
}

void Transport::networkDistances(int from, std::vector<float>& dist, std::vector<int>& prevRoad) const {
    dist.assign(w_.flags.size(), 1e9f);
    prevRoad.assign(w_.flags.size(), -1);
    if (from < 0 || from >= static_cast<int>(w_.flags.size()) || !w_.flags[from].alive) return;
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    dist[from] = 0.0f;
    open.push({0.0f, from});
    while (!open.empty()) {
        const auto [d, cur] = open.top();
        open.pop();
        if (d > dist[cur]) continue;
        for (int r : w_.flags[cur].roads) {
            if (!w_.roads[r].alive) continue;
            const int next = w_.otherEnd(w_.roads[r], cur);
            const float nd = d + w_.roads[r].length;
            if (nd < dist[next]) {
                dist[next] = nd;
                prevRoad[next] = r;
                open.push({nd, next});
            }
        }
    }
}

// Trasa po drogach: odtwarzamy łańcuch odcinków od celu do startu i sklejamy ich punkty
// (każdy odcinek w kierunku marszu).
bool Transport::networkPath(int from, int to, std::vector<Vec3>& out) const {
    out.clear();
    if (from == to) {
        if (to >= 0) out.push_back(w_.flags[to].pos);
        return to >= 0;
    }
    const NetDistances& net = networkFrom(from);
    const auto& dist = net.dist;
    const auto& prev = net.prev;
    if (to < 0 || to >= static_cast<int>(w_.flags.size()) || dist[to] >= 1e8f) return false;
    std::vector<int> chain;   // odcinki od celu do startu
    for (int cur = to; cur != from;) {
        const int r = prev[cur];
        chain.push_back(r);
        cur = w_.otherEnd(w_.roads[r], cur);
    }
    int at = from;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const Road& r = w_.roads[*it];
        if (r.flagA == at) {
            out.insert(out.end(), r.points.begin(), r.points.end());
        } else {
            out.insert(out.end(), r.points.rbegin(), r.points.rend());
        }
        at = w_.otherEnd(r, at);
    }
    return true;
}

// Opłaca zlecenie towarem najbliższym odbiorcy:
//  - z magazynu (zapas zdejmowany od razu),
//  - albo leżącym na fladze (np. deski na fladze tartaku) - towar zostaje na fladze, ale jest zarezerwowany.
// Dzięki flagom budowa i warsztaty dostają towar, nawet gdy magazyny są pełne albo towar przekroczył limit zapasu.
// Flagi mają lekką przewagę (x0,8 odległości) - zabranie towaru z flagi odblokowuje warsztat.
bool Transport::fundOrder(TransportOrder& o) {
    if (o.dest < 0) return false;   // odbiorca zniknął (zlecenie zaraz zostanie zwolnione)
    const Building& dst = w_.buildings[o.dest];
    const Vec3 at = dst.pos;
    // Dowolna żywność: rodzaj, którego jest najwięcej (count zwraca liczbę dostępnych sztuk danego rodzaju).
    auto pickType = [&](auto count) {
        if (o.type != CarryType::AnyFood) return o.type;
        CarryType best = CarryType::Bread;
        for (CarryType fd : kFoods) {
            if (count(fd) > count(best)) best = fd;
        }
        return best;
    };

    // Tylko źródła osiągalne po drogach od odbiorcy - towaru z magazynu bez drogi nikt nie doniesie,
    // a opłacone zlecenie blokowałoby budowę na zawsze. Odbiorca bez drogi: zlecenie poczeka na drogę.
    if (dst.flag < 0 || !w_.flags[dst.flag].alive || !w_.flags[dst.flag].connected) return false;
    const auto& dist = networkFrom(dst.flag).dist;

    int bestStore = -1, bestFlag = -1;
    float bestD = 1e9f;
    CarryType bestType = o.type;
    for (size_t i = 0; i < w_.buildings.size(); ++i) {
        const Building& b = w_.buildings[i];
        if (!b.alive || !b.constructed || b.faction != o.faction || storageCapacity(b.type) <= 0) continue;
        if (b.flag < 0 || dist[b.flag] >= 1e8f) continue;
        const CarryType type = pickType([&](CarryType t) { return b.stock[t]; });
        if (b.stock[type] < o.amount) continue;
        const float d = distanceXZ(b.pos, at);
        if (d < bestD) {
            bestD = d;
            bestStore = static_cast<int>(i);
            bestType = type;
        }
    }

    // Flagi: tylko osiągalne drogami z flagi odbiorcy.
    for (size_t i = 0; i < w_.flags.size(); ++i) {
        const Flag& fl = w_.flags[i];
        if (!fl.alive || fl.faction != o.faction || static_cast<int>(i) == dst.flag || fl.goods.empty()) continue;
        auto freeOf = [&](CarryType t) {
            return static_cast<int>(std::count(fl.goods.begin(), fl.goods.end(), t)) - fl.reserved[static_cast<int>(t)];
        };
        const CarryType type = pickType(freeOf);
        if (freeOf(type) < o.amount) continue;
        const float d = distanceXZ(fl.pos, at) * 0.8f;
        if (d >= bestD || dist[i] >= 1e8f) continue;
        bestD = d;
        bestFlag = static_cast<int>(i);
        bestStore = -1;
        bestType = type;
    }

    if (bestFlag >= 0) {
        o.type = bestType;
        w_.flags[bestFlag].reserved[static_cast<int>(o.type)] += o.amount;
        o.sourceFlag = bestFlag;
        o.source = -1;
        o.funded = true;
        return true;
    }
    if (bestStore < 0) return false;
    o.type = bestType;   // od teraz konkretny towar (chleb / ryby / mięso)
    w_.buildings[bestStore].stock[o.type] -= o.amount;
    o.source = bestStore;
    o.sourceFlag = -1;
    o.funded = true;
    w_.routingDirty_ = true;
    return true;
}

void Transport::unfundOrder(TransportOrder& o) {
    if (o.funded && !o.picked) {
        if (o.source >= 0 && w_.buildings[o.source].alive) {
            resourceRef(w_.buildings[o.source].stock, o.type) += o.amount;
        } else if (o.sourceFlag >= 0 && w_.flags[o.sourceFlag].alive) {
            int& r = w_.flags[o.sourceFlag].reserved[static_cast<int>(o.type)];
            r = std::max(0, r - o.amount);
        }
    }
    o.funded = false;
    o.source = -1;
    o.sourceFlag = -1;
}

// Nabór tragarzy spośród wolnych osadników (najwyżej limit naraz) albo zwalnianie wolnych, gdy jest ich za dużo.
// Nowy tragarz idzie do flagi zamku i tam czeka na zlecenia.
void Transport::manageCarriers(int f, int limit) {
    const int target = std::min(limit, w_.carrierTargetOf(f));
    int count = 0;
    for (const auto& u : w_.units) {
        if (u.alive && u.faction == f && w_.isCarrierState(u.state)) ++count;
    }
    const int castle = w_.factions[f].castle;
    if (castle < 0 || !w_.buildings[castle].alive) return;
    const int home = w_.buildings[castle].flag;
    while (count < target) {
        const int s = w_.findIdleSettler(f, w_.flags[home].pos);
        if (s < 0) return;
        Unit& u = w_.units[s];
        u.state = UnitState::GoToRoad;
        u.atFlag = home;
        u.jobFlag = -1;
        u.jobOrder = -1;
        u.jobDest = -1;
        u.carryCount = 0;
        w_.setPath(u, w_.flags[home].pos);
        ++count;
    }
    // Za dużo tragarzy (gracz zmniejszył liczbę) - wolni wracają do zwykłych prac.
    if (limit >= 1000) {
        const int wanted = w_.carrierTargetOf(f);
        for (auto& u : w_.units) {
            if (count <= wanted) break;
            if (u.alive && u.faction == f && u.state == UnitState::CarrierWait) {
                w_.becomeIdleSettler(u);
                --count;
            }
        }
    }
}

// Przydział zleceń: każdy wolny tragarz (czekający przy fladze) bierze najbliższe po drogach zadanie:
//  - odbiór towaru z flagi warsztatu (cel: najbliższy magazyn z miejscem),
//  - dostawę opłaconego zlecenia (z magazynu do odbiorcy).
void Transport::assignTransportJobs(int f) {
    Resources bound = w_.storeBound(f);   // limity zapasu: magazyny + towary już niesione do magazynów
    bool anyStoreRoom = false;
    for (const auto& b : w_.buildings) {
        anyStoreRoom |= b.alive && b.constructed && b.faction == f && storageCapacity(b.type) > 0 &&
                        b.stock.total() + b.incoming < storageCapacity(b.type);
    }
    for (size_t ci = 0; ci < w_.units.size(); ++ci) {
        Unit& u = w_.units[ci];
        if (!u.alive || u.faction != f || u.state != UnitState::CarrierWait) continue;
        if (u.atFlag < 0 || !w_.flags[u.atFlag].alive) continue;
        const auto& dist = networkFrom(u.atFlag).dist;

        // Najlepszy odbiór: blisko, a przy dużej liczbie towarów na fladze - chętniej.
        int bestFlag = -1;
        float bestScore = 1e9f;
        for (size_t i = 0; i < w_.flags.size(); ++i) {
            const Flag& fl = w_.flags[i];
            if (!fl.alive || fl.faction != f || dist[i] >= 1e8f) continue;
            if (fl.building >= 0) {
                const Building& b = w_.buildings[fl.building];
                // Flaga magazynu z wolnym miejscem rozładowuje się sama.
                if (b.alive && b.constructed && storageCapacity(b.type) > 0 && b.stock.total() + b.incoming < storageCapacity(b.type)) continue;
            }
            const int avail = anyStoreRoom ? w_.pickupableGoods(fl, bound) : 0;
            if (avail <= 0) continue;
            const float score = dist[i] - static_cast<float>(avail) * 8.0f;
            if (score < bestScore) {
                bestScore = score;
                bestFlag = static_cast<int>(i);
            }
        }
        // Najlepsza dostawa: odległość do magazynu źródłowego.
        int bestOrder = -1;
        for (size_t i = 0; i < w_.orders.size(); ++i) {
            const TransportOrder& o = w_.orders[i];
            if (!o.alive || o.faction != f || !o.funded || o.carrier >= 0) continue;
            const int src = o.source >= 0 ? w_.buildings[o.source].flag : o.sourceFlag;
            const int dst = w_.buildingOrDead(o.dest).flag;
            if (src < 0 || dst < 0 || dist[src] >= 1e8f) continue;
            if (networkFrom(src).dist[dst] >= 1e8f) continue;   // odbiorca nie ma drogi do magazynu
            const float score = dist[src] - 20.0f;   // dostawy odrobinę ważniejsze (stoją place budowy)
            if (score < bestScore) {
                bestScore = score;
                bestOrder = static_cast<int>(i);
                bestFlag = -1;
            }
        }

        if (bestOrder >= 0) {
            TransportOrder& o = w_.orders[bestOrder];
            o.carrier = static_cast<int>(ci);
            u.jobOrder = bestOrder;
            u.jobFlag = -1;
            u.jobDest = o.dest;
            u.carry = CarryType::None;
            u.carryCount = 0;
            const int src = o.source >= 0 ? w_.buildings[o.source].flag : o.sourceFlag;
            networkPath(u.atFlag, src, u.path);
            u.pathIndex = 0;
            u.atFlag = src;
            u.state = UnitState::GoToRoad;
            continue;
        }
        if (bestFlag < 0) continue;

        // Magazyn docelowy dla odbioru: najbliższy po drogach z wolnym miejscem.
        Flag& fl = w_.flags[bestFlag];
        const auto& d2 = networkFrom(bestFlag).dist;
        int store = -1;
        float storeD = 1e9f;
        for (size_t i = 0; i < w_.buildings.size(); ++i) {
            const Building& b = w_.buildings[i];
            if (!b.alive || !b.constructed || b.faction != f || storageCapacity(b.type) <= 0 || b.flag < 0) continue;
            if (b.stock.total() + b.incoming >= storageCapacity(b.type) || d2[b.flag] >= 1e8f) continue;
            if (d2[b.flag] < storeD) {
                storeD = d2[b.flag];
                store = static_cast<int>(i);
            }
        }
        if (store < 0) continue;   // wszystkie magazyny pełne - towar poczeka
        // Rodzaj towaru: ten, którego najwięcej czeka (niezarezerwowany).
        CarryType type = CarryType::None;
        int bestAvail = 0;
        for (int t = 1; t <= kGoodTypeCount; ++t) {
            if (bound.v[t] >= w_.factions[f].stockLimit[t]) continue;   // limit zapasu osiągnięty - towar zostaje na fladze
            const int avail = static_cast<int>(std::count(fl.goods.begin(), fl.goods.end(), static_cast<CarryType>(t))) - fl.reserved[t];
            if (avail > bestAvail) {
                bestAvail = avail;
                type = static_cast<CarryType>(t);
            }
        }
        if (type == CarryType::None) continue;
        const int load = std::min({kCarrierLoad, bestAvail, storageCapacity(w_.buildings[store].type) - w_.buildings[store].stock.total() -
                                                               w_.buildings[store].incoming,
                                   w_.factions[f].stockLimit[static_cast<int>(type)] - bound[type]});
        resourceRef(bound, type) += load;
        fl.reserved[static_cast<int>(type)] += load;
        w_.buildings[store].incoming += load;
        u.jobFlag = bestFlag;
        u.jobOrder = -1;
        u.jobDest = store;
        u.carry = type;          // rodzaj, po który idzie (model postaci pokazuje go dopiero po podniesieniu)
        u.carryCount = load;
        networkPath(u.atFlag, bestFlag, u.path);
        u.pathIndex = 0;
        u.atFlag = bestFlag;
        u.state = UnitState::GoToRoad;
    }
}

// Tragarz przerywa zlecenie (śmierć, zwolnienie, zniknięcie drogi).
void Transport::releaseCarrierJob(Unit& u) {
    if (u.state == UnitState::GoToRoad && u.jobFlag >= 0) {
        // Szedł po towar: zwalnia rezerwację na fladze i miejsce w magazynie.
        if (w_.flags[u.jobFlag].alive && u.carry != CarryType::None) {
            int& r = w_.flags[u.jobFlag].reserved[static_cast<int>(u.carry)];
            r = std::max(0, r - u.carryCount);
        }
        if (u.jobDest >= 0) w_.buildings[u.jobDest].incoming = std::max(0, w_.buildings[u.jobDest].incoming - u.carryCount);
    } else if (u.state == UnitState::CarrierWalk) {
        // Niesie towar: zostawia go na najbliższej fladze; inny tragarz zaniesie go do magazynu.
        if (u.jobFlag >= 0 && u.jobDest >= 0) w_.buildings[u.jobDest].incoming = std::max(0, w_.buildings[u.jobDest].incoming - u.carryCount);
        int near = u.atFlag;
        float best = 1e9f;
        for (size_t i = 0; i < w_.flags.size(); ++i) {
            if (!w_.flags[i].alive || w_.flags[i].faction != u.faction) continue;
            const float d = distanceXZ(w_.flags[i].pos, u.pos);
            if (d < best) {
                best = d;
                near = static_cast<int>(i);
            }
        }
        dropCarriedGoods(u, near);
    }
    if (u.jobOrder >= 0 && u.jobOrder < static_cast<int>(w_.orders.size())) {
        TransportOrder& o = w_.orders[u.jobOrder];
        if (o.alive && o.carrier >= 0 && &w_.units[o.carrier] == &u) {
            o.carrier = -1;
            if (o.picked) {
                o.picked = false;   // towar leży na fladze - zlecenie trzeba opłacić od nowa
                o.funded = false;
                o.source = -1;
                o.sourceFlag = -1;
            }
        }
    }
    u.jobFlag = -1;
    u.jobOrder = -1;
    u.jobDest = -1;
    u.carry = CarryType::None;
    u.carryCount = 0;
}

void Transport::dropCarriedGoods(Unit& u, int flag) {
    if (u.carryCount > 0 && u.carry != CarryType::None && flag >= 0 && flag < static_cast<int>(w_.flags.size()) && w_.flags[flag].alive) {
        for (int k = 0; k < u.carryCount; ++k) w_.flags[flag].goods.push_back(u.carry);
    }
    u.carry = CarryType::None;
    u.carryCount = 0;
}

// Tragarz:
//   GoToRoad    - idzie bez ładunku: do flagi warsztatu (odbiór), do magazynu (dostawa) albo wraca do magazynu,
//   CarrierWalk - niesie towar do celu,
//   CarrierWait - czeka przy fladze na zlecenie (gdy stoi nie przy magazynie - po chwili wraca do najbliższego).
void Transport::updateCarrier(Unit& u, int id, float dt) {
    (void)id;
    u.ghostTimer = std::max(u.ghostTimer, 0.2f);   // tragarze przechodzą przez tłum (wąskie drogi)
    if (u.atFlag >= 0 && !w_.flags[u.atFlag].alive) u.atFlag = -1;
    switch (u.state) {
    case UnitState::CarrierWait: carrierWait(u, dt); return;
    case UnitState::GoToRoad: carrierGoToRoad(u, dt); return;
    case UnitState::CarrierWalk: carrierWalk(u, dt); return;
    default: finishCarrierJob(u); return;
    }
}

// Koniec zlecenia (wykonanego albo porzuconego): tragarz bez ładunku czeka przy fladze.
void Transport::finishCarrierJob(Unit& u) {
    u.jobFlag = -1;
    u.jobOrder = -1;
    u.jobDest = -1;
    u.carry = CarryType::None;
    u.carryCount = 0;
    u.state = UnitState::CarrierWait;
    u.timer = 0.0f;
    u.path.clear();
}

// Zdejmuje z flagi do maxCount sztuk towaru danego typu; zwraca, ile zdjął.
int Transport::takeGoodsFromFlag(Flag& flag, CarryType type, int maxCount) {
    int taken = 0;
    for (auto it = flag.goods.begin(); it != flag.goods.end() && taken < maxCount;) {
        if (*it == type) {
            it = flag.goods.erase(it);
            ++taken;
        } else {
            ++it;
        }
    }
    return taken;
}

// Czekanie: gdy tragarz nie stoi przy magazynie (albo jego flaga zniknęła), po 3 s wraca do najbliższego magazynu
// (najbliższego po drogach; bez połączenia - w linii prostej).
void Transport::carrierWait(Unit& u, float dt) {
    u.timer += dt;
    bool atStore = false;
    if (u.atFlag >= 0 && w_.flags[u.atFlag].building >= 0) {
        const Building& b = w_.buildings[w_.flags[u.atFlag].building];
        atStore = b.alive && b.constructed && storageCapacity(b.type) > 0;
    }
    if (atStore || u.timer <= 3.0f) return;

    int best = -1;
    float bestD = 1e9f;
    const auto& dist = networkFrom(u.atFlag).dist;   // atFlag = -1: wszystkie "bez połączenia"
    for (const auto& b : w_.buildings) {
        if (!b.alive || !b.constructed || b.faction != u.faction || storageCapacity(b.type) <= 0 || b.flag < 0) continue;
        const float d = u.atFlag >= 0 && dist[b.flag] < 1e8f ? dist[b.flag] : 5000.0f + distanceXZ(u.pos, w_.flags[b.flag].pos);
        if (d < bestD) {
            bestD = d;
            best = b.flag;
        }
    }
    if (best >= 0) {
        if (u.atFlag < 0 || !networkPath(u.atFlag, best, u.path)) w_.setPath(u, w_.flags[best].pos);
        u.pathIndex = 0;
        u.atFlag = best;
        u.state = UnitState::GoToRoad;
    }
    u.timer = 0.0f;
}

// Marsz bez ładunku. Na miejscu: odbiór z flagi warsztatu, odbiór dostawy z magazynu albo koniec powrotu.
void Transport::carrierGoToRoad(Unit& u, float dt) {
    if (!w_.pathFinished(u)) {
        w_.followPath(u, dt, 0.3f);
        return;
    }
    if (u.atFlag < 0) {
        finishCarrierJob(u);
        return;
    }
    if (distanceXZ(u.pos, w_.flags[u.atFlag].pos) > 2.5f) {
        u.stuckTimer += dt;
        if (u.stuckTimer > 6.0f) {   // nie da się dojść - porzuca zlecenie
            u.stuckTimer = 0.0f;
            releaseCarrierJob(u);
            finishCarrierJob(u);
            return;
        }
        w_.setPath(u, w_.flags[u.atFlag].pos);
        return;
    }
    u.stuckTimer = 0.0f;
    if (u.jobFlag >= 0) carrierPickUpFromFlag(u);
    else if (u.jobOrder >= 0) carrierPickUpOrder(u);
    else finishCarrierJob(u);   // powrót do magazynu zakończony
}

// Odbiór z flagi warsztatu: zabiera zarezerwowane towary i niesie je do magazynu jobDest.
void Transport::carrierPickUpFromFlag(Unit& u) {
    Flag& fl = w_.flags[u.jobFlag];
    int& reserved = fl.reserved[static_cast<int>(u.carry)];
    reserved = std::max(0, reserved - u.carryCount);
    const int taken = takeGoodsFromFlag(fl, u.carry, u.carryCount);

    Building& store = w_.buildingOrDead(u.jobDest);   // magazyn mógł zniknąć w trakcie marszu
    store.incoming = std::max(0, store.incoming - (u.carryCount - taken));
    u.carryCount = taken;
    if (taken == 0 || !store.alive || store.flag < 0 || !networkPath(u.jobFlag, store.flag, u.path)) {
        if (taken > 0) dropCarriedGoods(u, u.jobFlag);
        store.incoming = std::max(0, store.incoming - taken);
        finishCarrierJob(u);
        return;
    }
    u.pathIndex = 0;
    u.atFlag = store.flag;
    u.state = UnitState::CarrierWalk;
}

// Dostawa: zabiera opłacony towar (z magazynu albo zarezerwowany na fladze) i niesie go do odbiorcy.
void Transport::carrierPickUpOrder(Unit& u) {
    TransportOrder& o = w_.orders[u.jobOrder];
    if (!o.alive || !o.funded || o.dest < 0 || !w_.buildings[o.dest].alive || w_.buildings[o.dest].flag < 0 ||
        !networkPath(u.atFlag, w_.buildings[o.dest].flag, u.path)) {
        if (o.alive) o.carrier = -1;
        finishCarrierJob(u);
        return;
    }
    if (o.sourceFlag >= 0) {
        // Towar z flagi: zdejmujemy zarezerwowane sztuki.
        Flag& src = w_.flags[o.sourceFlag];
        int& reserved = src.reserved[static_cast<int>(o.type)];
        reserved = std::max(0, reserved - o.amount);
        const int taken = takeGoodsFromFlag(src, o.type, o.amount);
        if (taken < o.amount) {   // towaru zabrakło (nie powinno się zdarzyć) - odkładamy i opłacamy od nowa
            for (int k = 0; k < taken; ++k) src.goods.push_back(o.type);
            o.funded = false;
            o.sourceFlag = -1;
            o.carrier = -1;
            finishCarrierJob(u);
            return;
        }
        o.sourceFlag = -1;
    }
    o.picked = true;
    u.carry = o.type;
    u.carryCount = o.amount;
    u.pathIndex = 0;
    u.atFlag = w_.buildingOrDead(o.dest).flag;   // -1 = odbiorca zniknął (obsłużone w carrierWalk)
    u.state = UnitState::CarrierWalk;
}

// Marsz z ładunkiem. Na miejscu towar trafia do magazynu (nadmiar zostaje na jego fladze) albo do odbiorcy.
void Transport::carrierWalk(Unit& u, float dt) {
    if (!w_.pathFinished(u)) {
        w_.followPath(u, dt, 0.3f);
        return;
    }
    if (u.atFlag < 0) {   // flaga celu zniknęła po drodze
        releaseCarrierJob(u);
        finishCarrierJob(u);
        return;
    }
    if (u.jobFlag >= 0) {
        Building& store = w_.buildingOrDead(u.jobDest);   // zniszczony magazyn - towar zostaje na fladze
        store.incoming = std::max(0, store.incoming - u.carryCount);
        if (store.alive && store.constructed) {
            const int stored = std::max(0, std::min(u.carryCount, storageCapacity(store.type) - store.stock.total()));
            resourceRef(store.stock, u.carry) += stored;
            u.carryCount -= stored;
            w_.refreshFactionResources();
        }
        dropCarriedGoods(u, u.atFlag);
        if (u.faction == kPlayer) w_.emitSound("deliver", u.pos, 0.25f);
    } else if (u.jobOrder >= 0) {
        TransportOrder& o = w_.orders[u.jobOrder];
        Building* dest = (o.dest >= 0 && w_.buildings[o.dest].alive) ? &w_.buildings[o.dest] : nullptr;
        if (dest) {
            resourceRef(dest->delivered, u.carry) += u.carryCount;
            u.carryCount = 0;
        } else {
            dropCarriedGoods(u, u.atFlag);   // odbiorca zniknął - towar wróci do magazynu
        }
        w_.releaseOrder(o);
    }
    finishCarrierJob(u);
}

} // namespace settlers
