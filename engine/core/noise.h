#pragma once

#include <cmath>
#include <cstdint>

namespace engine3d {

/**
 * @brief Szum wartości 2D (value noise) z fbm - do proceduralnego terenu, rozmieszczania lasów itp.
 *
 * Wartości w zakresie mniej więcej -1..1, ciągłe (sąsiednie punkty mają podobne wartości).
 * Ten sam seed daje zawsze ten sam wynik - mapę da się odtworzyć z ziarna.
 *
 *   ValueNoise n{seed};
 *   float h = n.fbm(x / 200.0f, z / 200.0f, 5);   // 5 oktaw: duże wzgórza + drobne nierówności
 *
 * Skala wejścia decyduje o "rozmiarze" struktur: x/200 = wzgórza co ~200 jednostek.
 */
struct ValueNoise {
    uint32_t seed = 1;

    // Pseudolosowa wartość -1..1 w węźle siatki (x, z).
    [[nodiscard]] float hash(int x, int z) const {
        uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu) * 2.0f - 1.0f;
    }

    // Wartość w dowolnym punkcie: interpolacja (smoothstep) czterech sąsiednich węzłów.
    [[nodiscard]] float value(float x, float z) const {
        const int ix = static_cast<int>(std::floor(x));
        const int iz = static_cast<int>(std::floor(z));
        const float fx = x - static_cast<float>(ix);
        const float fz = z - static_cast<float>(iz);
        const float ux = fx * fx * (3.0f - 2.0f * fx);
        const float uz = fz * fz * (3.0f - 2.0f * fz);
        const float a = hash(ix, iz), b = hash(ix + 1, iz);
        const float c = hash(ix, iz + 1), d = hash(ix + 1, iz + 1);
        return (a + (b - a) * ux) * (1.0f - uz) + (c + (d - c) * ux) * uz;
    }

    // Fractal Brownian motion: suma oktaw (każda 2x drobniejsza i 2x słabsza), znormalizowana do -1..1.
    [[nodiscard]] float fbm(float x, float z, int octaves) const {
        float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
        for (int i = 0; i < octaves; ++i) {
            sum += value(x * freq + static_cast<float>(i) * 17.3f, z * freq - static_cast<float>(i) * 9.1f) * amp;
            norm += amp;
            amp *= 0.5f;
            freq *= 2.03f;
        }
        return sum / norm;
    }
};

} // namespace engine3d
