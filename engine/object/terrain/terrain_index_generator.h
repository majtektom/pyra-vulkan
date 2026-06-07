#pragma once

#include <cstdint>
#include <vector>

namespace engine3d {

/**
 * @brief Generator indeksów trójkątów dla terenu z systemem LOD i zszywaniem krawędzi.
 *
 * Generuje bufory indeksowe dla węzła terenu (siatka gridRes_ x gridRes_).
 * Obsługuje 5 poziomów LOD (0-4) oraz 16 konfiguracji zszywania krawędzi (0-15).
 *
 * Konfiguracje zszywania:
 *   0  - brak, 1 - lewa, 2 - prawa, 3 - dolna, 4 - górna,
 *   5  - lewa+prawa, 6 - dolna+górna, 7 - lewa+dolna,
 *   8  - dolna+prawa, 9 - prawa+górna, 10 - górna+lewa,
 *   11 - lewa+dolna+prawa, 12 - dolna+prawa+górna,
 *   13 - prawa+górna+lewa, 14 - górna+lewa+dolna, 15 - wszystkie.
 */
class TerrainIndexGenerator {
public:
    explicit TerrainIndexGenerator(int gridResolution = 16);

    void generate(int lodLevel, int stitchConfig, std::vector<uint32_t>& indices) const;

    int gridResolution() const { return gridRes_; }

    static int computeStitchConfig(bool needLeftStitch, bool needRightStitch,
                                    bool needBottomStitch, bool needTopStitch);

private:
    int gridRes_;
};

} // namespace engine3d
