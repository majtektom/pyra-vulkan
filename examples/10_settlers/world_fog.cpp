/**
 * world_fog.cpp - mgła wojny i zwiad (widoczność każdej frakcji).
 *
 * Mapa jest podzielona na siatkę kFowSize x kFowSize komórek (4 x 4 jednostki). Dla KAŻDEJ frakcji pamiętamy:
 *   - widoczne teraz (visible): w zasięgu wzroku jej jednostek i budynków,
 *   - odkryte (explored): kiedykolwiek widoczne - zostaje na zawsze (i trafia do zapisu gry).
 * Gracz: z jego mapy powstaje tekstura dla renderera (0 nieodkryte, 128 odkryte, 255 widoczne) - shader deferred
 * przyciemnia teren, roślinność i budynki (engine: Renderer::setFogOfWar / updateFogOfWar), minimapa tak samo.
 * AI: jego mapa to wiedza komputera - zna tylko odkryte surowce, góry i budynki gracza (aiKnows w world_ai.cpp),
 * więc musi wysyłać zwiadowców i rozbudowywać się w stronę nieznanego terenu.
 *
 * Co widać z wroga: jego jednostki - tylko w zasięgu wzroku; budynki, flagi, drogi i granice - na odkrytym terenie
 * (pamiętamy, gdzie stały, jak w klasycznych RTS-ach).
 */
#include "world_internal.h"

namespace settlers {

namespace {

constexpr float kFowCell = 2.0f * kWorldHalf / static_cast<float>(World::kFowSize);

// Zasięg wzroku: zwiadowca-łucznik widzi najdalej, budynki wojskowe - dalej niż zasięg terytorium.
float unitSight(UnitType t) {
    switch (t) {
    case UnitType::Archer: return 80.0f;
    case UnitType::Swordsman: return 65.0f;
    default: return 45.0f;
    }
}
float buildingSight(const Building& b) {
    const BuildingDef& def = buildingDef(b.type);
    if (!b.constructed) return def.halfSize + 20.0f;
    if (def.territoryRadius > 0.0f) return def.territoryRadius + 25.0f;   // zamek, wieża
    return def.halfSize + 35.0f;
}

} // namespace

int World::fowCellOf(float x, float z) const {
    const int cx = static_cast<int>((x + kWorldHalf) / kFowCell);
    const int cz = static_cast<int>((z + kWorldHalf) / kFowCell);
    if (cx < 0 || cz < 0 || cx >= kFowSize || cz >= kFowSize) return -1;
    return cz * kFowSize + cx;
}

bool World::visibleToPlayer(const Vec3& p) const {
    return !fogOfWar || visibleTo(kPlayer, p);
}

bool World::exploredByPlayer(const Vec3& p) const {
    return !fogOfWar || exploredBy(kPlayer, p);
}

bool World::exploredBy(int faction, const Vec3& p) const {
    const FactionFog& fog = fog_[static_cast<size_t>(faction)];
    const int c = fowCellOf(p.x, p.z);
    return c >= 0 && !fog.explored.empty() && fog.explored[static_cast<size_t>(c)] != 0;
}

bool World::visibleTo(int faction, const Vec3& p) const {
    const FactionFog& fog = fog_[static_cast<size_t>(faction)];
    const int c = fowCellOf(p.x, p.z);
    return c >= 0 && !fog.visible.empty() && fog.visible[static_cast<size_t>(c)] != 0;
}

float World::exploredFraction(int faction) const {
    return static_cast<float>(fog_[static_cast<size_t>(faction)].exploredCells) / static_cast<float>(kFowSize * kFowSize);
}

void World::resetFog(FactionFog& fog) {
    const size_t cells = static_cast<size_t>(kFowSize) * kFowSize;
    fog.explored.assign(cells, 0);
    fog.visible.assign(cells, 0);
    fog.count.assign(cells, 0);
    fog.unitObs.clear();
    fog.buildingObs.clear();
    fog.dirtyX0 = fog.dirtyZ0 = 0;
    fog.dirtyX1 = fog.dirtyZ1 = kFowSize - 1;
    fog.newlyExplored = 0;
    fog.exploredCells = 0;
}

// Dodaje (delta = +1) albo zdejmuje (delta = -1) koło widzenia jednego obserwatora: licznik obserwatorów w każdej
// komórce koła. Komórka jest widoczna, gdy licznik > 0. Prostokąt zmian trafia do dirty* (przeliczenie tekstury).
void World::stampSight(FactionFog& fog, int cx, int cz, int r, int delta) {
    const int r2 = r * r;
    const int z0 = std::max(0, cz - r), z1 = std::min(kFowSize - 1, cz + r);
    const int x0 = std::max(0, cx - r), x1 = std::min(kFowSize - 1, cx + r);
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - cx, dz = z - cz;
            if (dx * dx + dz * dz > r2) continue;
            const size_t i = static_cast<size_t>(z) * kFowSize + x;
            fog.count[i] = static_cast<uint16_t>(static_cast<int>(fog.count[i]) + delta);
            fog.visible[i] = fog.count[i] > 0 ? 1 : 0;
            if (delta > 0 && !fog.explored[i]) {
                fog.explored[i] = 1;
                ++fog.newlyExplored;
                ++fog.exploredCells;
            }
        }
    }
    fog.dirtyX0 = std::min(fog.dirtyX0, x0);
    fog.dirtyZ0 = std::min(fog.dirtyZ0, z0);
    fog.dirtyX1 = std::max(fog.dirtyX1, x1);
    fog.dirtyZ1 = std::max(fog.dirtyZ1, z1);
}

// Obserwator (jednostka / budynek frakcji) patrzy teraz z komórki (cx, cz) w promieniu r - jeśli to zmiana,
// zdejmujemy stare koło i stawiamy nowe. Nieaktywny obserwator: r = 0.
void World::moveObserver(FactionFog& fog, FowObserver& o, int cx, int cz, int r) {
    if (o.r == r && o.cx == cx && o.cz == cz) return;
    if (o.r > 0) stampSight(fog, o.cx, o.cz, o.r, -1);
    o.cx = cx;
    o.cz = cz;
    o.r = r;
    if (o.r > 0) stampSight(fog, o.cx, o.cz, o.r, +1);
}

// Przyrostowo: przeliczane są tylko jednostki i budynki frakcji, które zmieniły komórkę albo zasięg.
// Obserwator jest indeksowany slotem - gdy slot zajmie obiekt innej frakcji, stare koło zostaje zdjęte.
void World::updateFactionFog(int f) {
    FactionFog& fog = fog_[static_cast<size_t>(f)];
    auto cellX = [](float x) { return static_cast<int>((x + kWorldHalf) / kFowCell); };
    auto cells = [](float radius) { return static_cast<int>(radius / kFowCell) + 1; };
    fog.unitObs.resize(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        const bool sees = u.alive && u.faction == f && u.state != UnitState::Garrison;
        moveObserver(fog, fog.unitObs[i], cellX(u.pos.x), cellX(u.pos.z), sees ? cells(unitSight(u.type)) : 0);
    }
    fog.buildingObs.resize(buildings.size());
    for (size_t i = 0; i < buildings.size(); ++i) {
        const Building& b = buildings[i];
        const bool sees = b.alive && b.faction == f;
        moveObserver(fog, fog.buildingObs[i], cellX(b.pos.x), cellX(b.pos.z), sees ? cells(buildingSight(b)) : 0);
    }
}

// Kilka razy na sekundę (czas rzeczywisty): mapy widoczności obu frakcji, potem tekstura gracza - tylko
// w prostokącie zmian (wcześniej co odświeżenie cała siatka 65 tys. komórek była liczona od nowa).
void World::updateFogOfWar(float dt) {
    const size_t cellsTotal = static_cast<size_t>(kFowSize) * kFowSize;
    if (fowTexture_.size() != cellsTotal) {
        fowTexture_.assign(cellsTotal, 0);
        for (auto& fog : fog_) resetFog(fog);
        fowTimer_ = 0.0f;
    }
    fowTimer_ -= dt;
    if (fowTimer_ > 0.0f) return;
    fowTimer_ = 0.15f;   // czas rzeczywisty (present) - kilka razy na sekundę niezależnie od prędkości gry

    for (int f = 0; f < kFactionCount; ++f) updateFactionFog(f);
    FactionFog& pf = fog_[kPlayer];
    for (int f = 0; f < kFactionCount; ++f) {
        if (f == kPlayer) continue;   // prostokąt zmian AI nie jest potrzebny (nie ma tekstury)
        FactionFog& fog = fog_[static_cast<size_t>(f)];
        fog.dirtyX0 = fog.dirtyZ0 = kFowSize;
        fog.dirtyX1 = fog.dirtyZ1 = -1;
    }
    if (!fogOfWar || pf.dirtyX0 > pf.dirtyX1) return;   // tekstura gracza bez zmian (albo bez mgły)

    // Tekstura: 0 / 128 / 255 rozmyte filtrem 3x3 (miękka krawędź mgły) - tylko w prostokącie zmian (+1 na rozmycie).
    auto raw = [&](int x, int z) {
        x = std::clamp(x, 0, kFowSize - 1);
        z = std::clamp(z, 0, kFowSize - 1);
        const size_t i = static_cast<size_t>(z) * kFowSize + x;
        return pf.visible[i] ? 255 : (pf.explored[i] ? 128 : 0);
    };
    bool changed = false;
    const int x0 = std::max(0, pf.dirtyX0 - 1), x1 = std::min(kFowSize - 1, pf.dirtyX1 + 1);
    const int z0 = std::max(0, pf.dirtyZ0 - 1), z1 = std::min(kFowSize - 1, pf.dirtyZ1 + 1);
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            int sum = 0;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) sum += raw(x + dx, z + dz);
            }
            const auto v = static_cast<uint8_t>(sum / 9);
            uint8_t& t = fowTexture_[static_cast<size_t>(z) * kFowSize + x];
            if (t != v) {
                t = v;
                changed = true;
            }
        }
    }
    pf.dirtyX0 = pf.dirtyZ0 = kFowSize;
    pf.dirtyX1 = pf.dirtyZ1 = -1;
    if (changed) ++fowVersion_;
    // Odkryto spory kawałek mapy - słupki granic wroga do przebudowy (pokazujemy je tylko na odkrytym terenie).
    if (pf.newlyExplored > 150) {
        pf.newlyExplored = 0;
        territoryMeshDirty_ = true;   // tylko grafika - skutki dla gry się nie zmieniają
    }
}

// Zapis odkrytego terenu: kodowanie długości serii (RLE) - "0:12000,1:340,..." zamiast 65 tys. zer i jedynek.
std::string World::fogExploredRle(int faction) const {
    const auto& explored = fog_[static_cast<size_t>(faction)].explored;
    std::string out;
    size_t i = 0;
    while (i < explored.size()) {
        size_t j = i;
        while (j < explored.size() && explored[j] == explored[i]) ++j;
        out += std::to_string(explored[i]) + ":" + std::to_string(j - i) + ",";
        i = j;
    }
    return out;
}

void World::setFogExploredRle(int faction, const std::string& rle) {
    const size_t cells = static_cast<size_t>(kFowSize) * kFowSize;
    if (fowTexture_.size() != cells) {
        fowTexture_.assign(cells, 0);
        for (auto& fog : fog_) resetFog(fog);
    }
    FactionFog& fog = fog_[static_cast<size_t>(faction)];
    resetFog(fog);
    size_t pos = 0, i = 0;
    while (pos < rle.size() && i < cells) {
        const size_t colon = rle.find(':', pos), comma = rle.find(',', pos);
        if (colon == std::string::npos || comma == std::string::npos) break;
        const int v = std::stoi(rle.substr(pos, colon - pos));
        const size_t n = std::stoul(rle.substr(colon + 1, comma - colon - 1));
        for (size_t k = 0; k < n && i < cells; ++k) {
            fog.explored[i++] = static_cast<uint8_t>(v);
            fog.exploredCells += v ? 1 : 0;
        }
        pos = comma + 1;
    }
    fowTimer_ = 0.0f;   // widoczność przeliczy się w najbliższym kroku (cała tekstura - prostokąt zmian = mapa)
    territoryMeshDirty_ = true;
}

} // namespace settlers
