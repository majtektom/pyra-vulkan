#include "object/terrain/terrain_heightmap_generator.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace engine3d {

float TerrainHeightmapGenerator::rangedRandom(float v1, float v2) {
    return v1 + (v2 - v1) * ((float)rand() / (float)RAND_MAX);
}

void TerrainHeightmapGenerator::generateFault(
    int size, int iterations, int maxDelta, int minDelta,
    int itersPerFilter, float filter,
    std::vector<float>& outHeightmap, int& outSize)
{
    outSize = size;
    outHeightmap.assign(size * size, 10.0f);
    srand((unsigned)time(nullptr));

    for (int i = 0; i < iterations; ++i) {
        int dHeight = maxDelta - ((maxDelta - minDelta) * i) / iterations;
        int x1 = rand() % size, y1 = rand() % size, x2, y2;
        do { x2 = rand() % size; y2 = rand() % size; } while (x2 == x1 && y2 == y1);
        int dx1 = x2 - x1, dy1 = y2 - y1;

        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                if ((x - x1) * dy1 - dx1 * (y - y1) > 0)
                    outHeightmap[x + size * y] += (float)dHeight;

        if (itersPerFilter && (i % itersPerFilter) == 0) {
            auto filterBand = [&](float* band, int stride, int count, float f) {
                float v = band[0];
                for (int j = stride; j < count * stride; j += stride) {
                    band[j] = f * v + (1 - f) * band[j];
                    v = band[j];
                }
            };
            for (int r = 0; r < size; ++r) {
                filterBand(&outHeightmap[size * r], 1, size, filter);
                filterBand(&outHeightmap[size * r + size - 1], -1, size, filter);
                filterBand(&outHeightmap[r], size, size, filter);
                filterBand(&outHeightmap[size * (size - 1) + r], -size, size, filter);
            }
        }
    }

    float minV = outHeightmap[0], maxV = outHeightmap[0];
    for (auto h : outHeightmap) { if (h < minV) minV = h; if (h > maxV) maxV = h; }
    if (maxV > minV) {
        float dh = maxV - minV;
        for (auto& h : outHeightmap) h = ((h - minV) / dh) * 300.0f;
    }
}

void TerrainHeightmapGenerator::generatePlasma(
    int size, float roughness,
    std::vector<float>& outHeightmap, int& outSize)
{
    outSize = size;
    outHeightmap.assign(size * size, 0.0f);
    srand((unsigned)time(nullptr));

    int rectSize = size;
    float dh = (float)rectSize / 2.0f;
    float r = (float)pow(2.0, -roughness);

    while (rectSize > 0) {
        // Diamond step
        for (int i = 0; i < size; i += rectSize)
            for (int j = 0; j < size; j += rectSize) {
                int ni = (i + rectSize) % size;
                int nj = (j + rectSize) % size;
                int mi = i + rectSize / 2;
                int mj = j + rectSize / 2;
                outHeightmap[mi + mj * size] =
                    (outHeightmap[i + j * size] + outHeightmap[ni + j * size] +
                     outHeightmap[i + nj * size] + outHeightmap[ni + nj * size]) / 4
                    + rangedRandom(-dh / 2, dh / 2) * r;
            }

        // Square step
        for (int i = 0; i < size; i += rectSize)
            for (int j = 0; j < size; j += rectSize) {
                int ni = (i + rectSize) % size;
                int nj = (j + rectSize) % size;
                int mi = i + rectSize / 2;
                int mj = j + rectSize / 2;
                int pmi = (i - rectSize / 2 + size) % size;
                int pmj = (j - rectSize / 2 + size) % size;
                outHeightmap[mi + j * size] =
                    (outHeightmap[i + j * size] + outHeightmap[ni + j * size] +
                     outHeightmap[mi + mj * size] + outHeightmap[mi + pmj * size]) / 4
                    + rangedRandom(-dh / 2, dh / 2) * r;
                outHeightmap[i + mj * size] =
                    (outHeightmap[i + j * size] + outHeightmap[i + nj * size] +
                     outHeightmap[mi + mj * size] + outHeightmap[pmi + mj * size]) / 4
                    + rangedRandom(-dh / 2, dh / 2) * r;
            }

        rectSize /= 2;
        dh *= (float)pow(2.0, -roughness);
    }
}

void TerrainHeightmapGenerator::generateDeposition(
    int size, int jumps, int peakWalk,
    int minPart, int maxPart, float caldera,
    std::vector<float>& outHeightmap, int& outSize)
{
    outSize = size;
    outHeightmap.assign(size * size, 0.0f);
    srand((unsigned)time(nullptr));

    for (int i = 0; i < jumps; ++i) {
        int px = rand() % size, pz = rand() % size;
        int particles = minPart + rand() % (maxPart - minPart + 1);

        for (int j = 0; j < particles; ++j) {
            if (rand() % 2) px = (px + 1) % size; else px = (px - 1 + size) % size;
            if (rand() % 2) pz = (pz + 1) % size; else pz = (pz - 1 + size) % size;
            outHeightmap[px + pz * size] += 1.0f;

            for (int w = 0; w < peakWalk; ++w) {
                int nx = px, nz = pz;
                if (rand() % 2) nx = (nx + 1) % size; else nx = (nx - 1 + size) % size;
                if (rand() % 2) nz = (nz + 1) % size; else nz = (nz - 1 + size) % size;
                if (outHeightmap[nx + nz * size] < outHeightmap[px + pz * size]) {
                    px = nx; pz = nz;
                }
                outHeightmap[px + pz * size] += 1.0f;
            }
        }
    }

    float maxV = 0;
    for (auto h : outHeightmap) if (h > maxV) maxV = h;
    if (maxV > 0 && caldera > 0.0001f) {
        for (auto& h : outHeightmap) h = maxV - h;
        float v = maxV * caldera;
        for (auto& h : outHeightmap) if (h < v) h = v;
    }
}

} // namespace engine3d
