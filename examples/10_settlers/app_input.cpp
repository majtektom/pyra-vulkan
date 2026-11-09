/**
 * app_input.cpp - sterowanie gracza.
 *
 * Kamera (klawisze, krawędź ekranu, mysz), zaznaczanie jednostek (klik, ramka),
 * rozkazy dla wojska (PPM) oraz stawianie budynków z podglądem.
 * Wskazywanie obiektów w świecie: promień z kamery przez kursor (screenPointToRay)
 * i test przecięcia z terenem / jednostkami / budynkami (World::pick...).
 */
#include "settlers_app.h"

namespace settlers {

// Kamera: przesuwanie (klawisze lub kursor przy krawędzi ekranu), obrót (Q/E lub środkowy
// przycisk), zoom (kółko, +/-), Spacja - powrót nad zamek. Prędkość przesuwu rośnie z oddaleniem.
void SettlersApp::updateCameraInput(float dt, Engine& e, const InputSnapshot& in) {
    const float speed = cam_.distance() * 1.3f * dt;
    float right = 0.0f, forward = 0.0f;
    if (e.actionDown(kActPanLeft)) right -= speed;
    if (e.actionDown(kActPanRight)) right += speed;
    if (e.actionDown(kActPanUp)) forward += speed;
    if (e.actionDown(kActPanDown)) forward -= speed;

    // Przewijanie przy krawędzi ekranu - dopiero po pierwszym ruchu myszy
    // (przed nim SDL raportuje pozycję 0,0, co przesuwałoby kamerę od startu).
    if (in.mouseDeltaX != 0 || in.mouseDeltaY != 0) mouseMoved_ = true;
    if (mouseMoved_ && autoplaySeconds_ <= 0.0f) {
        const float margin = 4.0f;
        if (in.mouseX <= margin) right -= speed;
        if (in.mouseX >= screenW_ - margin - 1) right += speed;
        if (in.mouseY <= margin) forward += speed;
        if (in.mouseY >= screenH_ - margin - 1) forward -= speed;
    }
    if (right != 0.0f || forward != 0.0f) cam_.pan(right, forward);

    if (e.actionDown(kActRotL)) cam_.rotate(90.0f * dt);
    if (e.actionDown(kActRotR)) cam_.rotate(-90.0f * dt);
    if (e.mouseDown(2)) {
        cam_.rotate(-static_cast<float>(in.mouseDeltaX) * 0.3f);
        cam_.tilt(static_cast<float>(in.mouseDeltaY) * 0.2f);
    }
    if (in.mouseWheelF != 0.0f && !gui_->isPointerOverGui()) {
        cam_.zoom(in.mouseWheelF);
    }
    // Zoom z klawiatury (+/-, PageUp/PageDown) - przydatne np. na touchpadzie
    if (e.actionDown(kActZoomIn)) cam_.zoom(4.0f * dt);
    if (e.actionDown(kActZoomOut)) cam_.zoom(-4.0f * dt);
    if (e.actionPressed(kActHome)) {
        const int castle = world_->factions[kPlayer].castle;
        if (castle >= 0 && world_->buildings[castle].alive) cam_.moveTo(world_->buildings[castle].pos);
    }
}

// Położenie i rozmiar minimapy na ekranie (prawy dolny róg).
SettlersApp::MinimapRect SettlersApp::minimapRect() const {
    const float size = 200.0f;
    return {screenW_ - size - 14.0f, screenH_ - size - 14.0f, size};
}

// Czy kursor jest nad minimapą; jeśli tak, przelicza go na punkt świata.
bool SettlersApp::mouseInMinimap(int mx, int my, Vec3* worldPos) const {
    const MinimapRect r = minimapRect();
    const float fx = (static_cast<float>(mx) - r.x) / r.size;
    const float fy = (static_cast<float>(my) - r.y) / r.size;
    if (fx < 0.0f || fy < 0.0f || fx > 1.0f || fy > 1.0f) return false;
    if (worldPos) {
        *worldPos = {-kWorldHalf + fx * kWorldHalf * 2.0f, 0.0f, -kWorldHalf + fy * kWorldHalf * 2.0f};
    }
    return true;
}

// Czy kursor jest nad elementem interfejsu - wtedy kliknięcie nie trafia w świat gry.
bool SettlersApp::mouseOverUi(int mx, int my) const {
    return gui_->isPointerOverGui() || mouseInMinimap(mx, my) || static_cast<float>(my) < kTopBarH || mouseOverBuildBar(mx, my) ||
           (hasSelectionPanel() && mx > selPanelX_ && mx < selPanelX_ + selPanelW_ && my > selPanelY_ &&
            my < selPanelY_ + selPanelH_);
}

// Promień od kamery przez piksel pod kursorem - podstawa wskazywania obiektów myszą.
Ray SettlersApp::mouseRay(const InputSnapshot& in) const {
    return screenPointToRay(*camera_, static_cast<float>(in.mouseX), static_cast<float>(in.mouseY), screenW_, screenH_);
}

// Obsługa wejścia w trakcie gry: skróty klawiszowe, tryb budowy, minimapa,
// zaznaczanie (klik lub ramka przy przeciąganiu LPM) i rozkazy (PPM).
void SettlersApp::handleGameInput(Engine& e, const InputSnapshot& in) {
    const bool shift = in.keys[SDL_SCANCODE_LSHIFT] || in.keys[SDL_SCANCODE_RSHIFT];
    const bool ctrl = in.keys[SDL_SCANCODE_LCTRL] || in.keys[SDL_SCANCODE_RCTRL];
    const bool overUi = mouseOverUi(in.mouseX, in.mouseY);

    // Skróty budowy
    for (int i = 0; i < kBuildMenuSize; ++i) {
        if (e.actionPressed("build." + std::to_string(i + 1))) beginPlacement(kBuildMenu[i]);
    }
    if (e.actionPressed(kActDemolish)) demolishSelected();
    if (e.actionPressed(kActHalt)) haltSelected();
    if (e.actionPressed(kActSelectArmy)) selectAllArmy();
    if (e.actionPressed(kActRoad)) {
        if (roadMode_) {
            cancelRoad();
        } else if (selectedFlag_ >= 0) {
            beginRoad(selectedFlag_);
        } else if (selectedBuilding_ >= 0 && world_->buildings[selectedBuilding_].faction == kPlayer) {
            beginRoad(world_->buildings[selectedBuilding_].flag);
        } else {
            beginRoad();
        }
    }

    // Dolny pasek budowy (kliknięcie w pasek nie trafia w świat).
    if (handleBuildBar(e, in)) return;

    if (placing_) {
        updatePlacement(e, in, overUi, shift);
        return;
    }
    if (roadMode_) {
        updateRoadMode(e, in, overUi, shift);
        return;
    }

    // Minimapa
    Vec3 mmWorld;
    if (mouseInMinimap(in.mouseX, in.mouseY, &mmWorld)) {
        if (e.mouseDown(0) && !dragging_) cam_.moveTo(mmWorld);
        if (e.mousePressed(1)) issueMoveOrder(mmWorld, ctrl);
    }

    // Zaznaczanie (LPM)
    if (e.mousePressed(0) && !overUi) {
        dragging_ = true;
        dragStartX_ = in.mouseX;
        dragStartY_ = in.mouseY;
    }
    if (dragging_ && e.mouseReleased(0)) {
        dragging_ = false;
        const int dx = std::abs(in.mouseX - dragStartX_), dy = std::abs(in.mouseY - dragStartY_);
        if (dx > 6 || dy > 6) {
            boxSelect(dragStartX_, dragStartY_, in.mouseX, in.mouseY, shift);
        } else {
            clickSelect(in, shift);
        }
    }
    if (!e.mouseDown(0) && dragging_) dragging_ = false;

    // Rozkazy (PPM)
    if (e.mousePressed(1) && !overUi) {
        issueOrderAtCursor(in, ctrl);
    }
}

// Kliknięcie LPM: zaznacza jednostkę albo budynek pod kursorem (Shift dodaje/usuwa z zaznaczenia).
void SettlersApp::clickSelect(const InputSnapshot& in, bool additive) {
    const Ray ray = mouseRay(in);
    const int u = world_->pickUnit(ray);
    if (u >= 0) {
        if (!additive) clearSelection();
        const Unit& unit = world_->units[u];
        if (unit.faction == kPlayer && additive) {
            // Shift: przełączanie zaznaczenia
            auto it = std::find(selectedUnits_.begin(), selectedUnits_.end(), u);
            if (it != selectedUnits_.end()) {
                world_->setSelected(u, false);
                selectedUnits_.erase(it);
                return;
            }
        }
        if (unit.faction != kPlayer) clearSelection();
        if (unit.faction == kPlayer) sfx("select", 0.5f);
        selectedUnits_.push_back(u);
        world_->setSelected(u, true);
        return;
    }
    const int b = world_->pickBuilding(ray);
    clearSelection();
    // Flaga gracza w pobliżu klikniętego punktu terenu (gdy nie trafiliśmy w budynek).
    Vec3 hit;
    if (b < 0 && world_->pickGround(ray, hit)) {
        const int fl = world_->flagNear(kPlayer, hit, 3.5f);
        if (fl >= 0) {
            selectedFlag_ = fl;
            world_->setFlagSelected(fl, true);
            sfx("select", 0.4f);
            return;
        }
    }
    if (b >= 0) {
        selectedBuilding_ = b;
        world_->setBuildingSelected(b, true);
    }
}

// Ramka zaznaczenia: wybiera jednostki gracza, których pozycja na ekranie mieści się w ramce.
// Jeśli w ramce są żołnierze, osadnicy są pomijani (wojsko ma pierwszeństwo).
void SettlersApp::boxSelect(int x0, int y0, int x1, int y1, bool additive) {
    if (!additive) clearSelection();
    const float minX = static_cast<float>(std::min(x0, x1)), maxX = static_cast<float>(std::max(x0, x1));
    const float minY = static_cast<float>(std::min(y0, y1)), maxY = static_cast<float>(std::max(y0, y1));
    std::vector<int> soldiers, settlersSel;
    for (size_t i = 0; i < world_->units.size(); ++i) {
        const Unit& u = world_->units[i];
        if (!u.alive || u.faction != kPlayer) continue;
        float sx, sy;
        if (!worldToScreen(*camera_, add(u.pos, {0.0f, 3.0f, 0.0f}), screenW_, screenH_, sx, sy)) continue;
        if (sx < minX || sx > maxX || sy < minY || sy > maxY) continue;
        (u.type == UnitType::Settler ? settlersSel : soldiers).push_back(static_cast<int>(i));
    }
    // Wojsko ma pierwszeństwo - osadników zaznaczamy tylko, gdy w ramce nie ma żołnierzy
    const auto& pick = soldiers.empty() ? settlersSel : soldiers;
    if (selectedBuilding_ >= 0) {
        world_->setBuildingSelected(selectedBuilding_, false);
        selectedBuilding_ = -1;
    }
    for (int id : pick) {
        if (std::find(selectedUnits_.begin(), selectedUnits_.end(), id) != selectedUnits_.end()) continue;
        selectedUnits_.push_back(id);
        world_->setSelected(id, true);
    }
}

// Tab: zaznacza wszystkich żołnierzy gracza.
void SettlersApp::selectAllArmy() {
    clearSelection();
    for (size_t i = 0; i < world_->units.size(); ++i) {
        const Unit& u = world_->units[i];
        if (u.alive && u.faction == kPlayer && u.type != UnitType::Settler) {
            selectedUnits_.push_back(static_cast<int>(i));
            world_->setSelected(static_cast<int>(i), true);
        }
    }
}

// Zaznaczeni żołnierze gracza - tylko oni przyjmują rozkazy (osadnicy pracują samodzielnie).
std::vector<int> SettlersApp::selectedSoldiers() const {
    std::vector<int> out;
    if (spectator_) return out;   // w pokazie AI gracz tylko ogląda
    for (int id : selectedUnits_) {
        const Unit& u = world_->units[id];
        if (u.alive && u.faction == kPlayer && u.type != UnitType::Settler) out.push_back(id);
    }
    return out;
}

// PPM: wróg pod kursorem = rozkaz ataku, w przeciwnym razie marsz w wskazane miejsce.
void SettlersApp::issueOrderAtCursor(const InputSnapshot& in, bool attackMove) {
    const auto soldiers = selectedSoldiers();
    if (soldiers.empty()) return;
    const Ray ray = mouseRay(in);
    const int u = world_->pickUnit(ray);
    if (u >= 0 && world_->units[u].faction != kPlayer) {
        world_->commandAttackUnit(soldiers, u);
        world_->spawnMarker(world_->units[u].pos);
        return;
    }
    const int b = world_->pickBuilding(ray);
    if (b >= 0 && world_->buildings[b].faction != kPlayer) {
        world_->commandAttackBuilding(soldiers, b);
        world_->spawnMarker(world_->buildings[b].pos);
        return;
    }
    Vec3 hit;
    if (world_->pickGround(ray, hit)) issueMoveOrder(hit, attackMove);
}

// Marsz zaznaczonych żołnierzy w szyku; Ctrl = marsz bojowy.
void SettlersApp::issueMoveOrder(const Vec3& p, bool attackMove) {
    const auto soldiers = selectedSoldiers();
    if (soldiers.empty()) return;
    world_->commandMove(soldiers, p, attackMove);
    world_->spawnMarker(p);
    sfx("select", 0.45f);
}

// H: zaznaczeni żołnierze przerywają rozkaz i stają na posterunku tam, gdzie są.
void SettlersApp::haltSelected() {
    for (int id : selectedSoldiers()) {
        Unit& u = world_->units[id];
        u.state = UnitState::Idle;
        u.path.clear();
        u.targetUnit = -1;
        u.targetBuilding = -1;
        u.guardPos = u.pos;
    }
}

// Odznacza wszystko (także pierścienie zaznaczenia w świecie).
void SettlersApp::clearSelection() {
    if (world_) {
        for (int id : selectedUnits_) world_->setSelected(id, false);
        if (selectedBuilding_ >= 0) world_->setBuildingSelected(selectedBuilding_, false);
        if (selectedFlag_ >= 0) world_->setFlagSelected(selectedFlag_, false);
    }
    selectedUnits_.clear();
    selectedBuilding_ = -1;
    selectedFlag_ = -1;
}

// Usuwa z zaznaczenia obiekty, które zginęły lub zostały zniszczone.
void SettlersApp::cleanupSelection() {
    selectedUnits_.erase(std::remove_if(selectedUnits_.begin(), selectedUnits_.end(),
                                        [this](int id) { return !world_->units[id].alive; }),
                         selectedUnits_.end());
    if (selectedBuilding_ >= 0 && !world_->buildings[selectedBuilding_].alive) selectedBuilding_ = -1;
    if (selectedFlag_ >= 0 && !world_->flags[selectedFlag_].alive) selectedFlag_ = -1;
}

// Wejście w tryb stawiania budynku (przycisk lub klawisz 1-8), o ile stać nas na budynek.
void SettlersApp::beginPlacement(BuildingType type) {
    if (mode_ != Mode::Game || spectator_) return;
    cancelRoad();
    const BuildingDef& def = buildingDef(type);
    if (!world_->factions[kPlayer].res.canAfford(def.cost)) {
        world_->pushMessage(std::string(def.name) + ": brakuje " + missingText(def.cost, world_->factions[kPlayer].res),
                            {1.0f, 0.6f, 0.4f, 1.0f});
        sfx("ui_error", 0.7f);
        return;
    }
    placing_ = true;
    placeType_ = type;
    dragging_ = false;
    auto* mc = ghost_->getComponent<MeshComponent>();
    mc->meshName = buildingMeshName(type, kPlayer);
    ghost_->setActive(true);
}

// Wyjście z trybu stawiania budynku.
void SettlersApp::cancelPlacement() {
    placing_ = false;
    if (ghost_) ghost_->setActive(false);
}

// Zmienia materiał podglądu (zielony/czerwony). Po zmianie trzeba wyczyścić materialInstance,
// żeby system renderowania utworzył go na nowo z nowego materiału.
void SettlersApp::setGhostMaterial(bool ok) {
    auto* mc = ghost_->getComponent<MeshComponent>();
    const MaterialHandle wanted = ok ? materials_.ghostOk : materials_.ghostBad;
    if (mc->materialHandle.value != wanted.value) {
        mc->materialHandle = wanted;
        mc->materialInstance.reset();  // wymusza ponowne rozwiązanie materiału
    }
}

// Tryb stawiania budynku: podgląd podąża za kursorem (wyrównany do siatki), kolor mówi,
// czy można budować; LPM stawia plac budowy, PPM/ESC anuluje, R obraca, Shift - kilka z rzędu.
void SettlersApp::updatePlacement(Engine& e, const InputSnapshot& in, bool overUi, bool shift) {
    if (e.mousePressed(1) || e.actionPressed(kActEsc)) {
        cancelPlacement();
        return;
    }
    if (e.actionPressed(kActRotateBuilding)) placeRot_ += PI * 0.5f;

    Vec3 hit;
    if (!world_->pickGround(mouseRay(in), hit)) {
        ghost_->setActive(false);
        return;
    }
    ghost_->setActive(true);
    const Vec3 p = world_->snapToGrid(hit);
    std::string reason;
    const BuildingDef& def = buildingDef(placeType_);
    bool ok = world_->canPlace(placeType_, p, kPlayer, &reason, placeRot_);
    const bool affordable = world_->factions[kPlayer].res.canAfford(def.cost);
    if (ok && !affordable) {
        ok = false;
        reason = "Brakuje: " + missingText(def.cost, world_->factions[kPlayer].res);
    }
    placeReason_ = ok ? "" : reason;
    setGhostMaterial(ok);
    float y = 0.0f;
    for (int i = 0; i < 5; ++i) {
        const float h = def.halfSize;
        const float ox[] = {0, -h, h, -h, h}, oz[] = {0, -h, -h, h, h};
        y += world_->heightAt(p.x + ox[i], p.z + oz[i]);
    }
    ghost_->transform().position = {p.x, y / 5.0f, p.z};
    ghost_->transform().rotation.y = placeRot_;
    ghostPos_ = p;

    if (e.mousePressed(0) && !overUi) {
        if (ok) {
            world_->placeBuilding(placeType_, p, kPlayer, false, placeRot_);
            sfx("place", 0.9f);
            world_->pushMessage(std::string("Plac budowy: ") + def.name + " - osadnicy ruszają do pracy",
                                {0.85f, 0.95f, 1.0f, 1.0f});
            if (!shift || !world_->factions[kPlayer].res.canAfford(def.cost)) cancelPlacement();
        } else {
            world_->pushMessage("Nie można tu budować: " + reason, {1.0f, 0.55f, 0.45f, 1.0f});
            sfx("ui_error", 0.7f);
        }
    }
}

// Przycisk w oknie koszar: kolejkuje szkolenie żołnierza.
void SettlersApp::trainSelected(UnitType type) {
    if (selectedBuilding_ < 0 || spectator_) return;
    std::string reason;
    if (!world_->queueTraining(selectedBuilding_, type, &reason)) {
        world_->pushMessage("Nie można szkolić: " + reason, {1.0f, 0.6f, 0.4f, 1.0f});
        sfx("ui_error", 0.7f);
    }
}

// Del / przycisk: burzy zaznaczony budynek gracza (zamku nie można zburzyć)
// albo usuwa zaznaczoną flagę razem z dochodzącymi drogami.
void SettlersApp::demolishSelected() {
    if (spectator_) return;
    if (selectedFlag_ >= 0) {
        const int fl = selectedFlag_;
        const bool buildingFlag = world_->flags[fl].building >= 0;
        world_->removeFlag(fl);
        world_->pushMessage(buildingFlag ? "Usunięto drogi od flagi budynku" : "Usunięto flagę i jej drogi",
                            {0.9f, 0.85f, 0.7f, 1.0f});
        clearSelection();
        return;
    }
    if (selectedBuilding_ < 0) return;
    const Building& b = world_->buildings[selectedBuilding_];
    if (!b.alive || b.faction != kPlayer || b.type == BuildingType::Castle) return;
    world_->demolish(selectedBuilding_);
    selectedBuilding_ = -1;
}

// Okna zależne od zaznaczenia (koszary, akcje budynku) i kolory przycisków budowy
// (czerwone = brak surowców, z podpowiedzią czego brakuje po najechaniu).
void SettlersApp::updateContextWindows() {
    if (mode_ != Mode::Game) return;
    hoverMissing_.clear();
    bool barracks = false, actions = false;
    if (selectedBuilding_ >= 0) {
        const Building& b = world_->buildings[selectedBuilding_];
        if (b.alive && b.faction == kPlayer) {
            barracks = b.type == BuildingType::Barracks && b.constructed;
            actions = true;
        }
    }
    barracksWindow_->setVisible(barracks && !spectator_);
    // Okienko liczby tragarzy - przy dolnej krawędzi panelu gospodarki.
    const bool econ = showEconomy_ && !spectator_;
    carriersWindow_->setVisible(econ);
    if (econ) {
        float ex, ey, ew, eh;
        economyPanelRect(ex, ey, ew, eh);
        carriersWindow_->setPosition(static_cast<int>(ex + ew - 250.0f), static_cast<int>(ey + eh - 50.0f));
        btnCarrierAuto_->setText(world_->factions[kPlayer].carrierAuto ? "Auto: tak" : "Auto: nie");
    }
    actionsWindow_->setVisible(actions && !spectator_);
    // Pasek budowy: przycisk pod kursorem i brakujące surowce (pokazywane na karcie informacji).
    const auto& in = engine_.input();
    refreshBuildBar();
    buildBar_.update(in.mouseX, in.mouseY, false);
    const int hover = buildBar_.hoveredId();
    if (hover >= 0 && hover < kBuildMenuSize) {
        const BuildingDef& def = buildingDef(kBuildMenu[hover]);
        const auto& res = world_->factions[kPlayer].res;
        if (!res.canAfford(def.cost)) hoverMissing_ = std::string(def.name) + " - brakuje: " + missingText(def.cost, res);
    }
}

// =====================================================================================
// Budowa dróg
// =====================================================================================

// Wejście w tryb budowy drogi. fromFlag >= 0 - droga zaczyna się od tej flagi, inaczej pierwszy
// klik wskazuje flagę lub budynek początkowy.
void SettlersApp::beginRoad(int fromFlag) {
    if (mode_ != Mode::Game || spectator_) return;
    cancelPlacement();
    roadMode_ = true;
    roadStart_ = (fromFlag >= 0 && world_->flags[fromFlag].alive && world_->flags[fromFlag].faction == kPlayer) ? fromFlag : -1;
    roadPreview_.clear();
    roadPreviewTarget_ = {1e9f, 0.0f, 1e9f};
    dragging_ = false;
}

void SettlersApp::cancelRoad() {
    roadMode_ = false;
    roadStart_ = -1;
    roadPreview_.clear();
}

// Flaga w pobliżu punktu terenu pod kursorem, a gdy jej nie ma - flaga budynku gracza pod kursorem.
// Budynek liczy się tylko wtedy, gdy punkt terenu leży przy nim (promień z ukośnej kamery może
// zahaczyć o dach budynku stojącego przed wskazanym miejscem).
int SettlersApp::flagUnderCursor(const InputSnapshot& in, const Vec3& hit) const {
    const int near = world_->flagNear(kPlayer, hit, 5.0f);
    if (near >= 0) return near;
    const int b = world_->pickBuilding(mouseRay(in));
    if (b < 0) return -1;
    const Building& bd = world_->buildings[b];
    const float reach = buildingDef(bd.type).halfSize * 1.8f + 4.0f;
    if (bd.faction != kPlayer || bd.flag < 0 || std::hypot(hit.x - bd.pos.x, hit.z - bd.pos.z) > reach) return -1;
    return bd.flag;
}

// Tryb drogi. Pierwszy LPM: flaga/budynek początkowy. Potem podgląd trasy podąża za kursorem,
// a LPM buduje drogę do wskazanego punktu (lub flagi/budynku). Gdy droga kończy się na nowej
// fladze, można od razu prowadzić ją dalej; dojście do istniejącej flagi kończy budowę
// (z Shift - budujemy dalej). PPM / ESC / T - koniec.
void SettlersApp::updateRoadMode(Engine& e, const InputSnapshot& in, bool overUi, bool shift) {
    if (e.mousePressed(1) || e.actionPressed(kActEsc)) {
        cancelRoad();
        return;
    }
    Vec3 hit;
    if (!world_->pickGround(mouseRay(in), hit)) return;

    if (roadStart_ < 0) {
        roadReason_ = "Wskaż flagę lub budynek, od którego zacząć drogę";
        if (e.mousePressed(0) && !overUi) {
            const int fl = flagUnderCursor(in, hit);
            if (fl >= 0) {
                roadStart_ = fl;
                sfx("select", 0.5f);
            } else {
                world_->pushMessage("Droga musi zaczynać się od flagi albo budynku", {1.0f, 0.6f, 0.45f, 1.0f});
                sfx("ui_error", 0.6f);
            }
        }
        return;
    }
    if (!world_->flags[roadStart_].alive) {
        cancelRoad();
        return;
    }

    // Cel: flaga pod kursorem (także flaga wskazanego budynku) albo punkt terenu.
    const int targetFlag = flagUnderCursor(in, hit);
    const Vec3 target = targetFlag >= 0 ? world_->flags[targetFlag].pos : hit;
    if (std::hypot(target.x - roadPreviewTarget_.x, target.z - roadPreviewTarget_.z) > 1.0f) {
        roadPreviewTarget_ = target;
        roadReason_.clear();
        roadPreviewOk_ = world_->planRoad(kPlayer, roadStart_, target, roadPreview_, &roadReason_);
        if (!roadPreviewOk_ && roadPreview_.empty()) roadPreview_ = {world_->flags[roadStart_].pos, target};
    }

    if (e.mousePressed(0) && !overUi) {
        std::string reason;
        const int end = world_->buildRoad(kPlayer, roadStart_, target, &reason);
        if (end < 0) {
            world_->pushMessage("Nie można zbudować drogi: " + reason, {1.0f, 0.55f, 0.45f, 1.0f});
            sfx("ui_error", 0.7f);
            return;
        }
        sfx("place", 0.8f);
        roadPreview_.clear();
        roadPreviewTarget_ = {1e9f, 0.0f, 1e9f};
        if (targetFlag >= 0 && !shift) {
            cancelRoad();   // dołączyliśmy do istniejącej flagi - droga gotowa
        } else {
            roadStart_ = end;   // nowa flaga - prowadzimy drogę dalej
        }
    }
}

// Przycisk "Połącz drogą": AI-owy autoConnectFlag dla zaznaczonego budynku gracza.
void SettlersApp::connectSelected() {
    if (selectedBuilding_ < 0 || spectator_) return;
    const Building& b = world_->buildings[selectedBuilding_];
    if (!b.alive || b.faction != kPlayer || b.flag < 0) return;
    if (world_->buildingLinked(b)) {
        world_->pushMessage("Ten budynek jest już połączony drogą", {0.8f, 0.9f, 1.0f, 1.0f});
        return;
    }
    std::string reason;
    if (world_->autoConnectFlag(b.flag, &reason)) {
        world_->pushMessage("Zbudowano drogę do sieci magazynu", {0.6f, 1.0f, 0.6f, 1.0f});
        sfx("place", 0.8f);
    } else {
        world_->pushMessage("Nie udało się połączyć: " + reason + " - poprowadź drogę ręcznie (T)", {1.0f, 0.6f, 0.45f, 1.0f});
        sfx("ui_error", 0.7f);
    }
}

// Przycisk -/+ w okienku "Tragarze": ręczna liczba tragarzy (wyłącza tryb Auto).
void SettlersApp::changeCarriers(int delta) {
    if (spectator_) return;
    FactionState& fs = world_->factions[kPlayer];
    if (fs.carrierAuto) {
        fs.carrierTarget = world_->carrierTargetOf(kPlayer);   // start od bieżącej wartości automatu
        fs.carrierAuto = false;
    }
    fs.carrierTarget = std::clamp(fs.carrierTarget + delta, 0, 60);
}

void SettlersApp::toggleCarrierAuto() {
    if (spectator_) return;
    FactionState& fs = world_->factions[kPlayer];
    if (!fs.carrierAuto) {
        fs.carrierAuto = true;
    } else {
        fs.carrierTarget = world_->carrierTargetOf(kPlayer);
        fs.carrierAuto = false;
    }
}

} // namespace settlers
