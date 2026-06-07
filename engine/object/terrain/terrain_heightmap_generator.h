#pragma once

#include <vector>
#include <string>

namespace engine3d {

/**
 * @brief Generator proceduralnych map wysokości terenu.
 *
 * Zawiera statyczne metody implementujące różne algorytmy generowania terenu.
 * Każda metoda zwraca wektor wartości wysokości oraz ustawia rozmiar mapy.
 * Wszystkie algorytmy są deterministyczne przy tym samym ziarnie (seed).
 */
class TerrainHeightmapGenerator {
public:
    /**
     * @brief Algorytm Fault Formation (uskoki tektoniczne).
     *
     * Tworzy teren poprzez wielokrotne przesuwanie wysokości wzdłuż losowych linii
     * podziału. Każda iteracja dodaje nowy uskok, a pomiędzy iteracjami stosowany
     * jest filtr wygładzający krawędzie.
     *
     * @param size Rozmiar mapy wysokości (size x size).
     * @param iterations Liczba iteracji uskoków.
     * @param maxDelta Maksymalna zmiana wysokości na początku.
     * @param minDelta Minimalna zmiana wysokości na końcu.
     * @param itersPerFilter Co ile iteracji stosować filtr wygładzający (0 = brak).
     * @param filter Współczynnik filtru (0-1).
     * @param outHeightmap Wektor wyjściowy na wartości wysokości.
     * @param outSize Rozmiar wyjściowy mapy.
     */
    static void generateFault(int size, int iterations, int maxDelta, int minDelta,
                              int itersPerFilter, float filter,
                              std::vector<float>& outHeightmap, int& outSize);

    /**
     * @brief Algorytm Plasma / Midpoint Displacement (Diamond-Square).
     *
     * Tworzy fraktalny teren poprzez rekurencyjne dzielenie kwadratów i
     * dodawanie losowych przemieszczeń w punktach środkowych.
     *
     * @param size Rozmiar mapy wysokości (size x size).
     * @param roughness Chropowatość terenu (0 = gładki, 1 = bardzo nierówny).
     * @param outHeightmap Wektor wyjściowy na wartości wysokości.
     * @param outSize Rozmiar wyjściowy mapy.
     */
    static void generatePlasma(int size, float roughness,
                               std::vector<float>& outHeightmap, int& outSize);

    /**
     * @brief Algorytm Particle Deposition (osadzanie cząstek).
     *
     * Symuluje osadzanie się cząstek (np. lawy) poprzez losowe spacery.
     * Tworzy struktury przypominające wulkany tarczowe i kratery.
     *
     * @param size Rozmiar mapy wysokości (size x size).
     * @param jumps Liczba skoków (zastrzyków cząstek).
     * @param peakWalk Liczba kroków spaceru po szczycie.
     * @param minPart Minimalna liczba cząstek na skok.
     * @param maxPart Maksymalna liczba cząstek na skok.
     * @param caldera Współczynnik kaldery (0-1, 0 = brak).
     * @param outHeightmap Wektor wyjściowy na wartości wysokości.
     * @param outSize Rozmiar wyjściowy mapy.
     */
    static void generateDeposition(int size, int jumps, int peakWalk,
                                   int minPart, int maxPart, float caldera,
                                   std::vector<float>& outHeightmap, int& outSize);

private:
    static float rangedRandom(float v1, float v2);
};

} // namespace engine3d
