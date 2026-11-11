/**
 * world_settlers.cpp - zachowanie osadników.
 *
 * Osadnik to AUTOMAT STANÓW (pole Unit::state). W każdej klatce wykonuje się funkcja
 * odpowiadająca bieżącemu stanowi; funkcja może zmienić stan na kolejny.
 *
 *   BUDOWA / NAPRAWA:
 *     Idle --(dispatchJobs)--> GoBuild --(dotarł)--> Building --(gotowe)--> Idle
 *
 *   PRACA W WARSZTACIE (drwal, kamieniarz, farma, kopalnia):
 *     Idle --(dispatchJobs)--> GoToWork --(dotarł do drzwi)--> WorkWait
 *     WorkWait --> GoGather --(dotarł do drzewa/skały/pola)--> Gathering --> ReturnHome --> WorkWait ...
 *     W ReturnHome pracownik odkłada towary na fladze przed warsztatem. Gdy flaga jest pełna,
 *     czeka w WorkWait z towarem w rękach - produkcja stoi, dopóki tragarz nie odbierze towarów.
 *
 *   TRAGARZ (world_roads.cpp):
 *     Idle --(manageCarriers)--> GoToRoad (do flagi zamku) --> CarrierWait
 *     CarrierWait --(zlecenie)--> GoToRoad (po towar) --> CarrierWalk (z towarem do celu) --> CarrierWait
 *
 * Przydział prac (kto buduje, kto pracuje) robi World::dispatchJobs w world_update.cpp.
 * Ten sam stan GoBuild/Building służy też do naprawy ukończonego, uszkodzonego budynku.
 *
 * Reguła wspólna dla wszystkich stanów: jeśli budynek, do którego osadnik jest przypisany,
 * przestał istnieć (albo osadnik nie jest już jego pracownikiem) - osadnik wraca do Idle.
 */
#include "world_internal.h"

namespace settlers {

// =====================================================================================
// Rozdzielnik stanów
// =====================================================================================

void World::updateSettler(Unit& u, int id, float dt) {
    switch (u.state) {
    case UnitState::Idle:       settlerIdle(u, dt); break;
    case UnitState::GoBuild:    settlerGoBuild(u, dt); break;
    case UnitState::Building:   settlerBuild(u, dt); break;
    case UnitState::GoToWork:   settlerGoToWork(u, id, dt); break;
    case UnitState::WorkWait:   settlerWorkWait(u, id, dt); break;
    case UnitState::GoGather:   settlerGoGather(u, id, dt); break;
    case UnitState::Gathering:  settlerGather(u, id, dt); break;
    case UnitState::ReturnHome: settlerReturnHome(u, id, dt); break;
    case UnitState::GoToRoad:
    case UnitState::CarrierWait:
    case UnitState::CarrierWalk: transport_.updateCarrier(u, id, dt); break;
    default:                    u.state = UnitState::Idle; break;   // stany wojskowe nie dotyczą osadników
    }

    // Model postaci zależy od tego, co osadnik niesie (pusty / drewno / kamień / zboże).
    setUnitMesh(u, unitMeshName(u.type, u.faction, u.carry));
}

// =====================================================================================
// Funkcje pomocnicze
// =====================================================================================

Building* World::assignedBuilding(Unit& u) {
    if (u.building < 0 || u.building >= static_cast<int>(buildings.size())) return nullptr;
    Building& b = buildings[u.building];
    return b.alive ? &b : nullptr;
}

// Warsztat, w którym osadnik jest pracownikiem - albo nullptr, gdy przestał nim być.
Building* World::workplaceOf(Unit& u, int id) {
    Building* b = assignedBuilding(u);
    // Miejscem pracy jest tylko ukończony budynek, w którym ten osadnik jest pracownikiem.
    if (!b || !b->constructed || b->worker != id) return nullptr;
    return b;
}

// Czy jednostka nie ma już punktów ścieżki do przejścia.
bool World::pathFinished(const Unit& u) {
    return u.path.empty() || u.pathIndex >= u.path.size();
}

// Płynny obrót jednostki w stronę celu.
void World::faceTowards(Unit& u, const Vec3& target, float dt) {
    const float wanted = std::atan2(target.x - u.pos.x, target.z - u.pos.z);
    u.yaw = angleLerp(u.yaw, wanted, std::min(1.0f, dt * 6.0f));
}

// Dźwięk pracy (siekiera, kilof, młotek) w losowych odstępach.
void World::playWorkSound(Unit& u, const char* sound, const Vec3& where, float minGap, float maxGap, float volume, float dt) {
    u.sfxTimer -= dt;
    if (u.sfxTimer <= 0.0f) {
        u.sfxTimer = randf(minGap, maxGap);
        emitSound(sound, where, volume);
    }
}

// Odłożenie niesionego surowca na fladze warsztatu (kGoodsPerTrip towarów); pracownik odpoczywa
// i rusza na kolejny kurs. Gdy flaga jest pełna, czeka z towarem przy wejściu (WorkWait ponawia próbę).
void World::deliverCarriedResource(Unit& u, Building& b) {
    if (!depositAtWorkshopFlag(u, b)) {
        u.state = UnitState::WorkWait;
        u.timer = 1.0f;
        u.path.clear();
        return;
    }
    b.produced++;
    u.carry = CarryType::None;
    u.state = UnitState::WorkWait;
    u.timer = 1.5f;   // krótki odpoczynek przed kolejnym kursem
    u.path.clear();
}

// =====================================================================================
// Stany
// =====================================================================================

// Bez zadania: osadnik spaceruje powoli w pobliżu bramy zamku i czeka na przydział.
void World::settlerIdle(Unit& u, float dt) {
    u.timer -= dt;
    if (u.timer <= 0.0f) {
        u.timer = randf(4.0f, 9.0f);
        Vec3 center = u.pos;
        const int castle = factions[u.faction].castle;
        if (castle >= 0 && buildings[castle].alive) center = buildings[castle].door();
        const float angle = randf(0.0f, 2.0f * PI);
        const float radius = randf(4.0f, 28.0f);
        setPath(u, {center.x + std::cos(angle) * radius, 0.0f, center.z + std::sin(angle) * radius});
    }
    followPath(u, dt * 0.45f, 0.5f);   // spacer: ok. połowa normalnej szybkości
}

// Marsz na plac budowy (albo do budynku wymagającego naprawy).
void World::settlerGoBuild(Unit& u, float dt) {
    Building* b = assignedBuilding(u);
    // Rezygnacja: budynek zniknął, albo to naprawa, która nie jest już potrzebna / możliwa.
    // (Rozpoczęta naprawa trwa do pełnych PŻ, choć zlecana jest dopiero poniżej 97%.)
    if (!b || (b->constructed && (b->hp >= buildingDef(b->type).maxHp || b->repairBlocked))) {
        becomeIdleSettler(u);
        return;
    }

    // Czy dotarł? Budynek blokuje całe komórki nawigacji (do kNavCell poza obrysem), więc ścieżka
    // może się skończyć dalej niż 3.2 od ściany - koniec ścieżki w pobliżu budynku też się liczy.
    const float distance = distanceToBuilding(u.pos, *b);
    const bool pathDone = !u.path.empty() && u.pathIndex >= u.path.size();
    if (distance < 3.2f || (pathDone && distance < kNavCell * 2.0f + 1.5f)) {
        u.stuckTimer = 0.0f;
        b->accessFailures = 0;
        b->unreachable = false;
        u.state = UnitState::Building;
        u.path.clear();
        return;
    }

    if (pathFinished(u)) {
        // Ścieżka skończyła się daleko od budynku - po 4 s rezygnujemy i zgłaszamy brak dojścia.
        u.stuckTimer += dt;
        if (u.stuckTimer > 4.0f) {
            const int site = u.building;
            becomeIdleSettler(u);
            reportAccessFailure(site);
            return;
        }
        // Celem jest punkt przy ścianie budynku od strony osadnika.
        Vec3 dir = subtract(u.pos, b->pos);
        dir.y = 0.0f;
        dir = normalize(dir);
        if (dot(dir, dir) < 0.5f) dir = {0.0f, 0.0f, 1.0f};
        setPath(u, add(b->pos, multiply(dir, buildingDef(b->type).halfSize + 1.5f)));
    }
    followPath(u, dt, 0.3f);
}

// Praca na budowie: podnosi postęp budowy albo - dla ukończonego budynku - naprawia go.
void World::settlerBuild(Unit& u, float dt) {
    Building* b = assignedBuilding(u);
    if (!b || (b->constructed && b->hp >= buildingDef(b->type).maxHp)) {
        becomeIdleSettler(u);
        return;
    }
    if (b->constructed) {
        settlerRepair(u, *b, dt);
        return;
    }

    // Budowa: jeden osadnik buduje w czasie buildTime, dwóch - dwa razy szybciej.
    // Postęp nie może wyprzedzić dowiezionych materiałów (np. połowa materiałów = najwyżej 50%).
    const BuildingDef& def = buildingDef(b->type);
    const float materials = def.cost.total() > 0 ? static_cast<float>(b->delivered.total()) / def.cost.total() : 1.0f;
    if (b->progress >= materials) {
        faceTowards(u, b->pos, dt);   // czeka na tragarzy z materiałami
        return;
    }
    const float progressStep = std::min(dt / def.buildTime, materials - b->progress);
    b->progress = std::min(1.0f, b->progress + progressStep);
    b->hp = std::min(def.maxHp, b->hp + def.maxHp * 0.9f * progressStep);   // budynek "rośnie" razem z PŻ
    playWorkSound(u, "hammer", u.pos, 0.65f, 0.95f, 0.55f, dt);
    faceTowards(u, b->pos, dt);

    if (b->progress >= 1.0f) {
        completeBuilding(u.building);
        becomeIdleSettler(u);
    }
}

// Naprawa: 1 drewno przywraca 10% PŻ, tempo to połowa szybkości budowy.
void World::settlerRepair(Unit& u, Building& b, float dt) {
    const BuildingDef& def = buildingDef(b.type);

    // Drewno jest pobierane "z góry" za kolejne 10% (repairCredit = PŻ już opłacone).
    if (b.repairCredit <= 0.0f) {
        FactionState& fs = factions[b.faction];
        if (fs.res[CarryType::Planks] < 1) {
            b.repairBlocked = true;
            if (b.faction == kPlayer && gameTime - b.repairMsgTime > 30.0f) {
                b.repairMsgTime = gameTime;
                pushMessage(std::string("Naprawa wstrzymana (") + def.name + ") - brak desek", {1.0f, 0.75f, 0.4f, 1.0f});
            }
            becomeIdleSettler(u);
            return;
        }
        spend(b.faction, Resources{{CarryType::Planks, 1}});
        b.repairCredit += def.maxHp * 0.1f;
    }

    const float heal = std::min(b.repairCredit, def.maxHp / (def.buildTime * 2.0f) * dt);
    b.hp = std::min(def.maxHp, b.hp + heal);
    b.repairCredit -= heal;
    playWorkSound(u, "hammer", u.pos, 0.75f, 1.05f, 0.5f, dt);
    faceTowards(u, b.pos, dt);

    if (b.hp >= def.maxHp) {
        if (b.faction == kPlayer) pushMessage(std::string("Naprawiono: ") + def.name, {0.6f, 1.0f, 0.6f, 1.0f});
        becomeIdleSettler(u);
    }
}

// Marsz do drzwi warsztatu, w którym osadnik będzie pracownikiem.
void World::settlerGoToWork(Unit& u, int id, float dt) {
    Building* b = workplaceOf(u, id);
    if (!b) {
        becomeIdleSettler(u);
        return;
    }
    const Vec3 door = b->door();
    if (distanceXZ(u.pos, door) < 2.5f) {
        u.stuckTimer = 0.0f;
        b->accessFailures = 0;
        b->unreachable = false;
        u.state = UnitState::WorkWait;
        u.timer = 1.0f;
        u.path.clear();
        return;
    }
    if (pathFinished(u)) {
        u.stuckTimer += dt;
        if (u.stuckTimer > 4.0f) {
            const int site = u.building;
            becomeIdleSettler(u);
            reportAccessFailure(site);
            return;
        }
        setPath(u, door);
    }
    followPath(u, dt, 0.3f);
}

// Pracownik czeka przy warsztacie, a po odliczeniu wybiera kolejny cel pracy.
void World::settlerWorkWait(Unit& u, int id, float dt) {
    Building* b = workplaceOf(u, id);
    if (!b) {
        becomeIdleSettler(u);
        return;
    }
    u.timer -= dt;
    if (u.timer > 0.0f) return;

    // Towar w rękach: flaga była pełna - ponawiamy odłożenie (bez tego nie ma kolejnego kursu).
    if (u.carry != CarryType::None) {
        deliverCarriedResource(u, *b);
        return;
    }

    const BuildingDef& def = buildingDef(b->type);
    const ProductionDef& prod = productionDef(b->type);
    switch (prod.kind) {
    case WorkKind::Gather: {
        // Drwal / kamieniarz: najbliższe wolne drzewo / skała w zasięgu pracy.
        const NodeKind kind = prod.output == CarryType::Wood ? NodeKind::Tree : NodeKind::Rock;
        checkWorkResources(*b);   // aktualizuje ostrzeżenia "kończą się / brak surowca"
        const int node = findNearestNode(kind, b->pos, def.workRadius);
        if (node < 0) {
            u.timer = 6.0f;       // nic wolnego - spróbujemy później
            return;
        }
        nodes[node].reserved++;  // rezerwacja, żeby dwóch drwali nie szło do tego samego drzewa
        u.resourceNode = node;
        u.state = UnitState::GoGather;
        u.path.clear();
        return;
    }
    case WorkKind::Mine:
    case WorkKind::Process:
        // Górnik / przetwórnia: praca we wnętrzu budynku, o ile tragarze dowieźli towary wejściowe
        // (kopalnia - żywność, tartak - kłody, huta - ruda i węgiel...).
        if (!consumeInputs(*b)) {
            u.timer = 2.0f;   // czeka na dostawę
            return;
        }
        u.state = UnitState::Gathering;
        u.timer = prod.workTime;
        u.path.clear();
        return;
    case WorkKind::Forester:
    case WorkKind::Fisher:
    case WorkKind::Hunter: {
        // Leśniczy: miejsce na sadzonkę; rybak: brzeg wody; myśliwy: miejsce w okolicy.
        Vec3 spot;
        const bool found = prod.kind == WorkKind::Forester ? findPlantingSpot(*b, spot)
                         : prod.kind == WorkKind::Fisher   ? findFishingSpot(*b, spot)
                                                            : findHuntingSpot(*b, spot);
        if (!found) {
            u.timer = 8.0f;   // brak miejsca - spróbujemy później
            return;
        }
        u.workSpot = spot;
        u.state = UnitState::GoGather;
        u.resourceNode = -1;
        u.path.clear();
        setPath(u, spot);
        return;
    }
    case WorkKind::Farm:
        // Rolnik: losowe miejsce na polu (pole jest częścią modelu farmy, przed zagrodą).
        u.workSpot = add(b->pos, rotateY({randf(-9.0f, 9.0f), 0.0f, randf(-1.0f, 9.5f)}, b->rotY));
        u.state = UnitState::GoGather;
        u.resourceNode = -1;
        u.path.clear();
        setPath(u, u.workSpot);
        return;
    case WorkKind::None:
        u.timer = 5.0f;
        return;
    }
}

// Zużywa towary wejściowe na jeden cykl pracy (z dostaw przywiezionych przez tragarzy).
// false = czegoś brakuje (praca czeka).
bool World::consumeInputs(Building& b) {
    const ProductionDef& prod = productionDef(b.type);
    Resources need;
    for (CarryType in : prod.inputs) {
        if (in != CarryType::None) need[in] += inputPerCycle(in);
    }
    if (need.total() == 0) return true;
    if (!b.delivered.canAfford(need)) {
        b.inputStarved = true;
        return false;
    }
    b.delivered.subtract(need);
    b.inputStarved = false;
    return true;
}

// Marsz do drzewa / skały / miejsca na polu / miejsca pracy (sadzenie, łowienie, polowanie).
void World::settlerGoGather(Unit& u, int id, float dt) {
    Building* b = workplaceOf(u, id);
    if (!b) {
        becomeIdleSettler(u);
        return;
    }
    const ProductionDef& prod = productionDef(b->type);

    if (prod.kind == WorkKind::Forester || prod.kind == WorkKind::Fisher || prod.kind == WorkKind::Hunter) {
        // Marsz na miejsce pracy (zwykła nawigacja). Gdy nie da się dojść - wracamy po nowe miejsce.
        const float distance = distanceXZ(u.pos, u.workSpot);
        if (distance < 2.0f || (!u.path.empty() && u.pathIndex >= u.path.size() && distance < 6.0f)) {
            u.state = UnitState::Gathering;
            u.timer = prod.workTime;
            u.path.clear();
            return;
        }
        if (pathFinished(u)) {
            u.stuckTimer += dt;
            if (u.stuckTimer > 2.0f) {
                u.stuckTimer = 0.0f;
                u.state = UnitState::WorkWait;
                u.timer = 2.0f;
                return;
            }
            setPath(u, u.workSpot);
        }
        followPath(u, dt, 0.5f);
        return;
    }

    if (prod.kind == WorkKind::Farm) {
        // Pole leży wewnątrz obrysu farmy (komórki zablokowane), więc idziemy prosto, bez nawigacji.
        if (distanceXZ(u.pos, u.workSpot) < 0.8f) {
            u.state = UnitState::Gathering;
            u.timer = prod.workTime;
            return;
        }
        u.path = {u.workSpot};
        u.pathIndex = 0;
        followPath(u, dt, 0.5f);
        return;
    }

    // Surowiec zniknął (ścięty przez kogoś innego) - wracamy do wyboru celu.
    if (u.resourceNode < 0 || !nodes[u.resourceNode].alive || nodes[u.resourceNode].amount <= 0) {
        if (u.resourceNode >= 0) nodes[u.resourceNode].reserved = std::max(0, nodes[u.resourceNode].reserved - 1);
        u.resourceNode = -1;
        u.state = UnitState::WorkWait;
        u.timer = 0.5f;
        return;
    }

    const ResourceNode& n = nodes[u.resourceNode];
    const float distance = distanceXZ(u.pos, n.pos);
    const bool pathDone = !u.path.empty() && u.pathIndex >= u.path.size();
    // Skała blokuje siatkę nawigacji, więc ścieżka kończy się na najbliższej wolnej komórce,
    // która może leżeć dalej niż promień skały - koniec ścieżki w pobliżu też oznacza dotarcie.
    if (distance < n.radius + 3.0f || (pathDone && distance < n.radius + 3.0f + kNavCell * 2.0f)) {
        u.state = UnitState::Gathering;
        u.timer = prod.workTime;   // czas ścinania / kucia
        u.stuckTimer = 0.0f;
        u.path.clear();
        return;
    }

    if (pathDone) {
        // Koniec ścieżki daleko od surowca - surowiec nieosiągalny, po 2 s szukamy innego.
        u.stuckTimer += dt;
        if (u.stuckTimer > 2.0f) {
            markNodeUnreachable(u);
            u.stuckTimer = 0.0f;
            u.state = UnitState::WorkWait;
            u.timer = 3.0f;
            return;
        }
    }
    if (pathFinished(u)) {
        setPath(u, n.pos);
        if (u.path.empty()) {
            markNodeUnreachable(u);
            u.state = UnitState::WorkWait;
            u.timer = 3.0f;
            return;
        }
    }
    followPath(u, dt, n.radius + 2.0f);
}

// Praca: ścinanie / kucie / pole / sadzenie / łowienie / polowanie albo praca we wnętrzu (kopalnia,
// przetwórnia). Po skończeniu osadnik niesie towar wyjściowy do flagi warsztatu.
void World::settlerGather(Unit& u, int id, float dt) {
    Building* b = workplaceOf(u, id);
    if (!b) {
        becomeIdleSettler(u);
        return;
    }
    u.timer -= dt;
    const ProductionDef& prod = productionDef(b->type);
    auto finishWithOutput = [&]() {
        u.carry = prod.output;
        u.state = UnitState::ReturnHome;
        u.path.clear();
    };

    switch (prod.kind) {
    case WorkKind::Farm:
    case WorkKind::Hunter:
        if (u.timer <= 0.0f) finishWithOutput();
        return;
    case WorkKind::Fisher:
        faceTowards(u, u.workSpot, dt);
        if (u.timer <= 0.0f) finishWithOutput();
        return;
    case WorkKind::Mine:
    case WorkKind::Process:
        // Praca we wnętrzu (model ukryty w syncVisuals), słychać tylko narzędzia.
        playWorkSound(u, prod.kind == WorkKind::Mine ? "mine" : "hammer", b->pos, 1.0f, 1.4f, 0.45f, dt);
        if (u.timer <= 0.0f) finishWithOutput();   // wychodzi przed wejście i oddaje wyrób
        return;
    case WorkKind::Forester:
        faceTowards(u, u.workSpot, dt);
        if (u.timer <= 0.0f) {
            // Posadzona sadzonka to drzewo z growth ~0 - rośnie w updateNodes, drwal zetnie je po wyrośnięciu.
            const int tree = addNode(NodeKind::Tree, u.workSpot, static_cast<int>(randf(0.0f, 3.99f)), randf(0.85f, 1.2f));
            nodes[tree].growth = 0.02f;
            nodes[tree].entity->transform().scale = multiply(Vec3{1, 1, 1}, nodes[tree].baseScale * 0.15f);
            u.carry = CarryType::None;
            u.state = UnitState::ReturnHome;   // powrót do chaty (deliverCarriedResource liczy posadzone drzewa)
            u.path.clear();
        }
        return;
    case WorkKind::Gather:
    case WorkKind::None:
        break;
    }

    if (u.resourceNode < 0 || !nodes[u.resourceNode].alive) {
        u.state = UnitState::WorkWait;
        u.timer = 0.5f;
        u.resourceNode = -1;
        return;
    }

    ResourceNode& n = nodes[u.resourceNode];
    const bool tree = n.kind == NodeKind::Tree;
    faceTowards(u, n.pos, dt);
    n.shake = 0.4f;   // drzewo drży od uderzeń siekiery
    playWorkSound(u, tree ? "chop" : "mine", n.pos, tree ? 0.8f : 0.9f, tree ? 1.0f : 1.1f, 0.6f, dt);
    if (u.timer > 0.0f) return;

    // Gotowe: zabieramy jedną porcję surowca.
    n.amount--;
    n.reserved = std::max(0, n.reserved - 1);
    if (tree && n.amount <= 0) {
        n.fallTimer = 0.0f;   // ostatnia porcja - drzewo się przewraca (World::updateNodes)
        emitSound("hit_building", n.pos, 0.7f);
    }
    u.resourceNode = -1;
    finishWithOutput();
}

// Powrót z surowcem: do drzwi warsztatu (rolnik - do zagrody na tyłach pola).
void World::settlerReturnHome(Unit& u, int id, float dt) {
    Building* b = workplaceOf(u, id);
    if (!b) {
        becomeIdleSettler(u);
        return;
    }
    const bool farm = productionDef(b->type).kind == WorkKind::Farm;
    const Vec3 door = b->door();
    const Vec3 target = farm ? add(b->pos, rotateY({0.0f, 0.0f, -2.5f}, b->rotY)) : door;

    if (distanceXZ(u.pos, target) < 2.5f) {
        if (u.faction == kPlayer) emitSound("deliver", u.pos, 0.3f);
        deliverCarriedResource(u, *b);
        return;
    }

    if (farm) {
        // Zagroda jest wewnątrz obrysu farmy - idziemy prosto.
        u.path = {target};
        u.pathIndex = 0;
    } else if (pathFinished(u)) {
        setPath(u, door);
    }
    followPath(u, dt, 0.3f);
}

} // namespace settlers
