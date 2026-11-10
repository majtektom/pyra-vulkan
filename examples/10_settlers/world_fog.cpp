/**
 * world_fog.cpp - mgła wojny i zwiad (widoczność gracza).
 *
 * Mapa jest podzielona na siatkę kFowSize x kFowSize komórek (4 x 4 jednostki). Dla gracza pamiętamy:
 *   - widoczne teraz (fowVisible_): w zasięgu wzroku jego jednostek i budynków, przeliczane 4 razy na sekundę,
 *   - odkryte (fowExplored_): kiedykolwiek widoczne - zostaje na zawsze (i trafia do zapisu gry).
 * Z obu powstaje tekstura dla renderera (0 nieodkryte, 128 odkryte, 255 widoczne) - shader deferred przyciemnia
 * teren, roślinność i budynki (engine: Renderer::setFogOfWar / updateFogOfWar), a minimapa tak samo.
 *
 * Co widać z wroga: jego jednostki - tylko w zasięgu wzroku; budynki, flagi, drogi i granice - na odkrytym terenie
 * (pamiętamy, gdzie stały, jak w klasycznych RTS-ach). AI nie korzysta z mgły (zna mapę) - prostsze AI.
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
    if (!fogOfWar) return true;
    const int c = fowCellOf(p.x, p.z);
    return c >= 0 && !fowVisible_.empty() && fowVisible_[c] != 0;
}

bool World::exploredByPlayer(const Vec3& p) const {
    if (!fogOfWar) return true;
    const int c = fowCellOf(p.x, p.z);
    return c >= 0 && !fowExplored_.empty() && fowExplored_[c] != 0;
}

void World::revealCircle(const Vec3& center, float radius) {
    const int r = static_cast<int>(radius / kFowCell) + 1;
    const int cx = static_cast<int>((center.x + kWorldHalf) / kFowCell);
    const int cz = static_cast<int>((center.z + kWorldHalf) / kFowCell);
    const float r2 = (radius / kFowCell) * (radius / kFowCell);
    for (int z = std::max(0, cz - r); z <= std::min(kFowSize - 1, cz + r); ++z) {
        for (int x = std::max(0, cx - r); x <= std::min(kFowSize - 1, cx + r); ++x) {
            const float dx = static_cast<float>(x - cx), dz = static_cast<float>(z - cz);
            if (dx * dx + dz * dz > r2) continue;
            const size_t i = static_cast<size_t>(z) * kFowSize + x;
            fowVisible_[i] = 1;
            if (!fowExplored_[i]) {
                fowExplored_[i] = 1;
                ++fowNewlyExplored_;
            }
        }
    }
}

// 4 razy na sekundę: widoczność z jednostek i budynków gracza, tekstura dla renderera.
void World::updateFogOfWar(float dt) {
    const size_t cells = static_cast<size_t>(kFowSize) * kFowSize;
    if (fowExplored_.size() != cells) {
        fowExplored_.assign(cells, 0);
        fowVisible_.assign(cells, 0);
        fowTexture_.assign(cells, 0);
        fowTimer_ = 0.0f;
    }
    if (!fogOfWar) return;
    fowTimer_ -= dt;
    if (fowTimer_ > 0.0f) return;
    fowTimer_ = 0.25f;

    std::fill(fowVisible_.begin(), fowVisible_.end(), 0);
    for (const auto& u : units) {
        if (u.alive && u.faction == kPlayer && u.state != UnitState::Garrison) revealCircle(u.pos, unitSight(u.type));
    }
    for (const auto& b : buildings) {
        if (b.alive && b.faction == kPlayer) revealCircle(b.pos, buildingSight(b));
    }
    // Tekstura: 0 / 128 / 255 rozmyte filtrem 3x3 - krawędź mgły jest miękka zamiast schodkowej (komórka = 4 j.).
    bool changed = false;
    auto raw = [&](int x, int z) {
        x = std::clamp(x, 0, kFowSize - 1);
        z = std::clamp(z, 0, kFowSize - 1);
        const size_t i = static_cast<size_t>(z) * kFowSize + x;
        return fowVisible_[i] ? 255 : (fowExplored_[i] ? 128 : 0);
    };
    for (int z = 0; z < kFowSize; ++z) {
        for (int x = 0; x < kFowSize; ++x) {
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
    if (changed) ++fowVersion_;
    // Odkryto spory kawałek mapy - granice wroga trzeba narysować na nowo (słupki tylko na odkrytym terenie).
    if (fowNewlyExplored_ > 150) {
        fowNewlyExplored_ = 0;
        territoryDirty_ = true;
    }
}

// Zapis odkrytego terenu: kodowanie długości serii (RLE) - "0:12000,1:340,..." zamiast 65 tys. zer i jedynek.
std::string World::fogExploredRle() const {
    std::string out;
    size_t i = 0;
    while (i < fowExplored_.size()) {
        size_t j = i;
        while (j < fowExplored_.size() && fowExplored_[j] == fowExplored_[i]) ++j;
        out += std::to_string(fowExplored_[i]) + ":" + std::to_string(j - i) + ",";
        i = j;
    }
    return out;
}

void World::setFogExploredRle(const std::string& rle) {
    const size_t cells = static_cast<size_t>(kFowSize) * kFowSize;
    fowExplored_.assign(cells, 0);
    fowVisible_.assign(cells, 0);
    fowTexture_.assign(cells, 0);
    size_t pos = 0, i = 0;
    while (pos < rle.size() && i < cells) {
        const size_t colon = rle.find(':', pos), comma = rle.find(',', pos);
        if (colon == std::string::npos || comma == std::string::npos) break;
        const int v = std::stoi(rle.substr(pos, colon - pos));
        const size_t n = std::stoul(rle.substr(colon + 1, comma - colon - 1));
        for (size_t k = 0; k < n && i < cells; ++k) fowExplored_[i++] = static_cast<uint8_t>(v);
        pos = comma + 1;
    }
    fowTimer_ = 0.0f;   // widoczność przeliczy się w najbliższym kroku
    territoryDirty_ = true;
}

} // namespace settlers
