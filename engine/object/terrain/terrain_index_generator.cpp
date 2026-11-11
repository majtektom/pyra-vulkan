#include "object/terrain/terrain_index_generator.h"

#include <algorithm>

namespace engine3d {

namespace {

// Które krawędzie zszywamy w danej konfiguracji (numeracja jak w nagłówku).
struct StitchEdges {
    bool left, right, bottom, top;
};

constexpr StitchEdges kStitchTable[16] = {
    {false, false, false, false},   //  0 - brak
    {true,  false, false, false},   //  1 - lewa
    {false, true,  false, false},   //  2 - prawa
    {false, false, true,  false},   //  3 - dolna
    {false, false, false, true },   //  4 - górna
    {true,  true,  false, false},   //  5 - lewa + prawa
    {false, false, true,  true },   //  6 - dolna + górna
    {true,  false, true,  false},   //  7 - lewa + dolna
    {false, true,  true,  false},   //  8 - dolna + prawa
    {false, true,  false, true },   //  9 - prawa + górna
    {true,  false, false, true },   // 10 - górna + lewa
    {true,  true,  true,  false},   // 11 - lewa + dolna + prawa
    {false, true,  true,  true },   // 12 - dolna + prawa + górna
    {true,  true,  false, true },   // 13 - prawa + górna + lewa
    {true,  false, true,  true },   // 14 - górna + lewa + dolna
    {true,  true,  true,  true },   // 15 - wszystkie
};

} // namespace

TerrainIndexGenerator::TerrainIndexGenerator(int gridResolution)
    : gridRes_(gridResolution) {}

int TerrainIndexGenerator::computeStitchConfig(bool needLeftStitch, bool needRightStitch,
                                               bool needBottomStitch, bool needTopStitch) {
    for (int config = 0; config < 16; ++config) {
        const StitchEdges& e = kStitchTable[config];
        if (e.left == needLeftStitch && e.right == needRightStitch &&
            e.bottom == needBottomStitch && e.top == needTopStitch) {
            return config;
        }
    }
    return 0;
}

/*
 * Siatka węzła ma (gridRes+1) x (gridRes+1) wierzchołków; wierzchołek (z, x) ma indeks z + x * (gridRes + 1).
 * Poziom LOD co krok = 2^lod bierze co któryś wierzchołek (większy krok = mniej trójkątów).
 *
 * Zszywanie: sąsiad z rzadszą siatką (krok 2x większy) nie ma co drugiego wierzchołka na wspólnej krawędzi.
 * Gdyby nasze trójkąty go używały, powstałaby szczelina ("T-junction"). Na zszywanej krawędzi bierzemy więc
 * po dwie komórki naraz i pokrywamy je "wachlarzem" 3 trójkątów, który na krawędzi używa tylko co drugiego
 * wierzchołka:
 *
 *        krawędź        wnętrze
 *        A(2s) -------- +
 *          |  \   (3)   |
 *          |    \       |
 *          | (2)  >---- M      (lewa krawędź; M = środek po stronie wnętrza)
 *          |    /       |
 *          |  /   (1)   |
 *        A(0) --------- +
 *
 * Trójkąty (1) i (3) przylegają do rogu węzła i dotykają prostopadłej krawędzi w jej wierzchołku pośrednim.
 * Gdy ta prostopadła krawędź też jest zszywana, taki trójkąt pomijamy (jej wachlarz pokrywa ten fragment).
 * Komórki, które nie leżą na żadnej zszywanej krawędzi, dostają zwykłe dwa trójkąty.
 */
void TerrainIndexGenerator::generate(int lodLevel, int stitchConfig, std::vector<uint32_t>& indices) const {
    const int n = gridRes_;
    const int s = std::min(1 << lodLevel, n);
    const auto idx = [n](int z, int x) { return static_cast<uint32_t>(z + x * (n + 1)); };
    const auto tri = [&](int z0, int x0, int z1, int x1, int z2, int x2) {
        indices.push_back(idx(z0, x0));
        indices.push_back(idx(z1, x1));
        indices.push_back(idx(z2, x2));
    };

    // Węzeł z jedną komórką (najgrubszy LOD) nie ma czego zszywać.
    const StitchEdges e = (s >= n || stitchConfig < 0 || stitchConfig > 15) ? kStitchTable[0] : kStitchTable[stitchConfig];

    // Zwykłe komórki (poza zszywanymi krawędziami).
    for (int z = 0; z < n; z += s) {
        for (int x = 0; x < n; x += s) {
            const bool onStitchedEdge = (e.left && x == 0) || (e.right && x == n - s) ||
                                        (e.bottom && z == 0) || (e.top && z == n - s);
            if (onStitchedEdge) continue;
            tri(z, x, z + s, x, z, x + s);
            tri(z, x + s, z + s, x, z + s, x + s);
        }
    }

    // Wachlarze na zszywanych krawędziach - po dwie komórki (2 * s) wzdłuż krawędzi.
    const int last = n - 2 * s;   // początek ostatniej pary (przy drugim rogu)
    for (int a = 0; a < n; a += 2 * s) {
        if (e.left) {             // x = 0, idziemy wzdłuż z
            if (!(e.bottom && a == 0)) tri(a, 0, a + s, s, a, s);
            tri(a, 0, a + 2 * s, 0, a + s, s);
            if (!(e.top && a == last)) tri(a + s, s, a + 2 * s, 0, a + 2 * s, s);
        }
        if (e.right) {            // x = n, idziemy wzdłuż z
            const int x = n - s;
            if (!(e.bottom && a == 0)) tri(a, x, a + s, x, a, x + s);
            tri(a, x + s, a + s, x, a + 2 * s, x + s);
            if (!(e.top && a == last)) tri(a + s, x, a + 2 * s, x, a + 2 * s, x + s);
        }
        if (e.bottom) {           // z = 0, idziemy wzdłuż x
            if (!(e.left && a == 0)) tri(0, a, s, a, s, a + s);
            tri(0, a, s, a + s, 0, a + 2 * s);
            if (!(e.right && a == last)) tri(s, a + s, s, a + 2 * s, 0, a + 2 * s);
        }
        if (e.top) {              // z = n, idziemy wzdłuż x
            const int z = n - s;
            if (!(e.left && a == 0)) tri(z, a, z + s, a, z, a + s);
            tri(z + s, a, z + s, a + 2 * s, z, a + s);
            if (!(e.right && a == last)) tri(z, a + s, z + s, a + 2 * s, z, a + 2 * s);
        }
    }
}

} // namespace engine3d
